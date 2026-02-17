/**
 * @file ds3231.h
 * @brief DS3231 RTC 驱动
 */

#ifndef DS3231_H
#define DS3231_H

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// DS3231 I2C 地址
#define DS3231_I2C_ADDR 0x68

/**
 * @brief DS3231 时间结构体
 */
typedef struct {
    uint16_t year;         // 2000-2099
    uint8_t month;         // 1-12
    uint8_t day;           // 1-31
    uint8_t day_of_week;   // 1-7
    uint8_t hour;          // 0-23
    uint8_t minute;        // 0-59
    uint8_t second;        // 0-59
} ds3231_time_t;

/**
 * @brief 初始化 DS3231
 * @return ESP_OK 成功
 */
esp_err_t ds3231_init(void);

/**
 * @brief 读取 DS3231 当前时间
 * @param time 输出时间结构体
 * @return ESP_OK 成功
 */
esp_err_t ds3231_get_time(ds3231_time_t *time);

/**
 * @brief 设置 DS3231 时间
 * @param time 输入时间结构体
 * @return ESP_OK 成功
 */
esp_err_t ds3231_set_time(const ds3231_time_t *time);

#ifdef __cplusplus
}
#endif

#endif // DS3231_H
