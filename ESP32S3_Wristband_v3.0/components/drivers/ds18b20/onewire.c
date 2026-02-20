/**
 * @file onewire.c
 * @brief 1-Wire 协议驱动实现
 */

#include "onewire.h"
#include "esp_rom_sys.h"
#include "esp_attr.h"

// 1-Wire 时序常量 (微秒)
#define OW_DELAY_A  6
#define OW_DELAY_B  64
#define OW_DELAY_C  60
#define OW_DELAY_D  10
#define OW_DELAY_E  9
#define OW_DELAY_F  55
#define OW_DELAY_G  0
#define OW_DELAY_H  480
#define OW_DELAY_I  70
#define OW_DELAY_J  410

/**
 * @brief 设置引脚为输出模式并输出低电平
 */
static inline void IRAM_ATTR onewire_set_low(onewire_bus_t *bus)
{
    gpio_set_direction(bus->pin, GPIO_MODE_OUTPUT);
    gpio_set_level(bus->pin, 0);
}

/**
 * @brief 释放总线 (设置为输入模式，由上拉电阻拉高)
 */
static inline void IRAM_ATTR onewire_release(onewire_bus_t *bus)
{
    gpio_set_direction(bus->pin, GPIO_MODE_INPUT);
}

/**
 * @brief 读取总线电平
 */
static inline int IRAM_ATTR onewire_read_level(onewire_bus_t *bus)
{
    return gpio_get_level(bus->pin);
}

esp_err_t onewire_init(onewire_bus_t *bus, gpio_num_t pin)
{
    bus->pin = pin;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    return gpio_config(&io_conf);
}

bool IRAM_ATTR onewire_reset(onewire_bus_t *bus)
{
    int presence;

    onewire_set_low(bus);
    esp_rom_delay_us(OW_DELAY_H);
    onewire_release(bus);
    esp_rom_delay_us(OW_DELAY_I);

    presence = !onewire_read_level(bus);
    esp_rom_delay_us(OW_DELAY_J);

    return presence;
}

void IRAM_ATTR onewire_write_bit(onewire_bus_t *bus, uint8_t bit)
{
    if (bit & 1) {
        onewire_set_low(bus);
        esp_rom_delay_us(OW_DELAY_A);
        onewire_release(bus);
        esp_rom_delay_us(OW_DELAY_B);
    } else {
        onewire_set_low(bus);
        esp_rom_delay_us(OW_DELAY_C);
        onewire_release(bus);
        esp_rom_delay_us(OW_DELAY_D);
    }
}

uint8_t IRAM_ATTR onewire_read_bit(onewire_bus_t *bus)
{
    uint8_t bit;

    onewire_set_low(bus);
    esp_rom_delay_us(OW_DELAY_A);
    onewire_release(bus);
    esp_rom_delay_us(OW_DELAY_E);

    bit = onewire_read_level(bus);
    esp_rom_delay_us(OW_DELAY_F);

    return bit;
}

void IRAM_ATTR onewire_write_byte(onewire_bus_t *bus, uint8_t data)
{
    for (int i = 0; i < 8; i++) {
        onewire_write_bit(bus, data & 0x01);
        data >>= 1;
    }
}

uint8_t IRAM_ATTR onewire_read_byte(onewire_bus_t *bus)
{
    uint8_t data = 0;
    for (int i = 0; i < 8; i++) {
        data >>= 1;
        if (onewire_read_bit(bus)) {
            data |= 0x80;
        }
    }
    return data;
}
