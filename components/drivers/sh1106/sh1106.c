/**
 * @file sh1106.c
 * @brief SH1106 OLED 显示驱动实现
 */

#include "sh1106.h"
#include "font.h"
#include "font_large.h"
#include "font_cn.h"
#include "i2c_bus.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "sh1106";

// 显示缓冲区
static uint8_t s_buffer[SH1106_WIDTH * SH1106_PAGES];

// SH1106 命令定义
#define SH1106_CMD_DISPLAY_OFF          0xAE
#define SH1106_CMD_DISPLAY_ON           0xAF
#define SH1106_CMD_SET_CONTRAST         0x81
#define SH1106_CMD_SET_SEGMENT_REMAP    0xA1
#define SH1106_CMD_SET_COM_SCAN_DEC     0xC8
#define SH1106_CMD_SET_DISPLAY_OFFSET   0xD3
#define SH1106_CMD_SET_COM_PINS         0xDA
#define SH1106_CMD_SET_CLOCK_DIV        0xD5
#define SH1106_CMD_SET_PRECHARGE        0xD9
#define SH1106_CMD_SET_VCOM_DESELECT    0xDB
#define SH1106_CMD_SET_MULTIPLEX        0xA8
#define SH1106_CMD_SET_START_LINE       0x40
#define SH1106_CMD_SET_PAGE_ADDR        0xB0
#define SH1106_CMD_SET_LOW_COLUMN       0x00
#define SH1106_CMD_SET_HIGH_COLUMN      0x10

static void sh1106_draw_filled_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color);
static void sh1106_draw_large_char(int16_t x, int16_t y, char c, uint8_t color);

// 写命令（内部无锁版本，调用方需自行持有 I2C 总线锁）
static esp_err_t sh1106_write_cmd_nolock(uint8_t cmd)
{
    uint8_t data[2] = {0x00, cmd};  // Co=0, D/C#=0 (command)
    i2c_cmd_handle_t handle = i2c_cmd_link_create();
    i2c_master_start(handle);
    i2c_master_write_byte(handle, (SH1106_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(handle, data, 2, true);
    i2c_master_stop(handle);
    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, handle, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(handle);
    return ret;
}

// 写命令（加锁版本，供单条命令使用）
static esp_err_t sh1106_write_cmd(uint8_t cmd)
{
    i2c_bus_lock(200);
    esp_err_t ret = sh1106_write_cmd_nolock(cmd);
    i2c_bus_unlock();
    return ret;
}

esp_err_t sh1106_init(void)
{
    esp_err_t ret;

    // 关闭显示
    ret = sh1106_write_cmd(SH1106_CMD_DISPLAY_OFF);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send display off command");
        return ret;
    }

    // 设置时钟分频
    sh1106_write_cmd(SH1106_CMD_SET_CLOCK_DIV);
    sh1106_write_cmd(0x80);

    // 设置多路复用比
    sh1106_write_cmd(SH1106_CMD_SET_MULTIPLEX);
    sh1106_write_cmd(0x3F);  // 64-1

    // 设置显示偏移
    sh1106_write_cmd(SH1106_CMD_SET_DISPLAY_OFFSET);
    sh1106_write_cmd(0x00);

    // 设置起始行
    sh1106_write_cmd(SH1106_CMD_SET_START_LINE | 0x00);

    // 设置段重映射
    sh1106_write_cmd(SH1106_CMD_SET_SEGMENT_REMAP);

    // 设置COM扫描方向
    sh1106_write_cmd(SH1106_CMD_SET_COM_SCAN_DEC);

    // 设置COM引脚配置
    sh1106_write_cmd(SH1106_CMD_SET_COM_PINS);
    sh1106_write_cmd(0x12);

    // 设置对比度
    sh1106_write_cmd(SH1106_CMD_SET_CONTRAST);
    sh1106_write_cmd(0xCF);

    // 设置预充电周期
    sh1106_write_cmd(SH1106_CMD_SET_PRECHARGE);
    sh1106_write_cmd(0xF1);

    // 设置VCOMH电压
    sh1106_write_cmd(SH1106_CMD_SET_VCOM_DESELECT);
    sh1106_write_cmd(0x40);

    // 正常显示
    sh1106_write_cmd(0xA4);  // 输出跟随RAM
    sh1106_write_cmd(0xA6);  // 正常显示（非反转）

    // 清空缓冲区并更新
    sh1106_clear();
    sh1106_update();

    // 开启显示
    sh1106_write_cmd(SH1106_CMD_DISPLAY_ON);

    ESP_LOGI(TAG, "SH1106 OLED initialized");
    return ESP_OK;
}

void sh1106_clear(void)
{
    memset(s_buffer, 0, sizeof(s_buffer));
}

void sh1106_fill(uint8_t color)
{
    memset(s_buffer, color ? 0xFF : 0x00, sizeof(s_buffer));
}

void sh1106_update(void)
{
    for (uint8_t page = 0; page < SH1106_PAGES; page++) {
        if (i2c_bus_lock(200) != ESP_OK) {
            break;
        }

        sh1106_write_cmd_nolock(SH1106_CMD_SET_PAGE_ADDR | page);
        sh1106_write_cmd_nolock(SH1106_CMD_SET_LOW_COLUMN | 0x02);  // SH1106偏移2列
        sh1106_write_cmd_nolock(SH1106_CMD_SET_HIGH_COLUMN | 0x00);

        // 写入一页数据
        i2c_cmd_handle_t handle = i2c_cmd_link_create();
        i2c_master_start(handle);
        i2c_master_write_byte(handle, (SH1106_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
        i2c_master_write_byte(handle, 0x40, true);  // Co=0, D/C#=1 (data)
        i2c_master_write(handle, &s_buffer[page * SH1106_WIDTH], SH1106_WIDTH, true);
        i2c_master_stop(handle);
        i2c_master_cmd_begin(I2C_NUM_0, handle, pdMS_TO_TICKS(100));
        i2c_cmd_link_delete(handle);

        i2c_bus_unlock();
    }
}

void sh1106_draw_pixel(int16_t x, int16_t y, uint8_t color)
{
    if (x < 0 || x >= SH1106_WIDTH || y < 0 || y >= SH1106_HEIGHT) {
        return;
    }

    uint16_t idx = x + (y / 8) * SH1106_WIDTH;
    if (color) {
        s_buffer[idx] |= (1 << (y % 8));
    } else {
        s_buffer[idx] &= ~(1 << (y % 8));
    }
}

static void sh1106_draw_filled_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color)
{
    for (int16_t yy = y; yy < y + h; yy++) {
        for (int16_t xx = x; xx < x + w; xx++) {
            sh1106_draw_pixel(xx, yy, color);
        }
    }
}

void sh1106_draw_char(int16_t x, int16_t y, char c, uint8_t color)
{
    if (c < 32 || c > 126) {
        c = '?';
    }

    for (uint8_t i = 0; i < 5; i++) {
        uint8_t line = font5x7[c - 32][i];
        for (uint8_t j = 0; j < 7; j++) {
            if (line & (1 << j)) {
                sh1106_draw_pixel(x + i, y + j, color);
            } else {
                sh1106_draw_pixel(x + i, y + j, !color);
            }
        }
    }
    // 清除字符间 1 像素间隔，防止残影
    for (uint8_t j = 0; j < 7; j++) {
        sh1106_draw_pixel(x + 5, y + j, !color);
    }
}

void sh1106_draw_string(int16_t x, int16_t y, const char *str, uint8_t color)
{
    while (*str) {
        sh1106_draw_char(x, y, *str, color);
        x += 6;  // 5像素宽 + 1像素间距
        str++;
    }
}

static void sh1106_draw_large_char(int16_t x, int16_t y, char c, uint8_t color)
{
    // 先清空字符区域，避免局部更新残影
    sh1106_draw_filled_rect(x, y, FONT_LARGE_WIDTH, FONT_LARGE_HEIGHT, !color);

    if (c == ':') {
        sh1106_draw_filled_rect(x + 6, y + 7, 3, 3, color);
        sh1106_draw_filled_rect(x + 6, y + 15, 3, 3, color);
        return;
    }

    if (c < '0' || c > '9') {
        return;
    }

    uint8_t seg = font_large_digit_segments[c - '0'];

    if (seg & LARGE_SEG_A) sh1106_draw_filled_rect(x + 2, y + 0, 12, 3, color);
    if (seg & LARGE_SEG_B) sh1106_draw_filled_rect(x + 13, y + 2, 3, 9, color);
    if (seg & LARGE_SEG_C) sh1106_draw_filled_rect(x + 13, y + 13, 3, 9, color);
    if (seg & LARGE_SEG_D) sh1106_draw_filled_rect(x + 2, y + 21, 12, 3, color);
    if (seg & LARGE_SEG_E) sh1106_draw_filled_rect(x + 0, y + 13, 3, 9, color);
    if (seg & LARGE_SEG_F) sh1106_draw_filled_rect(x + 0, y + 2, 3, 9, color);
    if (seg & LARGE_SEG_G) sh1106_draw_filled_rect(x + 2, y + 10, 12, 3, color);
}

void sh1106_draw_string_large(int16_t x, int16_t y, const char *str, uint8_t color)
{
    while (*str) {
        sh1106_draw_large_char(x, y, *str, color);
        x += FONT_LARGE_WIDTH + FONT_LARGE_CHAR_SPACING;
        str++;
    }
}

void sh1106_draw_chinese(int16_t x, int16_t y, uint8_t index, uint8_t color)
{
    if (index >= FONT_CN_COUNT) {
        return;
    }

    for (uint8_t row = 0; row < FONT_CN_HEIGHT; row++) {
        uint16_t bits = font_cn_12x12[index][row];
        for (uint8_t col = 0; col < FONT_CN_WIDTH; col++) {
            bool on = (bits & (1U << (FONT_CN_WIDTH - 1 - col))) != 0;
            sh1106_draw_pixel(x + col, y + row, on ? color : !color);
        }
        // 清除字符间距，避免残影
        sh1106_draw_pixel(x + FONT_CN_WIDTH, y + row, !color);
    }
}

void sh1106_display_on(bool on)
{
    sh1106_write_cmd(on ? SH1106_CMD_DISPLAY_ON : SH1106_CMD_DISPLAY_OFF);
}

void sh1106_set_contrast(uint8_t contrast)
{
    sh1106_write_cmd(SH1106_CMD_SET_CONTRAST);
    sh1106_write_cmd(contrast);
}
