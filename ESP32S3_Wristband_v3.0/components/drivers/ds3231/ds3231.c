/**
 * @file ds3231.c
 * @brief DS3231 RTC 驱动实现
 */

#include "ds3231.h"
#include "i2c_bus.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>

static const char *TAG = "ds3231";

#define DS3231_REG_SECONDS 0x00
#define DS3231_REG_STATUS  0x0F

#define DS3231_STATUS_OSF  0x80

static uint8_t bcd_to_dec(uint8_t bcd)
{
    return (uint8_t)(((bcd >> 4) * 10U) + (bcd & 0x0FU));
}

static uint8_t dec_to_bcd(uint8_t dec)
{
    return (uint8_t)(((dec / 10U) << 4) | (dec % 10U));
}

static bool is_time_valid(const ds3231_time_t *time)
{
    if (time == NULL) {
        return false;
    }
    if (time->year < 2000 || time->year > 2099) {
        return false;
    }
    if (time->month < 1 || time->month > 12) {
        return false;
    }
    if (time->day < 1 || time->day > 31) {
        return false;
    }
    if (time->day_of_week < 1 || time->day_of_week > 7) {
        return false;
    }
    if (time->hour > 23 || time->minute > 59 || time->second > 59) {
        return false;
    }
    return true;
}

esp_err_t ds3231_init(void)
{
    uint8_t seconds = 0;
    esp_err_t ret = i2c_bus_read_byte_port(I2C_BUS1_NUM, DS3231_I2C_ADDR, DS3231_REG_SECONDS, &seconds);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "DS3231 communication failed");
        return ret;
    }

    uint8_t status = 0;
    ret = i2c_bus_read_byte_port(I2C_BUS1_NUM, DS3231_I2C_ADDR, DS3231_REG_STATUS, &status);
    if (ret == ESP_OK && (status & DS3231_STATUS_OSF)) {
        ESP_LOGW(TAG, "Oscillator stop flag is set, RTC time may be invalid");
        status &= (uint8_t)~DS3231_STATUS_OSF;
        i2c_bus_write_byte_port(I2C_BUS1_NUM, DS3231_I2C_ADDR, DS3231_REG_STATUS, status);
    }

    ESP_LOGI(TAG, "DS3231 initialized");
    return ESP_OK;
}

esp_err_t ds3231_get_time(ds3231_time_t *time)
{
    if (time == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t raw[7] = {0};
    esp_err_t ret = ESP_FAIL;
    for (int attempt = 0; attempt < 3; attempt++) {
        ret = i2c_bus_read_port(I2C_BUS1_NUM, DS3231_I2C_ADDR, DS3231_REG_SECONDS, raw, sizeof(raw));
        if (ret == ESP_OK) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "I2C read failed after 3 attempts: %s", esp_err_to_name(ret));
        return ret;
    }

    time->second = bcd_to_dec((uint8_t)(raw[0] & 0x7F));
    time->minute = bcd_to_dec((uint8_t)(raw[1] & 0x7F));

    if (raw[2] & 0x40) {
        uint8_t hour_12 = bcd_to_dec((uint8_t)(raw[2] & 0x1F));
        bool is_pm = ((raw[2] & 0x20) != 0);
        if (hour_12 == 12) {
            hour_12 = 0;
        }
        time->hour = (uint8_t)(hour_12 + (is_pm ? 12 : 0));
    } else {
        time->hour = bcd_to_dec((uint8_t)(raw[2] & 0x3F));
    }

    time->day_of_week = bcd_to_dec((uint8_t)(raw[3] & 0x07));
    time->day = bcd_to_dec((uint8_t)(raw[4] & 0x3F));
    time->month = bcd_to_dec((uint8_t)(raw[5] & 0x1F));
    time->year = (uint16_t)(2000 + bcd_to_dec(raw[6]));

    if (!is_time_valid(time)) {
        ESP_LOGW(TAG, "Invalid time from DS3231: %04u-%02u-%02u %02u:%02u:%02u",
                 time->year, time->month, time->day,
                 time->hour, time->minute, time->second);
        return ESP_ERR_INVALID_STATE;
    }

    return ESP_OK;
}

esp_err_t ds3231_set_time(const ds3231_time_t *time)
{
    if (!is_time_valid(time)) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t buf[7] = {0};
    buf[0] = dec_to_bcd(time->second);
    buf[1] = dec_to_bcd(time->minute);
    buf[2] = dec_to_bcd(time->hour);  // 24-hour mode
    buf[3] = dec_to_bcd(time->day_of_week);
    buf[4] = dec_to_bcd(time->day);
    buf[5] = dec_to_bcd(time->month);
    buf[6] = dec_to_bcd((uint8_t)(time->year - 2000));

    return i2c_bus_write_port(I2C_BUS1_NUM, DS3231_I2C_ADDR, DS3231_REG_SECONDS, buf, sizeof(buf));
}
