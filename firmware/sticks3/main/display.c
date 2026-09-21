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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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

/* M5PM1（I²C 0x6E）寄存器：L3B 屏幕供电轨由 PM1 GPIO2 控制（非 ESP32 GPIO） */
#define PM1_I2C_ADDR      0x6E
#define PM1_REG_GPIO_MODE 0x10
#define PM1_REG_GPIO_OUT  0x11
#define PM1_GPIO2_L3B     0x04

static esp_lcd_panel_handle_t s_panel;
static uint16_t s_fb[DISP_W * DISP_H];

/* RGB565 */
#define C_WHITE  0xFFFF
#define C_RED    0xF800
#define C_GREEN  0x07E0
#define C_BLUE   0x001F
#define C_NAVY   0x0008
#define C_DGREEN 0x03E0

static void pm1_bit_op(uint8_t reg, uint8_t bits, bool on) {
    uint8_t val = 0;
    /* RMW：与 M5Unified bitOn/bitOff 语义一致，不破坏其他 PM1 GPIO 位 */
    if (i2c_master_write_read_device(I2C_NUM_0, PM1_I2C_ADDR, &reg, 1, &val, 1,
                                     pdMS_TO_TICKS(100)) != ESP_OK) {
        ESP_LOGE(TAG, "PM1 read reg 0x%02x failed", reg);
        return;
    }
    val = on ? (uint8_t)(val | bits) : (uint8_t)(val & (uint8_t)~bits);
    const uint8_t buf[2] = {reg, val};
    if (i2c_master_write_to_device(I2C_NUM_0, PM1_I2C_ADDR, buf, 2,
                                   pdMS_TO_TICKS(100)) != ESP_OK) {
        ESP_LOGE(TAG, "PM1 write reg 0x%02x failed", reg);
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

/* 状态底色：广播=深蓝，已连接=深绿，录音=红 */
static void draw_state(bool connected, bool recording) {
    fill(recording ? C_RED : (connected ? C_DGREEN : C_NAVY));
}

static void display_task(void *arg) {
    (void)arg;
    bool last_conn = false;
    bool last_rec = false;
    bool first = true;
    for (;;) {
        const bool conn = ble_gatt_connected();
        const bool rec = audio_session_active();
        if (first || conn != last_conn || rec != last_rec) {
            draw_state(conn, rec);
            last_conn = conn;
            last_rec = rec;
            first = false;
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

/* 总线诊断：列出所有 ACK 的 7 位地址（预期 0x18 ES8311 / 0x68 BMI270 / 0x6E PM1） */
static void i2c_scan(void) {
    for (uint8_t a = 3; a < 0x78; ++a) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        if (!cmd) continue;
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (uint8_t)((a << 1) | I2C_MASTER_WRITE), true);
        i2c_master_stop(cmd);
        const esp_err_t err =
            i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(50));
        i2c_cmd_link_delete(cmd);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "i2c scan ack: 0x%02x", a);
        }
    }
}

esp_err_t display_init(void) {
    /* 1. PM1 L3B 供电轨（依赖 audio_capture_init 已装好 I2C_NUM_0 总线） */
    i2c_scan();
    const uint8_t dev_id_reg = 0x00;
    uint8_t dev_id = 0;
    esp_err_t pm1_err = ESP_FAIL;
    for (int attempt = 0; attempt < 3 && pm1_err != ESP_OK; ++attempt) {
        pm1_err = i2c_master_write_read_device(I2C_NUM_0, PM1_I2C_ADDR,
                                               &dev_id_reg, 1, &dev_id, 1,
                                               pdMS_TO_TICKS(100));
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (pm1_err != ESP_OK) {
        ESP_LOGE(TAG, "PM1(0x6E) 不可达，屏幕供电无法开启");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "PM1 device_id=0x%02x", dev_id);
    pm1_bit_op(PM1_REG_GPIO_MODE, PM1_GPIO2_L3B, true);
    pm1_bit_op(PM1_REG_GPIO_OUT, PM1_GPIO2_L3B, true);
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
