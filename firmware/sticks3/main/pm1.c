#include "pm1.h"

#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "pm1";

#define PM1_I2C_ADDR      0x6E
#define PM1_REG_DEVICE_ID 0x00
#define PM1_REG_PWR_SRC   0x04
#define PM1_REG_PWR_CFG   0x06
#define PM1_REG_I2C_CFG   0x09
#define PM1_REG_WDT_CNT   0x0A
#define PM1_REG_GPIO_MODE 0x10
#define PM1_REG_GPIO_OUT  0x11
#define PM1_REG_GPIO_DRV  0x13
#define PM1_REG_GPIO_FUNC 0x16

#define PM1_BIT_ES8311 0x08 /* GPIO3：SPK 功放使能（官方 PinMap PYG3_SPK_Pulse） */
#define PM1_BIT_L3B    0x04 /* GPIO2：L3B 供电轨（LCD/MIC/SPK；ES8311 所在） */
#define PM1_PWR_DCDC   0x02
#define PM1_PWR_LDO    0x04

static esp_err_t pm1_write_reg(uint8_t reg, uint8_t val) {
    const uint8_t buf[2] = {reg, val};
    return i2c_master_write_to_device(I2C_NUM_0, PM1_I2C_ADDR, buf, 2,
                                      pdMS_TO_TICKS(100));
}

esp_err_t pm1_init(void) {
    const uint8_t id_reg = PM1_REG_DEVICE_ID;
    uint8_t dev_id = 0;
    esp_err_t err = ESP_FAIL;
    for (int attempt = 0; attempt < 3 && err != ESP_OK; ++attempt) {
        err = i2c_master_write_read_device(I2C_NUM_0, PM1_I2C_ADDR, &id_reg, 1,
                                           &dev_id, 1, pdMS_TO_TICKS(100));
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "PM1 不可达");
        return err;
    }
    /* M5Unified begin() 同款：关 I2C idle sleep 与看门狗（防状态复位） */
    err = pm1_write_reg(PM1_REG_I2C_CFG, 0x00);
    if (err == ESP_OK) {
        err = pm1_write_reg(PM1_REG_WDT_CNT, 0x00);
    }
    /* 电源输出诊断：DCDC/LDO 若被关会连带外设失电（实测 ES8311 NACK 线索） */
    const uint8_t pwr_cfg_reg = PM1_REG_PWR_CFG;
    uint8_t pwr_cfg = 0;
    esp_err_t rd = i2c_master_write_read_device(I2C_NUM_0, PM1_I2C_ADDR,
                                                &pwr_cfg_reg, 1, &pwr_cfg, 1,
                                                pdMS_TO_TICKS(100));
    if (rd == ESP_OK) {
        ESP_LOGI(TAG, "PWR_CFG=0x%02x (chg=%d dcdc=%d ldo=%d boost=%d)",
                 pwr_cfg, pwr_cfg & 1, (pwr_cfg >> 1) & 1, (pwr_cfg >> 2) & 1,
                 (pwr_cfg >> 3) & 1);
        if ((pwr_cfg & (PM1_PWR_DCDC | PM1_PWR_LDO)) !=
            (PM1_PWR_DCDC | PM1_PWR_LDO)) {
            const esp_err_t on_err =
                pm1_write_reg(pwr_cfg_reg, (uint8_t)(pwr_cfg | PM1_PWR_DCDC | PM1_PWR_LDO));
            ESP_LOGW(TAG, "DCDC/LDO 曾被关闭，已补开 (err=%s)",
                     esp_err_to_name(on_err));
        }
    }
    ESP_LOGI(TAG, "PM1 device_id=0x%02x (wdt/idle 已禁用)", dev_id);
    return err;
}

static esp_err_t pm1_reg_bit(uint8_t reg, uint8_t bit, bool on) {
    uint8_t val = 0;
    const esp_err_t err = i2c_master_write_read_device(
        I2C_NUM_0, PM1_I2C_ADDR, &reg, 1, &val, 1, pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        return err;
    }
    val = on ? (uint8_t)(val | bit) : (uint8_t)(val & (uint8_t)~bit);
    return pm1_write_reg(reg, val);
}

esp_err_t pm1_bit(uint8_t bit, bool on) {
    return pm1_reg_bit(PM1_REG_GPIO_OUT, bit, on);
}

/* M5GFX/M5Unified 的引脚驱动顺序：0x16 清位=GPIO 功能 → 0x10 置位=输出 →
 * 0x13 清位=推挽 → 0x11 置位=电平。缺任一配置步，仅写 0x11 不会真正驱动引脚。 */
static esp_err_t pm1_gpio_output(uint8_t bit, bool level) {
    esp_err_t err = pm1_reg_bit(PM1_REG_GPIO_FUNC, bit, false);
    if (err == ESP_OK) {
        err = pm1_reg_bit(PM1_REG_GPIO_MODE, bit, true);
    }
    if (err == ESP_OK) {
        err = pm1_reg_bit(PM1_REG_GPIO_DRV, bit, false);
    }
    if (err == ESP_OK) {
        err = pm1_reg_bit(PM1_REG_GPIO_OUT, bit, level);
    }
    return err;
}

esp_err_t pm1_es8311(bool on) {
    return pm1_gpio_output(PM1_BIT_ES8311, on);
}

esp_err_t pm1_l3b_lcd(bool on) {
    return pm1_gpio_output(PM1_BIT_L3B, on);
}
