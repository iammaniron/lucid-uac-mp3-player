#include <Arduino.h>
#include <atomic>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "usb/usb_host.h"
#include "capture.h"

namespace {
struct Interface;
struct Input {
    Interface *owner = nullptr;
    usb_transfer_t *transfer = nullptr;
    uint8_t endpoint = 0;
    uint16_t packet = 0;
    bool pending = false, done = false, stopped = false;
    bool seen = false;
    uint8_t previous[64] = {};
    size_t previousLength = 0;
};
struct Interface {
    usb_device_handle_t device = nullptr;
    uint8_t address = 0, number = 0, klass = 0;
    bool claimed = false, removing = false;
    usb_transfer_t *control = nullptr;
    bool controlPending = false, controlDone = false;
    Input inputs[2];
};
struct Device { usb_device_handle_t handle = nullptr; uint8_t address = 0; bool gone = false; } devices[4];
Interface interfaces[8];
usb_host_client_handle_t client;
QueueHandle_t events;
std::atomic<int> hostError{0};
std::atomic<bool> overflow{false};
bool ready;
uint32_t sequence, lastHeartbeat;

void completed(usb_transfer_t *transfer) {
    if (!transfer->bEndpointAddress) {
        auto *iface = static_cast<Interface *>(transfer->context);
        iface->controlPending = false; iface->controlDone = true;
    } else {
        auto *input = static_cast<Input *>(transfer->context);
        input->pending = false; input->done = true;
    }
}
void onEvent(const usb_host_client_event_msg_t *event, void *) {
    if (xQueueSend(events,event,0) != pdTRUE) overflow.store(true);
}
void hostTask(void *) {
    for (;;) {
        uint32_t flags;
        esp_err_t err = usb_host_lib_handle_events(portMAX_DELAY,&flags);
        if (err != ESP_OK) { hostError.store(err); vTaskDelete(nullptr); return; }
    }
}
#if CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK
bool allowDevice(const usb_device_desc_t *, uint8_t *configuration) { *configuration=1; return true; }
#endif
void removeInterface(Interface &iface) {
    if (iface.removing) return;
    iface.removing=true;
    for (auto &input:iface.inputs) if (input.pending) {
        usb_host_endpoint_halt(iface.device,input.endpoint);
        usb_host_endpoint_flush(iface.device,input.endpoint);
    }
    // In-flight requests must return their callbacks before any buffer is freed.
    // The host cancels control requests on unplug; never halt shared endpoint zero.
}
void attachInterface(Device &device, const uint8_t *desc, size_t bytes) {
    if (bytes<9 || desc[3]) return; // Capture active alternate zero only.
    uint8_t endpoints[2]={}; uint16_t packets[2]={}, reportBytes=0;
    unsigned count=0;
    for (size_t pos=desc[0]; pos+2<=bytes;) {
        const uint8_t *d=desc+pos;
        if (d[0]<2 || d[0]>bytes-pos) return;
        if (d[1]==5 && d[0]>=7 && (d[2]&0x80) && (d[3]&3)==3) {
            uint16_t packet=d[4] | (d[5]<<8);
            if (count<2 && packet && packet<=64) { endpoints[count]=d[2]; packets[count++]=packet; }
            else capture_log().printf("SKIP ep=%02X packet=%u (endpoint/packet limit)\n",d[2],packet);
        }
        if (desc[5]==3 && d[1]==0x21 && d[0]>=6)
            for (unsigned i=0;i<d[5] && 6+3*i+2<d[0];++i)
                if (d[6+3*i]==0x22) reportBytes=d[7+3*i] | (d[8+3*i]<<8);
        pos+=d[0];
    }
    if (!count) return;
    Interface *slot=nullptr;
    for (auto &iface:interfaces) if (!iface.device) { slot=&iface; break; }
    if (!slot) { capture_log().println("LIMIT: no free capture interface slots"); return; }
    auto &iface=*slot;
    iface.device=device.handle; iface.address=device.address; iface.number=desc[2]; iface.klass=desc[5];
    esp_err_t err=usb_host_interface_claim(client,device.handle,iface.number,0);
    if (err!=ESP_OK) {
        capture_log().printf("CLAIM FAILED if=%u: %s\n",iface.number,esp_err_to_name(err));
        removeInterface(iface); return;
    }
    iface.claimed=true;
    // Poll every interrupt-IN interface, including HID vendor usages and UAC status.
    // No Consumer-page parser or recognized-button requirement gates capture.
    for (unsigned i=0;i<count;++i) {
        auto &input=iface.inputs[i];
        input.owner=&iface; input.endpoint=endpoints[i]; input.packet=packets[i];
        err=usb_host_transfer_alloc(input.packet,0,&input.transfer);
        if (err!=ESP_OK) { capture_log().printf("ALLOC FAILED: %s\n",esp_err_to_name(err)); input.stopped=true; continue; }
        auto *t=input.transfer;
        t->device_handle=iface.device; t->bEndpointAddress=input.endpoint;
        t->num_bytes=input.packet; t->callback=completed; t->context=&input;
        capture_log().printf("LISTEN addr=%u if=%u class=%02X ep=%02X packet=%u\n",
            iface.address,iface.number,iface.klass,input.endpoint,input.packet);
    }
    // The report descriptor is diagnostic only; failed/unknown descriptors do not disable capture.
    if (reportBytes && reportBytes<=2048) {
        err=usb_host_transfer_alloc(8+reportBytes,0,&iface.control);
        if (err==ESP_OK) {
            const uint8_t setup[8]={0x81,6,0,0x22,iface.number,0,uint8_t(reportBytes),uint8_t(reportBytes>>8)};
            auto *t=iface.control;
            memcpy(t->data_buffer,setup,8); t->num_bytes=8+reportBytes;
            t->device_handle=iface.device; t->bEndpointAddress=0;
            t->callback=completed; t->context=&iface;
            err=usb_host_transfer_submit_control(client,t);
            iface.controlPending=err==ESP_OK;
        }
        if (err!=ESP_OK) capture_log().printf("REPORT DESC REQUEST FAILED if=%u: %s\n",iface.number,esp_err_to_name(err));
    } else if (iface.klass==3) capture_log().printf("REPORT DESC unavailable/too large: if=%u length=%u; raw capture continues\n",iface.number,reportBytes);
}
void attach(uint8_t address) {
    Device *slot=nullptr;
    for (auto &d:devices) {
        if (d.handle && d.address==address) return;
        if (!d.handle && !slot) slot=&d;
    }
    if (!slot) { capture_log().println("LIMIT: no free USB device slots"); return; }
    esp_err_t err=usb_host_device_open(client,address,&slot->handle);
    if (err!=ESP_OK) { capture_log().printf("OPEN: %s\n",esp_err_to_name(err)); return; }
    slot->address=address;
    const usb_device_desc_t *dev;
    const usb_config_desc_t *config;
    if (usb_host_get_device_descriptor(slot->handle,&dev)!=ESP_OK ||
        usb_host_get_active_config_descriptor(slot->handle,&config)!=ESP_OK) {
        capture_log().println("Device/config descriptor unavailable; reconnect earphones"); return;
    }
    capture_log().printf("DEVICE addr=%u VID=%04X PID=%04X bcd=%04X\n",address,dev->idVendor,dev->idProduct,dev->bcdDevice);
    capture_descriptor(1,address,0,reinterpret_cast<const uint8_t *>(dev),dev->bLength);
    const auto *data=reinterpret_cast<const uint8_t *>(config);
    const size_t bytes=config->wTotalLength;
    capture_descriptor(2,address,0,data,bytes);
    // Bound each interface's descriptor slice at the next interface descriptor.
    for (size_t pos=0;pos+2<=bytes;) {
        unsigned len=data[pos];
        if (len<2 || len>bytes-pos) { capture_log().println("Malformed config descriptor"); break; }
        if (data[pos+1]==4 && len>=9) {
            size_t end=pos+len;
            while (end+2<=bytes && data[end+1]!=4) {
                if (data[end]<2 || data[end]>bytes-end) break;
                end+=data[end];
            }
            capture_log().printf("INTERFACE %u alt=%u class=%02X subclass=%02X protocol=%02X\n",
                data[pos+2],data[pos+3],data[pos+5],data[pos+6],data[pos+7]);
            attachInterface(*slot,data+pos,end-pos);
            pos=end;
        } else pos+=len;
    }
    unsigned count=0;
    for (const auto &iface:interfaces) if (iface.device==slot->handle && iface.claimed) ++count;
    capture_log().printf("CAPTURE addr=%u: %u interrupt interface(s); press and RELEASE each button\n",address,count);
    capture_device_ready(count);
}
void report(Input &input) {
    auto &iface=*input.owner; auto *t=input.transfer;
    ++sequence;
    size_t bytes=t->actual_num_bytes;
    bool changed=!input.seen || bytes!=input.previousLength || memcmp(input.previous,t->data_buffer,bytes);
    capture_report(iface.address,iface.number,iface.klass,input.endpoint,t->data_buffer,bytes,changed);
    memcpy(input.previous,t->data_buffer,bytes); input.previousLength=bytes; input.seen=true;
}
}

void capture_begin() {
    events=xQueueCreate(16,sizeof(usb_host_client_event_msg_t));
    esp_err_t err=events ? ESP_OK : ESP_ERR_NO_MEM;
    if (err!=ESP_OK) { capture_log().println("Queue allocation failed"); return; }
    usb_host_config_t host={}; host.intr_flags=ESP_INTR_FLAG_LEVEL1;
#if CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK
    host.enum_filter_cb=allowDevice;
#endif
    err=usb_host_install(&host);
    if (err!=ESP_OK) { capture_log().printf("HOST INSTALL: %s\n",esp_err_to_name(err)); return; }
    if (xTaskCreatePinnedToCore(hostTask,"button_usb",4096,nullptr,5,nullptr,0)!=pdPASS) {
        capture_log().println("USB task allocation failed"); return;
    }
    usb_host_client_config_t observer={}; observer.max_num_event_msg=16;
    observer.async.client_event_callback=onEvent;
    err=usb_host_client_register(&observer,&client);
    if (err!=ESP_OK) { capture_log().printf("CLIENT REGISTER: %s\n",esp_err_to_name(err)); return; }
    ready=true;
    capture_log().println("HOST READY. Connect powered earphones; capture goes to SD /file.txt.");
}
void capture_poll() {
    if (!ready) return;
    esp_err_t err=usb_host_client_handle_events(client,0);
    if ((err!=ESP_OK && err!=ESP_ERR_TIMEOUT) || hostError.load() || overflow.load()) {
        capture_log().printf("USB ERROR client=%d host=%d queueOverflow=%d; reset required\n",err,hostError.load(),int(overflow.load()));
        capture_preview("USB ERROR\nRESET BOARD"); ready=false; return;
    }
    usb_host_client_event_msg_t event;
    for (unsigned n=0;n<16 && xQueueReceive(events,&event,0)==pdTRUE;++n) {
        if (event.event==USB_HOST_CLIENT_EVENT_NEW_DEV) attach(event.new_dev.address);
        else if (event.event==USB_HOST_CLIENT_EVENT_DEV_GONE) {
            capture_log().printf("DISCONNECTED t=%lu\n",millis());
            capture_preview("DISCONNECTED\nReconnect earphones");
            for (auto &d:devices) if (d.handle==event.dev_gone.dev_hdl) d.gone=true;
            for (auto &iface:interfaces) if (iface.device==event.dev_gone.dev_hdl) removeInterface(iface);
        }
    }
    for (auto &iface:interfaces) {
        if (!iface.device) continue;
        if (iface.controlDone) {
            iface.controlDone=false;
            auto *t=iface.control;
            if (!iface.removing) {
                capture_log().printf("HID_REPORT_DESC addr=%u if=%u status=%d bytes=%d\n",iface.address,iface.number,t->status,t->actual_num_bytes>=8 ? t->actual_num_bytes-8 : 0);
                if (t->status==USB_TRANSFER_STATUS_COMPLETED && t->actual_num_bytes>8)
                    capture_descriptor(3,iface.address,iface.number,t->data_buffer+8,t->actual_num_bytes-8);
            }
            usb_host_transfer_free(t); iface.control=nullptr;
        }
        for (auto &input:iface.inputs) {
            if (!input.transfer) continue;
            if (input.done) {
                input.done=false;
                if (!iface.removing) {
                    if (input.transfer->status==USB_TRANSFER_STATUS_COMPLETED) report(input);
                    else {
                        capture_log().printf("INPUT ERROR if=%u ep=%02X status=%d; reconnect to retry\n",iface.number,input.endpoint,input.transfer->status);
                        input.stopped=true;
                    }
                }
            }
            if (!iface.removing && !input.pending && !input.stopped) {
                err=usb_host_transfer_submit(input.transfer);
                input.pending=err==ESP_OK;
                if (err!=ESP_OK) {
                    capture_log().printf("SUBMIT ERROR if=%u ep=%02X: %s\n",iface.number,input.endpoint,esp_err_to_name(err));
                    input.stopped=true;
                }
            }
        }
        if (iface.removing) {
            bool pending=iface.controlPending;
            for (const auto &input:iface.inputs) pending|=input.pending;
            if (pending) continue;
            if (iface.claimed && usb_host_interface_release(client,iface.device,iface.number)!=ESP_OK) continue;
            for (auto &input:iface.inputs) usb_host_transfer_free(input.transfer);
            usb_host_transfer_free(iface.control);
            iface=Interface{};
        }
    }
    for (auto &d:devices) if (d.gone) {
        bool busy=false;
        for (const auto &iface:interfaces) busy|=iface.device==d.handle;
        if (!busy && usb_host_device_close(client,d.handle)==ESP_OK) d=Device{};
    }
    if (millis()-lastHeartbeat>=10000) {
        lastHeartbeat=millis();
        unsigned listening=0;
        for (const auto &iface:interfaces) for (const auto &input:iface.inputs) listening+=input.pending;
        capture_log().printf("ALIVE t=%lu listening=%u reports=%lu\n",millis(),listening,(unsigned long)sequence);
    }
}
