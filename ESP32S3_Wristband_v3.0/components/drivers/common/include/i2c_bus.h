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

// I2C 总线通用配置
#define I2C_MASTER_FREQ_HZ      400000
#define I2C_MASTER_TIMEOUT_MS   100

// Bus 0: 心率 (MAX30102) + 运动 (MPU6050)  — 保持原接线
#define I2C_BUS0_NUM            I2C_NUM_0
#define I2C_BUS0_SDA            GPIO_NUM_8
#define I2C_BUS0_SCL            GPIO_NUM_9

// Bus 1: OLED (SH1106) + RTC (DS3231)  — 新接线
#define I2C_BUS1_NUM            I2C_NUM_1
#define I2C_BUS1_SDA            GPIO_NUM_10
#define I2C_BUS1_SCL            GPIO_NUM_11

// 向后兼容旧宏
#define I2C_MASTER_NUM          I2C_BUS0_NUM
#define I2C_MASTER_SDA_IO       I2C_BUS0_SDA
#define I2C_MASTER_SCL_IO       I2C_BUS0_SCL

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

/**
 * @brief 获取 I2C 总线互斥锁（用于需要多次 I2C 操作保持原子性的场景）
 * @param timeout_ms 等待超时 (ms)
 * @return ESP_OK 成功获取锁
 */
esp_err_t i2c_bus_lock(uint32_t timeout_ms);

/**
 * @brief 释放 I2C 总线互斥锁
 */
void i2c_bus_unlock(void);

/* ========== Bus 1 初始化与扫描 ========== */

/**
 * @brief 初始化 I2C Bus 1 (SH1106 OLED + DS3231 RTC)
 */
esp_err_t i2c_bus1_init(void);

/**
 * @brief 扫描 I2C Bus 1 上的设备
 */
void i2c_bus1_scan(void);

/* ========== 带 port 参数的通用 API ========== */

esp_err_t i2c_bus_write_port(i2c_port_t port, uint8_t dev_addr, uint8_t reg_addr,
                             const uint8_t *data, size_t len);
esp_err_t i2c_bus_read_port(i2c_port_t port, uint8_t dev_addr, uint8_t reg_addr,
                            uint8_t *data, size_t len);
esp_err_t i2c_bus_write_byte_port(i2c_port_t port, uint8_t dev_addr, uint8_t reg_addr,
                                  uint8_t data);
esp_err_t i2c_bus_read_byte_port(i2c_port_t port, uint8_t dev_addr, uint8_t reg_addr,
                                 uint8_t *data);
esp_err_t i2c_bus_lock_port(i2c_port_t port, uint32_t timeout_ms);
void      i2c_bus_unlock_port(i2c_port_t port);

#ifdef __cplusplus
}
#endif

#endif // I2C_BUS_H
