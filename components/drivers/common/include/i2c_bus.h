/**
 * @file i2c_bus.h
 * @brief I2C 总线管理封装
 */

#ifndef I2C_BUS_H
#define I2C_BUS_H

#include "esp_err.h"
#include "driver/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

// I2C 总线配置
#define I2C_MASTER_NUM          I2C_NUM_0
#define I2C_MASTER_SDA_IO       GPIO_NUM_8
#define I2C_MASTER_SCL_IO       GPIO_NUM_9
#define I2C_MASTER_FREQ_HZ      400000
#define I2C_MASTER_TIMEOUT_MS   100

/**
 * @brief 初始化 I2C 主机总线
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t i2c_bus_init(void);

/**
 * @brief 扫描 I2C 总线上的设备
 * @note 扫描结果通过日志输出
 */
void i2c_bus_scan(void);

/**
 * @brief 向 I2C 设备写入数据
 * @param dev_addr 设备地址 (7位)
 * @param reg_addr 寄存器地址
 * @param data 要写入的数据
 * @param len 数据长度
 * @return ESP_OK 成功
 */
esp_err_t i2c_bus_write(uint8_t dev_addr, uint8_t reg_addr, const uint8_t *data, size_t len);

/**
 * @brief 从 I2C 设备读取数据
 * @param dev_addr 设备地址 (7位)
 * @param reg_addr 寄存器地址
 * @param data 读取数据的缓冲区
 * @param len 要读取的长度
 * @return ESP_OK 成功
 */
esp_err_t i2c_bus_read(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, size_t len);

/**
 * @brief 向 I2C 设备写入单个字节
 * @param dev_addr 设备地址 (7位)
 * @param reg_addr 寄存器地址
 * @param data 要写入的字节
 * @return ESP_OK 成功
 */
esp_err_t i2c_bus_write_byte(uint8_t dev_addr, uint8_t reg_addr, uint8_t data);

/**
 * @brief 从 I2C 设备读取单个字节
 * @param dev_addr 设备地址 (7位)
 * @param reg_addr 寄存器地址
 * @param data 读取数据的指针
 * @return ESP_OK 成功
 */
esp_err_t i2c_bus_read_byte(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data);

#ifdef __cplusplus
}
#endif

#endif // I2C_BUS_H
