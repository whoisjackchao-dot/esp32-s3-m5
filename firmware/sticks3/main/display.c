#include "display.h"

#include <string.h>

#include "audio_session.h"
#include "ble_gatt.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "font8x8_basic.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pm1.h"

static const char *TAG = "display";

/* 硬件文档 §3.1/§3.2（M5GFX board_M5StickS3 实测值） */
#define DISP_MOSI 39
#define DISP_SCK  40
#define DISP_DC   45
#define DISP_CS   41
#define DISP_RST  21
#define DISP_BL   38
#define DISP_W    135
#define DISP_H    240
#define DISP_GAP_X 52
#define DISP_GAP_Y 40

/* M5PM1 寄存器操作已收口到 pm1.c（ES8311 使能与 L3B 同在 GPIO_OUT 0x11） */

static esp_lcd_panel_handle_t s_panel;
static uint16_t s_fb[DISP_W * DISP_H];

/* RGB565 */
#define C_WHITE  0xFFFF
#define C_RED    0xF800
#define C_GREEN  0x07E0
#define C_BLUE   0x001F
#define C_NAVY   0x0008
#define C_DGREEN 0x03E0
#define C_YELLOW 0xFFE0

/* font8x8（公有领域）：行字节，bit0=最左像素；scale 整数放大 */
static void draw_char8(int x, int y, char c, uint16_t color, int scale) {
    const uint8_t idx = (uint8_t)c;
    if (idx >= 128u) {
        return;
    }
    for (int row = 0; row < 8; ++row) {
        const uint8_t bits = (uint8_t)font8x8_basic[idx][row];
        for (int col = 0; col < 8; ++col) {
            if ((bits & (uint8_t)(1u << col)) == 0) {
                continue;
            }
            for (int sy = 0; sy < scale; ++sy) {
                for (int sx = 0; sx < scale; ++sx) {
                    const int px = x + col * scale + sx;
                    const int py = y + row * scale + sy;
                    if (px < DISP_W && py < DISP_H) {
                        s_fb[py * DISP_W + px] = color;
                    }
                }
            }
        }
    }
}

static void draw_str(int x, int y, const char *s, uint16_t color, int scale) {
    while (*s) {
        draw_char8(x, y, *s++, color, scale);
        x += 8 * scale;
    }
}

static void pm1_l3b_apply(bool on) {
    if (pm1_l3b_lcd(on) != ESP_OK) {
        ESP_LOGE(TAG, "PM1 L3B %s 失败", on ? "on" : "off");
    }
}

static void fill(uint16_t color) {
    for (size_t i = 0; i < DISP_W * DISP_H; ++i) {
        s_fb[i] = color;
    }
    esp_lcd_panel_draw_bitmap(s_panel, 0, 0, DISP_W, DISP_H, s_fb);
}

/* 三原色横条 + 白角：同时证明 R/G/B 通道、显存偏移与像素寻址正确 */
static void draw_boot_pattern(void) {
    for (int y = 0; y < DISP_H; ++y) {
        const uint16_t c =
            (y < DISP_H / 3) ? C_RED : (y < 2 * DISP_H / 3) ? C_GREEN : C_BLUE;
        for (int x = 0; x < DISP_W; ++x) {
            s_fb[y * DISP_W + x] = c;
        }
    }
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            s_fb[y * DISP_W + x] = C_WHITE;
            s_fb[y * DISP_W + (DISP_W - 1 - x)] = C_WHITE;
        }
    }
    esp_lcd_panel_draw_bitmap(s_panel, 0, 0, DISP_W, DISP_H, s_fb);
}

/* 状态帧：底色 + 主机状态文字（M2 纵切：未知必须如实显示）+ BLE 行 + 录音标 */
static void render_frame(bool connected, bool recording, bool state_valid,
                         bool busy) {
    fill(recording ? C_RED : (connected ? C_DGREEN : C_NAVY));
    draw_str(4, 4, "STICKS3", C_WHITE, 2);
    if (!state_valid) {
        draw_str(4, 36, "STATE", C_WHITE, 2);
        draw_str(4, 60, "UNKNOWN", C_YELLOW, 2);
    } else if (busy) {
        draw_str(4, 40, "BUSY", C_WHITE, 4);
    } else {
        draw_str(4, 40, "IDLE", C_WHITE, 4);
    }
    draw_str(4, DISP_H - 44, connected ? "BLE:CONN" : "BLE:ADV", C_WHITE, 2);
    if (recording) {
        draw_str(4, DISP_H - 22, "REC", C_WHITE, 2);
    }
}

static void display_task(void *arg) {
    (void)arg;
    bool last_rendered = false;
    bool last_conn = false;
    bool last_rec = false;
    bool last_valid = false;
    bool last_busy = false;
    bool first = true;
    for (;;) {
        const bool conn = ble_gatt_connected();
        const bool rec = audio_session_active();
        const bool valid = ble_gatt_host_state_valid();
        const bool busy = ble_gatt_host_busy();
        if (first || conn != last_conn || rec != last_rec ||
            valid != last_valid || busy != last_busy) {
            render_frame(conn, rec, valid, busy);
            last_conn = conn;
            last_rec = rec;
            last_valid = valid;
            last_busy = busy;
            first = false;
            last_rendered = true;
        }
        (void)last_rendered;
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

esp_err_t display_init(void) {
    /* 1. LCD L3B 供电轨（PM1 已由 audio_capture_init 提前初始化） */
    pm1_l3b_apply(true);
    vTaskDelay(pdMS_TO_TICKS(20));

    /* 2. 背光 */
    const gpio_config_t bl = {
        .pin_bit_mask = 1ULL << DISP_BL,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&bl);
    gpio_set_level(DISP_BL, 1);

    /* 3. SPI 总线 + ST7789 面板（§3.2：40MHz/mode0/gap52,40/反色） */
    const spi_bus_config_t bus = {
        .mosi_io_num = DISP_MOSI,
        .sclk_io_num = DISP_SCK,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .max_transfer_sz = DISP_W * DISP_H * 2 + 8,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO),
                        TAG, "spi bus");

    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = DISP_DC,
        .cs_gpio_num = DISP_CS,
        .pclk_hz = 40 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    esp_lcd_panel_io_handle_t io = NULL;
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI3_HOST, &io_cfg,
                                 &io),
        TAG, "panel io");

    const esp_lcd_panel_dev_config_t dev = {
        .reset_gpio_num = DISP_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(io, &dev, &s_panel), TAG,
                        "st7789");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel, true), TAG,
                        "invert");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(s_panel, DISP_GAP_X, DISP_GAP_Y),
                        TAG, "gap");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "on");

    /* 4. 自检图案（用户目视确认）→ 状态轮询 */
    fill(C_WHITE);
    vTaskDelay(pdMS_TO_TICKS(300));
    draw_boot_pattern();
    vTaskDelay(pdMS_TO_TICKS(600));

    if (xTaskCreate(display_task, "display", 3072, NULL, 4, NULL) != pdPASS) {
        return ESP_FAIL;
    }
    ESP_LOGI(TAG,
             "屏幕就绪：ST7789 135x240 gap(52,40) invert 背光开，状态轮询启动");
    return ESP_OK;
}
