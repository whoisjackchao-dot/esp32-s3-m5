#include "button.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "button";

#define BUTTON_BTNA_GPIO GPIO_NUM_11
/* 轮询间隔须 ≥1 tick：CONFIG_FREERTOS_HZ=100 时 pdMS_TO_TICKS(5)=0 → vTaskDelay(0) 不退让，
 * 会饿死 IDLE0 触发任务看门狗（2026-09-23 首次刷机实测命中）。 */
#define BUTTON_POLL_MS 10
/* 连续同电平样本数确认（≈30ms）。去抖为实施默认值，未经用户批准（Q06 待冻结）。 */
#define BUTTON_STABLE_SAMPLES 3

_Static_assert(pdMS_TO_TICKS(BUTTON_POLL_MS) >= 1, "BUTTON_POLL_MS 折算必须 >=1 tick");

static button_event_cb_t s_on_press;
static button_event_cb_t s_on_release;
static volatile bool s_pressed;

static void button_task(void *arg) {
    (void)arg;
    bool committed = false;
    bool last_raw = false;
    int stable = 0;
    for (;;) {
        const bool raw = gpio_get_level(BUTTON_BTNA_GPIO) == 0;
        if (raw == last_raw) {
            if (stable < BUTTON_STABLE_SAMPLES) stable += 1;
        } else {
            last_raw = raw;
            stable = 0;
        }
        if (stable >= BUTTON_STABLE_SAMPLES && raw != committed) {
            committed = raw;
            s_pressed = committed;
            if (committed) {
                ESP_LOGI(TAG, "BtnA 按下");
                if (s_on_press) s_on_press();
            } else {
                ESP_LOGI(TAG, "BtnA 松开");
                if (s_on_release) s_on_release();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(BUTTON_POLL_MS));
    }
}

void button_set_handlers(button_event_cb_t on_press, button_event_cb_t on_release) {
    s_on_press = on_press;
    s_on_release = on_release;
}

bool button_is_pressed(void) {
    return s_pressed;
}

esp_err_t button_init(void) {
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BUTTON_BTNA_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    const esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config 失败: %s", esp_err_to_name(err));
        return err;
    }
    s_pressed = false;
    if (xTaskCreate(button_task, "button", 3072, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "button task create failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "BtnA(GPIO%d) 就绪：按住录音/松开结束", (int)BUTTON_BTNA_GPIO);
    return ESP_OK;
}
