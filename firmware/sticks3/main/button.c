#include "button.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "button";

#define BUTTON_BTNA_GPIO GPIO_NUM_11
#define BUTTON_BTNB_GPIO GPIO_NUM_12
#define BUTTON_LONG_MS 800
/* 轮询间隔须 ≥1 tick：CONFIG_FREERTOS_HZ=100 时 pdMS_TO_TICKS(5)=0 → vTaskDelay(0) 不退让，
 * 会饿死 IDLE0 触发任务看门狗（2026-09-23 首次刷机实测命中）。 */
#define BUTTON_POLL_MS 10
/* 连续同电平样本数确认（≈30ms）。去抖为实施默认值，未经用户批准（Q06 待冻结）。 */
#define BUTTON_STABLE_SAMPLES 3

_Static_assert(pdMS_TO_TICKS(BUTTON_POLL_MS) >= 1, "BUTTON_POLL_MS 折算必须 >=1 tick");

static button_event_cb_t s_on_press;
static button_event_cb_t s_on_release;
static button_event_cb_t s_side_short;
static button_event_cb_t s_side_long;
static volatile bool s_pressed;

static void button_task(void *arg) {
    (void)arg;
    bool committed = false;
    bool last_raw = false;
    int stable = 0;
    bool b_committed = false;
    bool b_last = false;
    int b_stable = 0;
    TickType_t b_tick = 0;
    bool b_long_fired = false;
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

        /* 侧键 BtnB：短按(<800ms)切页；长按(≥800ms)取消（长按释放不再触发短按）。 */
        const bool braw = gpio_get_level(BUTTON_BTNB_GPIO) == 0;
        if (braw == b_last) {
            if (b_stable < BUTTON_STABLE_SAMPLES) b_stable += 1;
        } else {
            b_last = braw;
            b_stable = 0;
        }
        if (b_stable >= BUTTON_STABLE_SAMPLES && braw != b_committed) {
            b_committed = braw;
            if (braw) {
                b_tick = xTaskGetTickCount();
                b_long_fired = false;
                ESP_LOGI(TAG, "BtnB 按下");
            } else if (!b_long_fired) {
                const uint32_t held =
                    (uint32_t)(xTaskGetTickCount() - b_tick) * portTICK_PERIOD_MS;
                if (held < BUTTON_LONG_MS) {
                    ESP_LOGI(TAG, "BtnB 短按");
                    if (s_side_short) s_side_short();
                }
            } else {
                ESP_LOGI(TAG, "BtnB 松开（长按已触发）");
            }
        }
        if (b_committed && braw && !b_long_fired) {
            const uint32_t held =
                (uint32_t)(xTaskGetTickCount() - b_tick) * portTICK_PERIOD_MS;
            if (held >= BUTTON_LONG_MS) {
                b_long_fired = true;
                ESP_LOGI(TAG, "BtnB 长按");
                if (s_side_long) s_side_long();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(BUTTON_POLL_MS));
    }
}

void button_set_handlers(button_event_cb_t on_press, button_event_cb_t on_release) {
    s_on_press = on_press;
    s_on_release = on_release;
}

void button_set_side_handlers(button_event_cb_t on_short, button_event_cb_t on_long) {
    s_side_short = on_short;
    s_side_long = on_long;
}

bool button_is_pressed(void) {
    return s_pressed;
}

esp_err_t button_init(void) {
    const gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << BUTTON_BTNA_GPIO) | (1ULL << BUTTON_BTNB_GPIO),
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
    ESP_LOGI(TAG, "BtnA(GPIO%d) 就绪：按住录音/松开结束；BtnB(GPIO%d)：短按切页/长按取消",
             (int)BUTTON_BTNA_GPIO, (int)BUTTON_BTNB_GPIO);
    return ESP_OK;
}
