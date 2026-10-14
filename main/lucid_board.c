#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/sdspi_host.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "lucid_board.h"

static const char *TAG = "lucid_board";
#ifdef CONFIG_LUCID_DISPLAY
static spi_device_handle_t lcd;
static bool sd_ready;
static DMA_ATTR uint16_t screen[80][160];

// Five columns per glyph, bit zero at the top. Uppercase UI only.
static const uint8_t letters[26][5] = {
    {0x7e,0x11,0x11,0x11,0x7e}, {0x7f,0x49,0x49,0x49,0x36},
    {0x3e,0x41,0x41,0x41,0x22}, {0x7f,0x41,0x41,0x22,0x1c},
    {0x7f,0x49,0x49,0x49,0x41}, {0x7f,0x09,0x09,0x09,0x01},
    {0x3e,0x41,0x49,0x49,0x7a}, {0x7f,0x08,0x08,0x08,0x7f},
    {0x00,0x41,0x7f,0x41,0x00}, {0x20,0x40,0x41,0x3f,0x01},
    {0x7f,0x08,0x14,0x22,0x41}, {0x7f,0x40,0x40,0x40,0x40},
    {0x7f,0x02,0x0c,0x02,0x7f}, {0x7f,0x04,0x08,0x10,0x7f},
    {0x3e,0x41,0x41,0x41,0x3e}, {0x7f,0x09,0x09,0x09,0x06},
    {0x3e,0x41,0x51,0x21,0x5e}, {0x7f,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7f,0x01,0x01},
    {0x3f,0x40,0x40,0x40,0x3f}, {0x1f,0x20,0x40,0x20,0x1f},
    {0x3f,0x40,0x38,0x40,0x3f}, {0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43},
};
static const uint8_t digits[10][5] = {
    {0x3e,0x51,0x49,0x45,0x3e}, {0x00,0x42,0x7f,0x40,0x00},
    {0x62,0x51,0x49,0x49,0x46}, {0x22,0x41,0x49,0x49,0x36},
    {0x18,0x14,0x12,0x7f,0x10}, {0x27,0x45,0x45,0x45,0x39},
    {0x3c,0x4a,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1e},
};

static void transfer(bool data, const void *bytes, size_t len)
{
    gpio_set_level(CONFIG_LUCID_LCD_DC, data);
    spi_transaction_t t = {.length = len * 8, .tx_buffer = bytes};
    ESP_ERROR_CHECK(spi_device_polling_transmit(lcd, &t));
}

static void command(uint8_t cmd, const uint8_t *data, size_t len)
{
    transfer(false, &cmd, 1);
    if (len) transfer(true, data, len);
}

static void text_line(unsigned x, unsigned y, const char *s, uint16_t color, unsigned scale)
{
    const uint16_t wire_color = (color << 8) | (color >> 8);
    while (*s && x + 5 * scale <= 160) {
        const unsigned char c = *s++;
        const uint8_t *glyph = c >= 'A' && c <= 'Z' ? letters[c - 'A'] :
                               c >= '0' && c <= '9' ? digits[c - '0'] : NULL;
        if (glyph) {
            for (unsigned col = 0; col < 5; ++col)
                for (unsigned row = 0; row < 7; ++row)
                    if (glyph[col] & (1 << row))
                        for (unsigned dy = 0; dy < scale && y + row * scale + dy < 80; ++dy)
                            for (unsigned dx = 0; dx < scale; ++dx)
                                screen[y + row * scale + dy][x + col * scale + dx] = wire_color;
        }
        x += 6 * scale;
    }
}

static void init_lcd(void)
{
    spi_device_interface_config_t config = {
        .clock_speed_hz = 10000000, .mode = 0,
        .spics_io_num = CONFIG_LUCID_LCD_CS, .queue_size = 1,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &config, &lcd));
    gpio_set_level(CONFIG_LUCID_LCD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(CONFIG_LUCID_LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
    command(0x01, NULL, 0); // SWRESET
    vTaskDelay(pdMS_TO_TICKS(150));
    command(0x11, NULL, 0); // SLPOUT
    vTaskDelay(pdMS_TO_TICKS(500));

    // ST7735R mini 160x80 register values matching the tested Adafruit initR
    // profile. See THIRD_PARTY.md for upstream license and attribution.
    static const struct { uint8_t cmd, len, data[16]; } init[] = {
        {0xb1,3,{0x01,0x2c,0x2d}}, {0xb2,3,{0x01,0x2c,0x2d}},
        {0xb3,6,{0x01,0x2c,0x2d,0x01,0x2c,0x2d}}, {0xb4,1,{0x07}},
        {0xc0,3,{0xa2,0x02,0x84}}, {0xc1,1,{0xc5}}, {0xc2,2,{0x0a,0x00}},
        {0xc3,2,{0x8a,0x2a}}, {0xc4,2,{0x8a,0xee}}, {0xc5,1,{0x0e}},
        {0x20,0,{0}}, {0x3a,1,{0x05}},
        {0xe0,16,{0x02,0x1c,0x07,0x12,0x37,0x32,0x29,0x2d,0x29,0x25,0x2b,0x39,0x00,0x01,0x03,0x10}},
        {0xe1,16,{0x03,0x1d,0x07,0x06,0x2e,0x2c,0x29,0x2d,0x2e,0x2e,0x37,0x3f,0x00,0x00,0x02,0x10}},
        {0x36,1,{0x60}}, // MX | MV | RGB: Adafruit rotation 3
        {0x13,0,{0}},
    };
    for (unsigned i = 0; i < sizeof(init) / sizeof(init[0]); ++i)
        command(init[i].cmd, init[i].data, init[i].len);
    vTaskDelay(pdMS_TO_TICKS(10));
    command(0x29, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
}

static void validate_pins(void)
{
    const int pins[] = {CONFIG_LUCID_SCK, CONFIG_LUCID_MOSI, CONFIG_LUCID_MISO,
        CONFIG_LUCID_SD_CS, CONFIG_LUCID_LCD_CS, CONFIG_LUCID_LCD_DC,
        CONFIG_LUCID_LCD_RST, CONFIG_LUCID_LCD_BL};
    for (unsigned i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i) {
        bool valid = (i == 2 ? GPIO_IS_VALID_GPIO(pins[i]) : GPIO_IS_VALID_OUTPUT_GPIO(pins[i])) &&
            pins[i] != 19 && pins[i] != 20 && !(pins[i] >= 26 && pins[i] <= 32);
#ifdef CONFIG_ESP_CONSOLE_UART_CUSTOM
        valid = valid && pins[i] != CONFIG_ESP_CONSOLE_UART_TX_GPIO &&
                pins[i] != CONFIG_ESP_CONSOLE_UART_RX_GPIO;
#endif
        for (unsigned j = 0; j < i; ++j) valid = valid && pins[i] != pins[j];
        if (!valid) ESP_LOGE(TAG, "Conflicting/reserved Lucid pin: GPIO%d", pins[i]);
        ESP_ERROR_CHECK(valid ? ESP_OK : ESP_ERR_INVALID_ARG);
    }
}
#endif

void lucid_board_init(void)
{
#ifdef CONFIG_LUCID_DISPLAY
    validate_pins();
    const int outputs[] = {CONFIG_LUCID_SD_CS, CONFIG_LUCID_LCD_CS,
        CONFIG_LUCID_LCD_DC, CONFIG_LUCID_LCD_RST, CONFIG_LUCID_LCD_BL};
    for (unsigned i = 0; i < sizeof(outputs) / sizeof(outputs[0]); ++i) {
        gpio_reset_pin(outputs[i]);
        gpio_set_level(outputs[i], outputs[i] != CONFIG_LUCID_LCD_BL);
        ESP_ERROR_CHECK(gpio_set_direction(outputs[i], GPIO_MODE_OUTPUT));
    }
    gpio_set_pull_mode(CONFIG_LUCID_MISO, GPIO_PULLUP_ONLY);
    spi_bus_config_t bus = {
        .mosi_io_num = CONFIG_LUCID_MOSI, .miso_io_num = CONFIG_LUCID_MISO,
        .sclk_io_num = CONFIG_LUCID_SCK, .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = sizeof(screen),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));
    // Preserve the tested Lucid sequencing: SD enters SPI mode before LCD clocks.
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    host.max_freq_khz = 10000;
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.host_id = SPI2_HOST;
    slot.gpio_cs = CONFIG_LUCID_SD_CS;
    esp_vfs_fat_sdmmc_mount_config_t mount = {
        .format_if_mount_failed = false, .max_files = 3, .allocation_unit_size = 16 * 1024,
    };
    sdmmc_card_t *card;
    esp_err_t err = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot, &mount, &card);
    sd_ready = err == ESP_OK;
    if (sd_ready) ESP_LOGI(TAG, "SD mounted at /sdcard; beep needs no files");
    else ESP_LOGW(TAG, "SD unavailable (%s); continuing with beep", esp_err_to_name(err));
    // A failed mount can release the SD CS pin. Restore its inactive level.
    gpio_set_level(CONFIG_LUCID_SD_CS, 1);
    gpio_set_direction(CONFIG_LUCID_SD_CS, GPIO_MODE_OUTPUT);
    init_lcd();
    lucid_status("STARTING USB", "BEEP TEST");
    gpio_set_level(CONFIG_LUCID_LCD_BL, 1);
#else
    ESP_LOGI(TAG, "Lucid LCD/SD disabled; USB-only mode");
#endif
}

void lucid_status(const char *status, const char *detail)
{
    ESP_LOGI(TAG, "%s | %s", status, detail);
#ifdef CONFIG_LUCID_DISPLAY
    if (!lcd) return;
    memset(screen, 0, sizeof(screen));
    text_line(4, 3, "LUCID", 0x07ff, 2);
    text_line(4, 27, status, 0xffff, 1);
    text_line(4, 43, detail, 0x07e0, 1);
    text_line(4, 65, sd_ready ? "SD READY" : "SD NOT READY", 0x8410, 1);
    // MINI160x80 rotation 3: x offset 0, y offset 24, RGB565 MSB first.
    const uint8_t columns[] = {0, 0, 0, 159};
    const uint8_t rows[] = {0, 24, 0, 103};
    command(0x2a, columns, sizeof(columns));
    command(0x2b, rows, sizeof(rows));
    command(0x2c, NULL, 0);
    transfer(true, screen, sizeof(screen));
#endif
}
