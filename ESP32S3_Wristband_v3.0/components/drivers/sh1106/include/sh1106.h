/**
 * @file sh1106.h
 * @brief SH1106 OLED 显示驱动
 */

#ifndef SH1106_H
#define SH1106_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// SH1106 I2C 地址
#define SH1106_I2C_ADDR     0x3C

// 显示尺寸
#define SH1106_WIDTH        128
#define SH1106_HEIGHT       64
#define SH1106_PAGES        (SH1106_HEIGHT / 8)

/**
 * @brief 初始化 SH1106 OLED
 * @return ESP_OK 成功
 */
esp_err_t sh1106_init(void);

/**
 * @brief 清空显示缓冲区
 */
void sh1106_clear(void);

/**
 * @brief 填充显示缓冲区
 * @param color 0=黑色, 1=白色
 */
void sh1106_fill(uint8_t color);

/**
 * @brief 更新显示（将缓冲区写入OLED）
 */
void sh1106_update(void);

/**
 * @brief 设置像素点
 * @param x X坐标 (0-127)
 * @param y Y坐标 (0-63)
 * @param color 0=黑色, 1=白色
 */
void sh1106_draw_pixel(int16_t x, int16_t y, uint8_t color);

/**
 * @brief 绘制字符
 * @param x X坐标
 * @param y Y坐标
 * @param c 字符
 * @param color 颜色
 */
void sh1106_draw_char(int16_t x, int16_t y, char c, uint8_t color);

/**
 * @brief 绘制字符串
 * @param x X坐标
 * @param y Y坐标
 * @param str 字符串
 * @param color 颜色
 */
void sh1106_draw_string(int16_t x, int16_t y, const char *str, uint8_t color);

/**
 * @brief 绘制 16x24 大号字符串（支持 0-9 和 ':'）
 * @param x X坐标
 * @param y Y坐标
 * @param str 字符串
 * @param color 颜色
 */
void sh1106_draw_string_large(int16_t x, int16_t y, const char *str, uint8_t color);

/**
 * @brief 绘制 12x12 中文字符
 * @param x X坐标
 * @param y Y坐标
 * @param index 字模索引（0-8: 星/期/一/二/三/四/五/六/日）
 * @param color 颜色
 */
void sh1106_draw_chinese(int16_t x, int16_t y, uint8_t index, uint8_t color);

/**
 * @brief 设置显示开关
 * @param on true=开启, false=关闭
 */
void sh1106_display_on(bool on);

/**
 * @brief 设置对比度
 * @param contrast 对比度值 (0-255)
 */
void sh1106_set_contrast(uint8_t contrast);

#ifdef __cplusplus
}
#endif

#endif // SH1106_H
