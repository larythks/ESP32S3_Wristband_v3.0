/**
 * @file max30102.h
 * @brief MAX30102 心率血氧传感器驱动
 */

#ifndef MAX30102_H
#define MAX30102_H

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// MAX30102 I2C 地址
#define MAX30102_I2C_ADDR           0x57

// MAX30102 寄存器地址
#define MAX30102_REG_INT_STATUS1    0x00
#define MAX30102_REG_INT_STATUS2    0x01
#define MAX30102_REG_INT_ENABLE1    0x02
#define MAX30102_REG_INT_ENABLE2    0x03
#define MAX30102_REG_FIFO_WR_PTR    0x04
#define MAX30102_REG_OVF_COUNTER    0x05
#define MAX30102_REG_FIFO_RD_PTR    0x06
#define MAX30102_REG_FIFO_DATA      0x07
#define MAX30102_REG_FIFO_CONFIG    0x08
#define MAX30102_REG_MODE_CONFIG    0x09
#define MAX30102_REG_SPO2_CONFIG    0x0A
#define MAX30102_REG_LED1_PA        0x0C
#define MAX30102_REG_LED2_PA        0x0D
#define MAX30102_REG_PILOT_PA       0x10
#define MAX30102_REG_MULTI_LED1     0x11
#define MAX30102_REG_MULTI_LED2     0x12
#define MAX30102_REG_TEMP_INT       0x1F
#define MAX30102_REG_TEMP_FRAC      0x20
#define MAX30102_REG_TEMP_CONFIG    0x21
#define MAX30102_REG_REV_ID         0xFE
#define MAX30102_REG_PART_ID        0xFF

// Part ID 期望值
#define MAX30102_PART_ID_VALUE      0x15

// 工作模式
#define MAX30102_MODE_HR            0x02    // 仅心率模式
#define MAX30102_MODE_SPO2          0x03    // SpO2 模式
#define MAX30102_MODE_MULTI_LED     0x07    // 多 LED 模式

// FIFO 配置
#define MAX30102_FIFO_DEPTH         32

/**
 * @brief MAX30102 配置结构体
 */
typedef struct {
    uint8_t fifo_sample_avg;    // FIFO 采样平均 (0-7: 1,2,4,8,16,32 samples)
    uint8_t fifo_rollover;      // FIFO 溢出时是否覆盖
    uint8_t fifo_almost_full;   // FIFO 几乎满阈值
    uint8_t mode;               // 工作模式
    uint8_t adc_range;          // ADC 范围 (0-3: 2048,4096,8192,16384 nA)
    uint8_t sample_rate;        // 采样率 (0-7: 50,100,200,400,800,1000,1600,3200 Hz)
    uint8_t pulse_width;        // LED 脉冲宽度 (0-3: 69,118,215,411 us)
    uint8_t led1_current;       // LED1 (红光) 电流 (0-255: 0-51mA)
    uint8_t led2_current;       // LED2 (红外) 电流 (0-255: 0-51mA)
} max30102_config_t;

/**
 * @brief 初始化 MAX30102
 * @return ESP_OK 成功
 */
esp_err_t max30102_init(void);

/**
 * @brief 使用自定义配置初始化 MAX30102
 * @param config 配置参数
 * @return ESP_OK 成功
 */
esp_err_t max30102_init_with_config(const max30102_config_t *config);

/**
 * @brief 软复位 MAX30102
 * @return ESP_OK 成功
 */
esp_err_t max30102_reset(void);

/**
 * @brief 读取 Part ID
 * @param part_id 输出 Part ID
 * @return ESP_OK 成功
 */
esp_err_t max30102_read_part_id(uint8_t *part_id);

/**
 * @brief 从 FIFO 读取数据
 * @param red RED 通道数据输出
 * @param ir IR 通道数据输出
 * @param count 输入: 缓冲区大小; 输出: 实际读取的样本数
 * @return ESP_OK 成功
 */
esp_err_t max30102_read_fifo(uint32_t *red, uint32_t *ir, uint8_t *count);

/**
 * @brief 获取 FIFO 中可用的样本数
 * @param count 输出样本数
 * @return ESP_OK 成功
 */
esp_err_t max30102_get_fifo_count(uint8_t *count);

/**
 * @brief 清空 FIFO
 * @return ESP_OK 成功
 */
esp_err_t max30102_clear_fifo(void);

/**
 * @brief 关闭 MAX30102 (进入低功耗模式)
 * @return ESP_OK 成功
 */
esp_err_t max30102_shutdown(void);

/**
 * @brief 唤醒 MAX30102
 * @return ESP_OK 成功
 */
esp_err_t max30102_wakeup(void);

/**
 * @brief 读取芯片温度
 * @param temp 输出温度值 (摄氏度)
 * @return ESP_OK 成功
 */
esp_err_t max30102_read_temperature(float *temp);

#ifdef __cplusplus
}
#endif

#endif // MAX30102_H
