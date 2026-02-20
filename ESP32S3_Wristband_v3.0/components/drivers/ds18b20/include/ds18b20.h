/**
 * @file ds18b20.h
 * @brief DS18B20 温度传感器驱动
 */

#ifndef DS18B20_H
#define DS18B20_H

#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

// DS18B20 默认引脚
#define DS18B20_DEFAULT_PIN     GPIO_NUM_4

// DS18B20 ROM 命令
#define DS18B20_CMD_SKIP_ROM    0xCC
#define DS18B20_CMD_READ_ROM    0x33

// DS18B20 功能命令
#define DS18B20_CMD_CONVERT_T   0x44
#define DS18B20_CMD_READ_SCRATCH    0xBE
#define DS18B20_CMD_WRITE_SCRATCH   0x4E

/**
 * @brief 初始化 DS18B20
 * @param pin GPIO 引脚
 * @return ESP_OK 成功
 */
esp_err_t ds18b20_init(gpio_num_t pin);

/**
 * @brief 读取温度
 * @param temp_c 输出温度值 (摄氏度)
 * @return ESP_OK 成功
 */
esp_err_t ds18b20_read_temp(float *temp_c);

/**
 * @brief 启动温度转换
 * @return ESP_OK 成功
 */
esp_err_t ds18b20_start_convert(void);

/**
 * @brief 读取暂存器中的温度值（需先调用 ds18b20_start_convert 并等待转换完成）
 * @param temp_c 输出温度值 (摄氏度)
 * @return ESP_OK 成功
 */
esp_err_t ds18b20_read_scratchpad(float *temp_c);

/**
 * @brief 检查设备是否存在
 * @return true 存在, false 不存在
 */
bool ds18b20_is_present(void);

#ifdef __cplusplus
}
#endif

#endif // DS18B20_H
