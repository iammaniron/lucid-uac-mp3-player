// Arduino USB host bring-up, based on Espressif's UAC host API/example.
// One application context owns open/start/write/close. Callbacks only notify.
#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_intr_alloc.h"
#include "esp_timer.h"
#include "usb/usb_host.h"
#include "uac/uac_host.h"
#include "music.h"
#include "player.h"
#include "usb_probe.h"
#include "uac2/uac2_host.h"
#include "usb_pcm.h"

static const char *TAG = "lucid_audio";
typedef enum { USB_ATTACH, USB_GONE, AUDIO_ATTACH, AUDIO_GONE } event_kind_t;
typedef struct {
    event_kind_t kind;
    uint8_t address, interface;
    usb_device_handle_t usb;
    void *audio;
    bool uac2;
} player_event_t;
typedef struct {
    usb_device_handle_t handle;
    uint8_t address;
    lucid_usb_probe_t probe;
    uint32_t appeared;
    bool gone;
} observed_device_t;

static QueueHandle_t events;
static usb_host_client_handle_t observer;
static observed_device_t devices[8];
static void *speaker;
static bool speaker_uac2;
static unsigned output_slot = 2, output_bits = 16;
static atomic_uint transfers, transfer_errors;
static atomic_int host_error;
static atomic_bool queue_overflow, stream_failed;
static bool ready, fatal, playing;
static uint32_t boot_time, last_display, last_progress, previous_transfers;
static uint8_t speaker_address;
static unsigned write_failures;
static unsigned output_rate, output_channels;
static size_t pending_bytes;
static bool first_chunk;
static uint8_t pcm[480 * 2 * 4]; // Ten ms stereo, widened in-place for UAC2.

unsigned lucid_audio_class(void) { return speaker ? (speaker_uac2 ? 2 : 1) : 0; }
unsigned lucid_audio_bits(void) { return output_bits; }
unsigned lucid_audio_rate(void) { return output_rate; }
static esp_err_t stop_speaker(void)
{
    return speaker_uac2 ? uac2_host_device_stop(speaker) : uac_host_device_stop(speaker);
}

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void show_error(const char *stage, esp_err_t error)
{
    ESP_LOGE(TAG, "%s: %s", stage, esp_err_to_name(error));
    char detail[26];
    snprintf(detail, sizeof(detail), "ERR 0x%X - RECONNECT", (unsigned)error);
    lucid_status(stage, detail);
}

static void post(player_event_t event)
{
    if (xQueueSend(events, &event, 0) != pdTRUE) atomic_store(&queue_overflow, true);
}

static void usb_event(const usb_host_client_event_msg_t *event, void *arg)
{
    (void)arg;
    if (event->event == USB_HOST_CLIENT_EVENT_NEW_DEV)
        post((player_event_t){.kind = USB_ATTACH, .address = event->new_dev.address});
    else if (event->event == USB_HOST_CLIENT_EVENT_DEV_GONE)
        post((player_event_t){.kind = USB_GONE, .usb = event->dev_gone.dev_hdl});
}

static void audio_event(uac_host_device_handle_t handle, uac_host_device_event_t event, void *arg)
{
    (void)arg;
    if (event == UAC_HOST_DRIVER_EVENT_DISCONNECTED)
        post((player_event_t){.kind = AUDIO_GONE, .audio = handle});
    else if (event == UAC_HOST_DEVICE_EVENT_TX_TRANSFER_DONE)
        atomic_fetch_add(&transfers, 1);
    else if (event == UAC_HOST_DEVICE_EVENT_TRANSFER_ERROR)
        atomic_fetch_add(&transfer_errors, 1);
    // TX_DONE only reports buffer demand, not successful USB transmission.
}

static void audio_found(uint8_t address, uint8_t interface, uac_host_driver_event_t event, void *arg)
{
    (void)arg;
    if (event == UAC_HOST_DRIVER_EVENT_TX_CONNECTED)
        post((player_event_t){.kind = AUDIO_ATTACH, .address = address, .interface = interface});
}

static void audio2_event(uac2_host_device_handle_t handle, uac2_host_device_event_t event, void *arg)
{
    (void)arg;
    if (event == UAC2_HOST_DEVICE_EVENT_DISCONNECTED)
        post((player_event_t){.kind = AUDIO_GONE, .audio = handle, .uac2 = true});
    else if (event == UAC2_HOST_DEVICE_EVENT_TX_TRANSFER_DONE)
        atomic_fetch_add(&transfers, 1);
    else if (event == UAC2_HOST_DEVICE_EVENT_STREAM_ERROR) {
        atomic_fetch_add(&transfer_errors, 1);
        atomic_store(&stream_failed, true);
    } else if (event == UAC2_HOST_DEVICE_EVENT_TRANSFER_ERROR)
        atomic_fetch_add(&transfer_errors, 1);
}

static void audio2_found(uint8_t address, uint8_t interface, uac2_host_driver_event_t event, void *arg)
{
    (void)arg;
    if (event == UAC2_HOST_DRIVER_EVENT_TX_CONNECTED)
        post((player_event_t){.kind = AUDIO_ATTACH, .address = address, .interface = interface, .uac2 = true});
}

static void host_task(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t flags;
        esp_err_t error = usb_host_lib_handle_events(portMAX_DELAY, &flags);
        if (error != ESP_OK) {
            atomic_store(&host_error, error);
            vTaskDelete(NULL);
            return;
        }
        // Both clients remain installed for reconnects; no NO_CLIENTS teardown.
    }
}

#if CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK
static bool allow_device(const usb_device_desc_t *descriptor, uint8_t *configuration)
{
    (void)descriptor;
    *configuration = 1;
    return true;
}
#endif

static bool supports_rate(const uac_host_dev_alt_param_t *p, uint32_t rate)
{
    if (!p->sample_freq_type)
        return rate >= p->sample_freq_lower && rate <= p->sample_freq_upper;
    for (unsigned i = 0; i < p->sample_freq_type && i < UAC_FREQ_NUM_MAX; ++i)
        if (p->sample_freq[i] == rate) return true;
    return false;
}

static bool choose_format(uac_host_stream_config_t *config)
{
    uac_host_dev_info_t info;
    if (uac_host_get_device_info(speaker, &info) != ESP_OK) return false;
    uac_host_printf_device_param(speaker);
    const uint32_t rates[] = {48000, 44100};
    for (unsigned r = 0; r < 2; ++r) {
        for (unsigned channels = 2; channels >= 1; --channels) {
            bool found = false, unsafe_layout = false;
            for (unsigned alt = 1; alt <= info.iface_alt_num; ++alt) {
                uac_host_dev_alt_param_t p;
                if (uac_host_get_device_alt_param(speaker, alt, &p) != ESP_OK) continue;
                if (p.channels != channels || p.bit_resolution != 16 || !supports_rate(&p, rates[r])) continue;
                // Driver selects an alternate by channels/bits/rate, not by byte
                // layout. Reject ambiguous modes instead of sending mispacked PCM.
                if (p.format != 1 || p.subframe_size != 2) unsafe_layout = true;
                else found = true;
            }
            if (found && !unsafe_layout) {
                *config = (uac_host_stream_config_t){.channels = channels,
                    .bit_resolution = 16, .sample_freq = rates[r]};
                return true;
            }
        }
    }
    return false;
}

static bool choose_format2(uint8_t interface, uac2_host_stream_config_t *config)
{
    uac2_device_info_t info;
    if (uac2_host_device_get_info(speaker, &info) != ESP_OK) return false;
    // The bundled driver supports one clock domain. Do not guess which of
    // several clocks, or a clock multiplier ratio, is feeding this stream.
    if (info.num_clock_sources != 1 || info.num_clock_multipliers) {
        ESP_LOGE(TAG, "UAC2 needs a single clock source without multipliers");
        return false;
    }
    uac2_sample_rate_range_t ranges[UAC2_MAX_SAMPLE_RATE_RANGES];
    uint8_t count = 0;
    esp_err_t range_error = uac2_host_device_get_sample_rate_range(speaker, ranges, &count);
    uint32_t current = 0;
    if (range_error != ESP_OK) {
        // Fixed-rate devices sometimes omit RANGE. Use only their confirmed
        // current rate, not an assumed 48 kHz setting.
        ESP_LOGW(TAG, "UAC2 RANGE failed; trying current clock rate");
        if (uac2_host_device_get_sample_rate(speaker, &current) != ESP_OK) return false;
    }
    const uint32_t rates[] = {48000, 44100};
    const uint8_t bits[] = {16, 24, 32};
    for (unsigned r = 0; r < 2; ++r) {
        bool rate_ok = range_error != ESP_OK && current == rates[r];
        for (unsigned n = 0; n < count; ++n)
            if (lucid_rate_in_range(rates[r], ranges[n].min, ranges[n].max, ranges[n].res)) rate_ok = true;
        if (!rate_ok) continue;
        for (unsigned channels = 2; channels >= 1; --channels) {
            for (unsigned b = 0; b < 3; ++b) {
                for (unsigned n = 0; n < info.num_as_ifaces; ++n) {
                    const uac2_as_iface_t *as = &info.as_ifaces[n];
                    if (as->nr_channels != channels || as->bit_resolution != bits[b] ||
                        !lucid_uac2_mode_ok(as, interface, rates[r])) continue;
                    *config = (uac2_host_stream_config_t){.channels = channels,
                        .bit_resolution = bits[b], .sample_freq = rates[r], .alt_setting = as->alt_setting};
                    output_slot = as->sub_slot_size;
                    output_bits = bits[b];
                    ESP_LOGI(TAG, "UAC2 iface %u alt %u: %lu Hz %uch %ubit/%ubyte", interface,
                        as->alt_setting, (unsigned long)rates[r], channels, bits[b], output_slot);
                    return true;
                }
            }
        }
    }
    return false;
}

static void observe_device(uint8_t address)
{
    observed_device_t *slot = NULL;
    for (unsigned i = 0; i < 8; ++i) {
        if (devices[i].handle && devices[i].address == address) return;
        if (!devices[i].handle && !slot) slot = &devices[i];
    }
    if (!slot) return;
    esp_err_t error = usb_host_device_open(observer, address, &slot->handle);
    if (error != ESP_OK) return; // Device may have unplugged while queued.
    slot->address = address;
    slot->appeared = now_ms();
    const usb_device_desc_t *desc;
    const usb_config_desc_t *config;
    if (usb_host_get_device_descriptor(slot->handle, &desc) != ESP_OK ||
        usb_host_get_active_config_descriptor(slot->handle, &config) != ESP_OK) {
        if (!speaker) lucid_status("USB DESCRIPTOR ERROR", "RECONNECT DEVICE");
        return;
    }
    slot->probe = lucid_usb_probe((const uint8_t *)config, config->wTotalLength);
    char identity[26];
    snprintf(identity, sizeof(identity), "%04X:%04X ADDR %u", desc->idVendor, desc->idProduct, address);
    ESP_LOGI(TAG, "USB enumerated %s: audio=%d UAC1-out=%d UAC2=%d hub=%d",
        identity, slot->probe.audio, slot->probe.uac1_output, slot->probe.uac2, slot->probe.hub);
    if (speaker) return;
    if (!slot->probe.valid) lucid_status("BAD USB DESCRIPTORS", identity);
    else if (slot->probe.uac1_output) lucid_status("USB DAC FOUND", identity);
    else if (slot->probe.uac2) lucid_status("UAC2 DAC FOUND", identity);
    else if (slot->probe.hub) lucid_status("USB HUB FOUND", "CONNECT DAC TO HUB");
    else if (slot->probe.audio) lucid_status("NO PLAYBACK INTERFACE", identity);
    else lucid_status("NOT AN AUDIO DAC", identity);
}

static bool any_device(void)
{
    for (unsigned i = 0; i < 8; ++i) if (devices[i].handle && !devices[i].gone) return true;
    return false;
}

static void start_speaker(const player_event_t *event)
{
    if (speaker || fatal) return;
    lucid_status("OPENING USB DAC", "CHECKING PCM FORMAT");
    speaker_uac2 = event->uac2;
    esp_err_t error;
    if (speaker_uac2) {
        const uac2_host_device_config_t device = {.addr = event->address,
            .iface_num = event->interface, .buffer_size = 32768,
            .buffer_threshold = 8192, .callback = audio2_event};
        uac2_host_device_handle_t handle = NULL;
        error = uac2_host_device_open(&device, &handle);
        speaker = handle;
    } else {
        const uac_host_device_config_t device = {.addr = event->address,
            .iface_num = event->interface, .buffer_size = 16384,
            .buffer_threshold = 4096, .callback = audio_event};
        uac_host_device_handle_t handle = NULL;
        error = uac_host_device_open(&device, &handle);
        speaker = handle;
    }
    if (error != ESP_OK) {
        speaker = NULL;
        show_error("DAC OPEN FAILED", error);
        return;
    }
    speaker_address = event->address;
    uac_host_stream_config_t format = {0};
    uac2_host_stream_config_t format2 = {0};
    output_slot = 2;
    output_bits = 16;
    if (!(speaker_uac2 ? choose_format2(event->interface, &format2) : choose_format(&format))) {
        lucid_status(speaker_uac2 ? "UAC2 MODE UNSUPPORTED" : "UAC1 MODE UNSUPPORTED", "NEED PCM 44.1K OR 48K");
        return; // Keep ownership until unplug, including failures.
    }
    output_rate = speaker_uac2 ? format2.sample_freq : format.sample_freq;
    output_channels = speaker_uac2 ? format2.channels : format.channels;
    pending_bytes = 0;
    first_chunk = true;
    if (!lucid_music_available()) {
        lucid_status("NO MUSIC READY", "CHECK SD CARD AND FILES");
        return;
    }
    lucid_music_start(output_rate, output_channels);
    atomic_store(&stream_failed, false);
    atomic_store(&transfers, 0);
    atomic_store(&transfer_errors, 0);
    previous_transfers = 0;
    write_failures = 0;
    lucid_status("STARTING MUSIC", "LOOPING SD PLAYLIST");
    error = speaker_uac2 ? uac2_host_device_start(speaker, &format2) : uac_host_device_start(speaker, &format);
    if (error != ESP_OK) {
        show_error("DAC START FAILED", error);
        return;
    }
    // Software volume remains 50%. Hardware controls are optional.
    error = speaker_uac2 ? uac2_host_device_set_volume_all_channels(speaker, -6 * 256)
                        : uac_host_device_set_volume_db(speaker, -6 * 256);
    if (error != ESP_OK) ESP_LOGW(TAG, "Optional -6 dB request: %s", esp_err_to_name(error));
    if (speaker_uac2) {
        // Try master and each channel: the driver checks advertised controls
        // before issuing a request. Unsupported channels do not block playback.
        for (unsigned channel = 0; channel <= output_channels; ++channel)
            uac2_host_device_set_mute(speaker, channel, false);
    } else {
        error = uac_host_device_set_mute(speaker, false);
        if (error != ESP_OK) ESP_LOGW(TAG, "Optional unmute request: %s", esp_err_to_name(error));
    }
    playing = true;
    last_progress = last_display = now_ms();
    ESP_LOGI(TAG, "SD music -> UAC%u PCM%u/%u %u Hz / %u channels", speaker_uac2 ? 2 : 1, output_bits, output_slot * 8, output_rate, output_channels);
}

void lucid_audio_begin(void)
{
    if (ready || fatal) return;
    boot_time = now_ms();
    events = xQueueCreate(24, sizeof(player_event_t));
    esp_err_t error = events ? ESP_OK : ESP_ERR_NO_MEM;
    if (error != ESP_OK) goto failed;
    const usb_host_config_t host = {.intr_flags = ESP_INTR_FLAG_LEVEL1,
#if CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK
        .enum_filter_cb = allow_device,
#endif
    };
    error = usb_host_install(&host);
    if (error != ESP_OK) goto failed;
    if (xTaskCreatePinnedToCore(host_task, "usb_host", 4096, NULL, 5, NULL, 0) != pdPASS) {
        error = ESP_ERR_NO_MEM;
        goto failed;
    }
    const usb_host_client_config_t monitor = {.max_num_event_msg = 8,
        .async = {.client_event_callback = usb_event}};
    error = usb_host_client_register(&monitor, &observer);
    if (error != ESP_OK) goto failed;
    const uac_host_driver_config_t driver = {.create_background_task = true,
        .task_priority = 5, .stack_size = 4096, .core_id = 0, .callback = audio_found};
    error = uac_host_install(&driver);
    if (error != ESP_OK) goto failed;
    const uac2_host_driver_config_t driver2 = {.create_background_task = true,
        .task_priority = 5, .stack_size = 6144, .core_id = 0, .callback = audio2_found};
    error = uac2_host_install(&driver2);
    if (error != ESP_OK) goto failed;
    ready = true;
    lucid_status("USB HOST READY", "CONNECT POWERED DAC");
    return;
failed:
    fatal = true;
    show_error("USB INIT FAILED", error);
}

void lucid_audio_poll(void)
{
    if (!ready || fatal) return;
    // This observer sees devices that the audio driver rejects, including unsupported audio modes.
    esp_err_t error = usb_host_client_handle_events(observer, 0);
    if (error != ESP_OK && error != ESP_ERR_TIMEOUT) {
        fatal = true;
        show_error("USB MONITOR FAILED", error);
        return;
    }
    error = atomic_load(&host_error);
    if (error != ESP_OK || atomic_load(&queue_overflow)) {
        fatal = true;
        if (speaker && playing) stop_speaker();
        playing = false;
        lucid_music_stop();
        show_error("USB RESET REQUIRED", error ? error : ESP_ERR_NO_MEM);
        return;
    }
    player_event_t event;
    // Bound event processing so the producer is not starved by an event burst.
    for (unsigned i = 0; i < 24 && xQueueReceive(events, &event, 0) == pdTRUE; ++i) {
        if (event.kind == USB_ATTACH) observe_device(event.address);
        else if (event.kind == AUDIO_ATTACH) start_speaker(&event);
        else if (event.kind == AUDIO_GONE && event.audio == speaker && event.uac2 == speaker_uac2) {
            playing = false;
            pending_bytes = 0;
            lucid_music_stop();
            error = speaker_uac2 ? uac2_host_device_close(speaker) : uac_host_device_close(speaker);
            if (error != ESP_OK) {
                fatal = true;
                show_error("DAC CLOSE FAILED", error);
                return;
            }
            speaker = NULL;
            speaker_address = 0;
            boot_time = now_ms();
            lucid_status("DAC DISCONNECTED", "CONNECT POWERED DAC");
        } else if (event.kind == USB_GONE) {
            for (unsigned j = 0; j < 8; ++j) {
                if (devices[j].handle != event.usb) continue;
                devices[j].gone = true;
                if (!speaker && !any_device()) {
                    boot_time = now_ms();
                    lucid_status("USB DISCONNECTED", "CONNECT POWERED DAC");
                }
            }
        }
    }
    // Observer has no claimed interfaces or in-flight transfers.
    for (unsigned i = 0; i < 8; ++i) {
        if (!devices[i].gone) continue;
        error = usb_host_device_close(observer, devices[i].handle);
        if (error == ESP_OK) memset(&devices[i], 0, sizeof(devices[i]));
    }
    if (playing && atomic_load(&stream_failed)) {
        playing = false;
        stop_speaker();
        lucid_music_stop();
        lucid_status("USB STREAM FAILED", "RECONNECT POWERED DAC");
        return;
    }
    const uint32_t now = now_ms();
    if (!playing) {
        if (!speaker && now - last_display >= 1000) {
            last_display = now;
            if (!any_device() && now - boot_time >= 5000)
                lucid_status("NO USB ENUMERATION", "CHECK DAC 5V AND CABLE");
            for (unsigned i = 0; i < 8; ++i)
                if (devices[i].handle && !devices[i].gone && (devices[i].probe.uac1_output || devices[i].probe.uac2) && now - devices[i].appeared >= 5000)
                    lucid_status("USB FOUND NO AUDIO", "SEE UART FOR DETAILS");
        }
        return;
    }
    if (!pending_bytes) {
        size_t frames = lucid_music_read(pcm, output_rate / 100);
        pending_bytes = lucid_pack_pcm16(pcm, pcm, frames * output_channels, output_slot);
        if (!pending_bytes) {
            if (!lucid_music_available()) {
                playing = false;
                stop_speaker();
                lucid_music_stop();
            }
            return;
        }
    }
    // Keep the same chunk until accepted; a full USB buffer must not skip music.
    error = speaker_uac2 ? uac2_host_device_write(speaker, pcm, pending_bytes, 30)
                        : uac_host_device_write(speaker, pcm, pending_bytes, pdMS_TO_TICKS(30));
    if (error == ESP_OK) {
        if (first_chunk) { last_progress = now_ms(); first_chunk = false; }
        pending_bytes = 0;
        write_failures = 0;
    } else if (++write_failures >= 10) {
        playing = false;
        stop_speaker();
        lucid_music_stop();
        show_error("AUDIO WRITE FAILED", error);
        return;
    }
    const unsigned completed = atomic_load(&transfers);
    if (completed != previous_transfers) {
        previous_transfers = completed;
        last_progress = now_ms();
    }
    if (now_ms() - last_progress >= 2000) {
        playing = false;
        stop_speaker();
        lucid_music_stop();
        lucid_status("NO AUDIO TRANSFERS", "RECONNECT POWERED DAC");
        return;
    }
    if (now - last_display >= 1000) {
        last_display = now;
        char detail[26];
        snprintf(detail, sizeof(detail), "TX %u ERR %u", completed, atomic_load(&transfer_errors));
        lucid_music_progress(completed, atomic_load(&transfer_errors));
        ESP_LOGI(TAG, "DAC addr %u: %s", speaker_address, detail);
    }
}
