#include <inttypes.h>
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_intr_alloc.h"
#include "usb/usb_host.h"
#include "usb/uac_host.h"
#include "beep.h"
#include "lucid_board.h"

static const char *TAG = "lucid_audio";
typedef struct {
    bool connected;
    uint8_t addr, iface;
    uac_host_device_handle_t handle;
} audio_event_t;
static QueueHandle_t events;
static atomic_uint transfer_errors;
static uac_host_device_handle_t speaker;
static bool playing;
static beep_t tone;
static uint8_t pcm[960 * 2 * 2]; // 20 ms at 48 kHz, stereo PCM16

static void enqueue(const audio_event_t *event)
{
    // No blocking or device API calls in the driver's callback task.
    // Lifecycle events must never silently disappear.
    ESP_ERROR_CHECK(xQueueSend(events, event, 0) == pdTRUE ? ESP_OK : ESP_ERR_NO_MEM);
}

static void device_event(uac_host_device_handle_t handle,
                         uac_host_device_event_t event, void *arg)
{
    (void)arg;
    if (event == UAC_HOST_DRIVER_EVENT_DISCONNECTED) {
        audio_event_t message = {.handle = handle};
        enqueue(&message);
    } else if (event == UAC_HOST_DEVICE_EVENT_TRANSFER_ERROR) {
        atomic_fetch_add(&transfer_errors, 1);
    }
    // TX_DONE can fire often; the producer is paced by ring-buffer writes.
}

static void driver_event(uint8_t addr, uint8_t iface,
                         uac_host_driver_event_t event, void *arg)
{
    (void)arg;
    if (event == UAC_HOST_DRIVER_EVENT_TX_CONNECTED) {
        audio_event_t message = {.connected = true, .addr = addr, .iface = iface};
        enqueue(&message);
    }
}

static void host_events(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t flags;
        ESP_ERROR_CHECK(usb_host_lib_handle_events(portMAX_DELAY, &flags));
        if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            esp_err_t err = usb_host_device_free_all();
            if (err != ESP_OK && err != ESP_ERR_NOT_FINISHED)
                ESP_LOGW(TAG, "USB cleanup: %s", esp_err_to_name(err));
        }
    }
}

static bool supports_rate(const uac_host_dev_alt_param_t *p, uint32_t rate)
{
    if (!p->sample_freq_type)
        return rate >= p->sample_freq_lower && rate <= p->sample_freq_upper;
    for (unsigned i = 0; i < p->sample_freq_type && i < UAC_FREQ_NUM_MAX; ++i)
        if (p->sample_freq[i] == rate) return true;
    return false;
}

static bool choose_format(uac_host_device_handle_t handle, uac_host_stream_config_t *cfg)
{
    uac_host_dev_info_t info;
    if (uac_host_get_device_info(handle, &info) != ESP_OK) return false;
    ESP_LOGI(TAG, "DAC %04x:%04x, interface %u", info.VID, info.PID, info.iface_num);
    uac_host_printf_device_param(handle);
    const uint32_t rates[] = {48000, 44100};
    for (unsigned r = 0; r < 2; ++r) {
        for (unsigned channels = 2; channels >= 1; --channels) {
            for (unsigned alt = 1; alt <= info.iface_alt_num; ++alt) {
                uac_host_dev_alt_param_t p;
                if (uac_host_get_device_alt_param(handle, alt, &p) != ESP_OK) continue;
                if (p.format == 1 && p.channels == channels && p.bit_resolution == 16 &&
                    p.subframe_size == 2 && supports_rate(&p, rates[r])) {
                    *cfg = (uac_host_stream_config_t){
                        .channels = channels, .bit_resolution = 16, .sample_freq = rates[r]};
                    return true;
                }
            }
        }
    }
    return false;
}

static void connect_speaker(const audio_event_t *event)
{
    if (speaker) return; // One playback interface at a time.
    uac_host_device_config_t config = {
        .addr = event->addr, .iface_num = event->iface,
        .buffer_size = 8192, .buffer_threshold = 2048, .callback = device_event,
    };
    esp_err_t err = uac_host_device_open(&config, &speaker);
    if (err != ESP_OK) {
        speaker = NULL;
        ESP_LOGW(TAG, "Open DAC: %s", esp_err_to_name(err));
        lucid_status("DAC OPEN FAILED", "RECONNECT DAC");
        return;
    }
    uac_host_stream_config_t stream = {0};
    if (!choose_format(speaker, &stream)) {
        ESP_LOGW(TAG, "Need UAC1 PCM16 mono/stereo at 48000 or 44100 Hz");
        lucid_status("FORMAT UNSUPPORTED", "NEED 16 BIT PCM");
        // Keep the handle until disconnect so the lifecycle stays serialized.
        return;
    }
    tone = (beep_t){.rate = stream.sample_freq, .channels = stream.channels};
    lucid_status("BEEP TEST", stream.sample_freq == 48000 ? "48000 HZ 16 BIT" : "44100 HZ 16 BIT");
    err = uac_host_device_start(speaker, &stream);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Start DAC: %s", esp_err_to_name(err));
        lucid_status("DAC START FAILED", "RECONNECT DAC");
        return;
    }
    // Hardware controls are optional. Software attenuation always applies.
    err = uac_host_device_set_volume(speaker, 75);
    if (err != ESP_OK) ESP_LOGW(TAG, "Volume control: %s", esp_err_to_name(err));
    err = uac_host_device_set_mute(speaker, false);
    if (err != ESP_OK) ESP_LOGW(TAG, "Mute control: %s", esp_err_to_name(err));
    playing = true;
    ESP_LOGI(TAG, "Playing 880 Hz beep: %" PRIu32 " Hz, %u channels, PCM16", tone.rate, tone.channels);
}

void app_main(void)
{
    lucid_board_init();
    lucid_status("CONNECT USB DAC", "UAC1 BEEP TEST");
    events = xQueueCreate(16, sizeof(audio_event_t));
    ESP_ERROR_CHECK(events ? ESP_OK : ESP_ERR_NO_MEM);
    const usb_host_config_t host = {.intr_flags = ESP_INTR_FLAG_LEVEL1};
    ESP_ERROR_CHECK(usb_host_install(&host));
    ESP_ERROR_CHECK(xTaskCreate(host_events, "usb_events", 4096, NULL, 5, NULL) == pdPASS
                    ? ESP_OK : ESP_ERR_NO_MEM);
    const uac_host_driver_config_t uac = {
        .create_background_task = true, .task_priority = 5,
        .stack_size = 4096, .core_id = 0, .callback = driver_event,
    };
    ESP_ERROR_CHECK(uac_host_install(&uac));
    ESP_LOGI(TAG, "Waiting for a powered USB UAC1 DAC. Logs: TX GPIO13, RX GPIO14, 115200.");
    unsigned failures = 0;
    for (;;) {
        audio_event_t event;
        if (xQueueReceive(events, &event, playing ? 0 : pdMS_TO_TICKS(250))) {
            if (event.connected) {
                connect_speaker(&event);
            } else if (event.handle == speaker) {
                playing = false;
                // The application task owns all writes and closes: no writer/close race.
                ESP_ERROR_CHECK(uac_host_device_close(speaker));
                speaker = NULL;
                failures = 0;
                atomic_store(&transfer_errors, 0);
                lucid_status("CONNECT USB DAC", "DAC DISCONNECTED");
                ESP_LOGI(TAG, "DAC disconnected; waiting for reconnection");
            }
            continue;
        }
        if (!playing) continue;
        const unsigned errors = atomic_exchange(&transfer_errors, 0);
        if (errors) ESP_LOGW(TAG, "USB transfer errors: %u", errors);
        beep_t next = tone;
        const size_t frames = tone.rate / 50; // 20 ms; integral at both supported rates
        beep_fill(&next, pcm, frames);
        esp_err_t err = uac_host_device_write(speaker, pcm, frames * tone.channels * 2,
                                             pdMS_TO_TICKS(100));
        if (err == ESP_OK) {
            tone = next;
            failures = 0;
        } else {
            if (++failures == 1) ESP_LOGW(TAG, "Audio write: %s", esp_err_to_name(err));
            if (failures >= 10) {
                playing = false;
                uac_host_device_stop(speaker);
                lucid_status("AUDIO STOPPED", "RECONNECT DAC");
                ESP_LOGE(TAG, "Repeated write failures; reconnect the DAC");
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}
