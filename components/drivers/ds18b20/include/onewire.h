/**
 * @file onewire.h
 * @brief 1-Wire 协议驱动
 */

#ifndef ONEWIRE_H
#define ONEWIRE_H

#include "esp_err.h"
#include "driver/gpio.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 1-Wire 总线句柄
 */
typedef struct {
    gpio_num_t pin;
} onewire_bus_t;

/**
 * @brief 初始化 1-Wire 总线
 * @param bus 总线句柄
 * @param pin GPIO 引脚
 * @return ESP_OK 成功
 */
esp_err_t onewire_init(onewire_bus_t *bus, gpio_num_t pin);

/**
 * @brief 复位 1-Wire 总线并检测设备
 * @param bus 总线句柄
 * @return true 检测到设备, false 无设备
 */
bool onewire_reset(onewire_bus_t *bus);

/**
 * @brief 写入一个字节
 * @param bus 总线句柄
 * @param data 要写入的字节
 */
void onewire_write_byte(onewire_bus_t *bus, uint8_t data);

/**
 * @brief 读取一个字节
 * @param bus 总线句柄
 * @return 读取的字节
 */
uint8_t onewire_read_byte(onewire_bus_t *bus);

/**
 * @brief 写入一个位
 * @param bus 总线句柄
 * @param bit 要写入的位 (0 或 1)
 */
void onewire_write_bit(onewire_bus_t *bus, uint8_t bit);

/**
 * @brief 读取一个位
 * @param bus 总线句柄
 * @return 读取的位 (0 或 1)
 */
uint8_t onewire_read_bit(onewire_bus_t *bus);

#ifdef __cplusplus
}
#endif

#endif // ONEWIRE_H
