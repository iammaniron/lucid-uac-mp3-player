#include "hid_media.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static unsigned allocated, claimed, commands, halted, flushed;
static unsigned raw_reports, releases;
static bool fail_claim, fail_submit;
static usb_transfer_t *control, *input;
static usb_device_handle_t device = (void *)1;
static usb_host_client_handle_t client = (void *)2;
static const uint8_t report[] = {0x05,0x0c,0x09,0x01,0xa1,0x01,
    0x09,0xcd,0x75,0x01,0x95,0x01,0x81,0x02,0x75,0x07,0x81,0x03,0xc0};
static const uint8_t config[] = {
    9,2,34,0,1,1,0,0x80,50,
    9,4,3,0,1,3,0,0,0,
    9,0x21,0x11,0x01,0,1,0x22,sizeof(report),0,
    7,5,0x83,3,8,0,10
};
const char *esp_err_to_name(esp_err_t e) { (void)e; return "test error"; }
esp_err_t usb_host_interface_claim(usb_host_client_handle_t c,usb_device_handle_t d,uint8_t i,uint8_t a) {
    assert(c==client && d==device && i==3 && a==0);
    if (fail_claim) return 1;
    ++claimed; return 0;
}
esp_err_t usb_host_interface_release(usb_host_client_handle_t c,usb_device_handle_t d,uint8_t i) {
    assert(c==client && d==device && i==3 && claimed);
    assert(!control || !control->pending); assert(!input || !input->pending);
    --claimed; return 0;
}
esp_err_t usb_host_transfer_alloc(size_t n,int iso,usb_transfer_t **t) {
    assert(!iso); *t=calloc(1,sizeof(**t)); assert(*t);
    (*t)->data_buffer=calloc(1,n); assert((*t)->data_buffer); ++allocated; return 0;
}
esp_err_t usb_host_transfer_free(usb_transfer_t *t) {
    if (!t) return 0;
    assert(!t->pending); if (t==control) control=NULL; if (t==input) input=NULL;
    free(t->data_buffer); free(t); --allocated; return 0;
}
esp_err_t usb_host_transfer_submit(usb_transfer_t *t) {
    assert(!t->pending && t->device_handle==device && t->bEndpointAddress==0x83);
    input=t; t->pending=true; return 0;
}
esp_err_t usb_host_transfer_submit_control(usb_host_client_handle_t c,usb_transfer_t *t) {
    const uint8_t setup[]={0x81,6,0,0x22,3,0,sizeof(report),0};
    assert(c==client && !t->pending && t->device_handle==device && t->bEndpointAddress==0);
    assert(t->num_bytes==8+sizeof(report) && !memcmp(t->data_buffer,setup,8));
    if (fail_submit) return 1;
    control=t; t->pending=true; return 0;
}
esp_err_t usb_host_endpoint_halt(usb_device_handle_t d,uint8_t e) {
    assert(d==device && e==0x83); ++halted; return 0;
}
esp_err_t usb_host_endpoint_flush(usb_device_handle_t d,uint8_t e) {
    assert(d==device && e==0x83); ++flushed; return 0;
}
static void command(uint8_t address,uint16_t usage) { assert(address==5 && usage==0xcd); ++commands; }
static void raw_report(uint8_t address,uint8_t interface,uint8_t endpoint,
    const uint8_t *data,size_t bytes,bool decoded,uint8_t pressed) {
    assert(address==5 && interface==3 && endpoint==0x83 && bytes==1 && decoded);
    assert(pressed<=1);
    ++raw_reports;
    if (!data[0]) ++releases;
}
static void done(usb_transfer_t *t,int status,int bytes) {
    assert(t && t->pending); t->pending=false; t->status=status; t->actual_num_bytes=bytes; t->callback(t);
}
static void attach(void) { lucid_hid_attach(client,device,5,config,sizeof(config)); }
static void ready(void) {
    attach(); assert(control && control->pending);
    memcpy(control->data_buffer+8,report,sizeof(report)); done(control,0,8+sizeof(report));
    lucid_hid_poll(command); assert(input && input->pending);
}
int main(void) {
    lucid_hid_set_report_callback(raw_report);
    ready();
    input->data_buffer[0]=1; done(input,0,1); lucid_hid_poll(command); assert(commands==1);
    input->data_buffer[0]=1; done(input,0,1); lucid_hid_poll(command); assert(commands==1);
    input->data_buffer[0]=0; done(input,0,1); lucid_hid_poll(command);
    input->data_buffer[0]=1; done(input,0,1); lucid_hid_poll(command); assert(commands==2);
    assert(raw_reports==4 && releases==1); // Raw diagnostics include repeats and release.
    // Unplug: a flush request alone must not free the pending interrupt transfer.
    lucid_hid_detach(device); lucid_hid_poll(command);
    assert(lucid_hid_busy(device) && allocated==2 && halted==1 && flushed==1);
    done(input,USB_TRANSFER_STATUS_CANCELED,0); lucid_hid_poll(command);
    assert(!lucid_hid_busy(device) && !claimed && !allocated);
    // Unplug during descriptor request: endpoint zero is never halted/flushed.
    attach(); lucid_hid_detach(device); lucid_hid_poll(command);
    assert(lucid_hid_busy(device) && halted==1);
    done(control,USB_TRANSFER_STATUS_CANCELED,0); lucid_hid_poll(command);
    assert(!lucid_hid_busy(device) && !allocated && !claimed);
    // Reconnect and input error both release ownership without affecting audio.
    ready(); done(input,USB_TRANSFER_STATUS_CANCELED,0); lucid_hid_poll(command);
    assert(!lucid_hid_busy(device) && !allocated && !claimed);
    fail_claim=true; attach(); lucid_hid_poll(command); fail_claim=false;
    assert(!lucid_hid_busy(device) && !allocated && !claimed);
    fail_submit=true; attach(); lucid_hid_poll(command); fail_submit=false;
    assert(!lucid_hid_busy(device) && !allocated && !claimed);
    // A malformed report descriptor must never start interrupt polling.
    attach(); memset(control->data_buffer+8,0,sizeof(report));
    done(control,0,8+sizeof(report)); lucid_hid_poll(command);
    assert(!lucid_hid_busy(device) && !allocated && !claimed && commands==2);
    return 0;
}
