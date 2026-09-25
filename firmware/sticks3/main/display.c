#include "display.h"

#include <stdio.h>
#include <string.h>

#include "audio_session.h"
#include "ble_gatt.h"
#include "s3p_state.h"
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

/* ST7789(SPI) 像素高字节在前，而 ESP32 帧缓冲 uint16 为小端 → 写前交换字节。
 * 否则 G/B 位互换：黄显示成紫、紫显示成绿（用户目视发现，2026-09-26）。 */
static inline uint16_t pxswap(uint16_t c) {
    return (uint16_t)((uint16_t)(c >> 8) | (uint16_t)(c << 8));
}

/* RGB565 */
#define C_BLACK  0x0000
#define C_PURPLE 0xCA9F /* 淡紫 #CC99FF */
#define C_BLUE3399 0x327F /* #3399FF */
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
                        s_fb[py * DISP_W + px] = pxswap(color);
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

/* 任意像素高字号（最近邻放缩 8x8 字形），与 tools/gen_screen_mock.py 一致。 */
static void draw_char_px(int x, int y, char c, uint16_t color, int size) {
    const uint8_t o = (uint8_t)c;
    if (o >= 128u) {
        return;
    }
    for (int row = 0; row < size; ++row) {
        const uint8_t bits = (uint8_t)font8x8_basic[o][row * 8 / size];
        for (int col = 0; col < size; ++col) {
            if ((bits & (uint8_t)(1u << (col * 8 / size))) == 0) {
                continue;
            }
            const int px = x + col;
            const int py = y + row;
            if (px >= 0 && px < DISP_W && py >= 0 && py < DISP_H) {
                s_fb[py * DISP_W + px] = color;
            }
        }
    }
}

static void draw_str_px(int x, int y, const char *s, uint16_t color, int size) {
    while (*s != '\0') {
        draw_char_px(x, y, *s++, color, size);
        x += size;
    }
}

static void pm1_l3b_apply(bool on) {
    if (pm1_l3b_lcd(on) != ESP_OK) {
        ESP_LOGE(TAG, "PM1 L3B %s 失败", on ? "on" : "off");
    }
}

static void fill(uint16_t color) {
    for (size_t i = 0; i < DISP_W * DISP_H; ++i) {
        s_fb[i] = pxswap(color);
    }
    esp_lcd_panel_draw_bitmap(s_panel, 0, 0, DISP_W, DISP_H, s_fb);
}

/* 三原色横条 + 白角：同时证明 R/G/B 通道、显存偏移与像素寻址正确 */
static void draw_boot_pattern(void) {
    for (int y = 0; y < DISP_H; ++y) {
        const uint16_t c =
            (y < DISP_H / 3) ? C_RED : (y < 2 * DISP_H / 3) ? C_GREEN : C_BLUE;
        for (int x = 0; x < DISP_W; ++x) {
            s_fb[y * DISP_W + x] = pxswap(c);
        }
    }
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            s_fb[y * DISP_W + x] = pxswap(C_WHITE);
            s_fb[y * DISP_W + (DISP_W - 1 - x)] = pxswap(C_WHITE);
        }
    }
    esp_lcd_panel_draw_bitmap(s_panel, 0, 0, DISP_W, DISP_H, s_fb);
}

/* 状态文字（M4 STATE 契约 v1.0）：未知不伪装、错误不冒充 idle（ST-04）。 */
static const char *status_text(uint8_t status) {
    switch (status) {
        case S3P_STATUS_IDLE:
            return "IDLE";
        case S3P_STATUS_BUSY:
            return "BUSY";
        case S3P_STATUS_RETRY:
            return "RETRY";
        case S3P_STATUS_WAITING:
            return "WAIT";
        case S3P_STATUS_ERROR:
            return "ERROR";
        default:
            return "UNKNOWN";
    }
}

/* 三页显示（M4/S4.3，英文界面）：状态 / 任务 / 用量；5s 自动翻页（切页归 S4.6 侧键）。 */
/* 紫色标题：页名 16px，按字距铺满整行（左到右）。 */
static void draw_title(const char *t) {
    const int n = (int)strlen(t);
    const int glyph = 16;
    const int span = DISP_W - 4 - n * glyph;
    const int gap = (n > 1 && span > 0) ? span / (n - 1) : 0;
    int x = 2;
    for (const char *p = t; *p != '\0'; ++p) {
        draw_char8(x, 2, *p, C_PURPLE, 2);
        x += glyph + gap;
    }
}

/* 用量三态：未知 "?" / 未报告 "-" / 具体值（不伪装 0，ST-08）。 */
static void u32_text(char *out, size_t cap, uint32_t v) {
    if (v == S3P_STATE_UNKNOWN) {
        snprintf(out, cap, "?");
    } else if (v == S3P_STATE_NOT_REPORTED) {
        snprintf(out, cap, "-");
    } else {
        snprintf(out, cap, "%lu", (unsigned long)v);
    }
}

static void render_page(int page, bool connected, bool recording) {
    const bool valid = ble_gatt_host_state_valid();
    const s3p_state_snapshot *s = ble_gatt_host_snapshot();
    char b[40];
    fill(C_BLACK);
    if (page == 0) {
        draw_title("STATUS");
        draw_str(4, 40, "BLE:", C_WHITE, 2);
        draw_str(4, 60, connected ? "CONN" : "ADV", C_YELLOW, 2);
        draw_str(4, 80, "HOST:", C_WHITE, 2);
        draw_str(4, 100, valid ? "OK" : "--", C_YELLOW, 2);
        const char *t = valid ? status_text(s->status) : "NO STATE";
        const int tn = (int)strlen(t);
        int sz = 23;
        if (tn > 0 && sz * tn > DISP_W - 8) {
            sz = (DISP_W - 8) / tn;
        }
        draw_str_px((DISP_W - tn * sz) / 2, 130, t, C_BLUE3399, sz);
        if (recording) {
            draw_str(4, 186, "REC", C_RED, 2);
        }
        if (valid && (s->flags & S3P_FLAG_EXPIRED) != 0u) {
            draw_str(100, 22, "STALE", C_YELLOW, 1);
        }
    } else if (page == 1) {
        draw_title("TASK");
        draw_str(4, 30, "TITLE:", C_WHITE, 2);
        snprintf(b, sizeof(b), "%.8s", (valid && s->title[0] != '\0') ? s->title : "-");
        draw_str(4, 50, b, C_WHITE, 2);
        draw_str(4, 70, "PROJ:", C_WHITE, 2);
        snprintf(b, sizeof(b), "%.8s", (valid && s->project[0] != '\0') ? s->project : "-");
        draw_str(4, 90, b, C_WHITE, 2);
        draw_str(4, 110, "TOOL:", C_WHITE, 2);
        if (valid) {
            snprintf(b, sizeof(b), "%u", (unsigned)s->tool_count);
        } else {
            snprintf(b, sizeof(b), "-");
        }
        draw_str(4, 130, b, C_WHITE, 2);
        draw_str(4, 150, "TODO:", C_WHITE, 2);
        if (valid) {
            snprintf(b, sizeof(b), "%u/%u", (unsigned)s->todo_completed,
                     (unsigned)s->todo_total);
        } else {
            snprintf(b, sizeof(b), "-");
        }
        draw_str(4, 170, b, C_WHITE, 2);
    } else {
        draw_title("USAGE");
        draw_str(4, 40, "IN:", C_WHITE, 2);
        u32_text(b, sizeof(b), valid ? s->tokens_input : S3P_STATE_UNKNOWN);
        draw_str(4, 60, b, C_YELLOW, 2);
        draw_str(4, 80, "OUT:", C_WHITE, 2);
        u32_text(b, sizeof(b), valid ? s->tokens_output : S3P_STATE_UNKNOWN);
        draw_str(4, 100, b, C_YELLOW, 2);
        draw_str(4, 120, "COST:", C_WHITE, 2);
        u32_text(b, sizeof(b), valid ? s->cost_milli : S3P_STATE_UNKNOWN);
        draw_str(4, 140, b, C_YELLOW, 2);
    }
    snprintf(b, sizeof(b), "%d/3", page + 1);
    draw_str(DISP_W - 24, DISP_H - 12, b, C_WHITE, 1);
}

static volatile bool s_page_next;

void display_next_page(void) {
    s_page_next = true;
}

void display_backlight(bool on) {
    gpio_set_level(DISP_BL, on ? 1 : 0);
}

static void display_task(void *arg) {
    (void)arg;
    bool last_conn = false;
    bool last_rec = false;
    bool last_valid = false;
    uint8_t last_status = 0xFFu;
    bool first = true;
    int page = 0;
    TickType_t last_rotate = xTaskGetTickCount();
    for (;;) {
        const bool conn = ble_gatt_connected();
        const bool rec = audio_session_active();
        const bool valid = ble_gatt_host_state_valid();
        const uint8_t status = ble_gatt_host_status();
        const TickType_t now = xTaskGetTickCount();
        const bool key = s_page_next;
        s_page_next = false;
        const bool rotate = (now - last_rotate) >= pdMS_TO_TICKS(5000);
        if (key || rotate) {
            page = (page + 1) % 3;
            last_rotate = now;
        }
        if (key || rotate || first || conn != last_conn || rec != last_rec ||
            valid != last_valid || status != last_status) {
            render_page(page, conn, rec);
            last_conn = conn;
            last_rec = rec;
            last_valid = valid;
            last_status = status;
            first = false;
        }
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
