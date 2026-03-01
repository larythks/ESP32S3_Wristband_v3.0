/**
 * @file i2c_bus.c
 * @brief I2C 总线管理封装实现
 */

#include "i2c_bus.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "i2c_bus";
static bool s_i2c_initialized[2] = {false, false};
static SemaphoreHandle_t s_i2c_mutex[2] = {NULL, NULL};

/* ---------- 内部：根据 port 获取互斥锁索引 ---------- */
static inline int port_idx(i2c_port_t port)
{
    return (port == I2C_NUM_1) ? 1 : 0;
}

/* ---------- Bus 0 初始化 (MAX30102 + MPU6050) ---------- */
esp_err_t i2c_bus_init(void)
{
    int idx = port_idx(I2C_BUS0_NUM);
    if (s_i2c_initialized[idx]) {
        ESP_LOGW(TAG, "I2C bus0 already initialized");
        return ESP_OK;
    }

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_BUS0_SDA,
        .scl_io_num = I2C_BUS0_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };

    esp_err_t ret = i2c_param_config(I2C_BUS0_NUM, &conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus0 param config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2c_driver_install(I2C_BUS0_NUM, conf.mode, 0, 0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus0 driver install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_i2c_initialized[idx] = true;

    if (s_i2c_mutex[idx] == NULL) {
        s_i2c_mutex[idx] = xSemaphoreCreateMutex();
        if (s_i2c_mutex[idx] == NULL) {
            ESP_LOGE(TAG, "Failed to create I2C bus0 mutex");
            return ESP_ERR_NO_MEM;
        }
    }

    ESP_LOGI(TAG, "I2C bus0 initialized (SDA=%d, SCL=%d, freq=%dHz)",
             I2C_BUS0_SDA, I2C_BUS0_SCL, I2C_MASTER_FREQ_HZ);
    return ESP_OK;
}

/* ---------- Bus 1 初始化 (SH1106 OLED + DS3231 RTC) ---------- */
esp_err_t i2c_bus1_init(void)
{
    int idx = port_idx(I2C_BUS1_NUM);
    if (s_i2c_initialized[idx]) {
        ESP_LOGW(TAG, "I2C bus1 already initialized");
        return ESP_OK;
    }

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_BUS1_SDA,
        .scl_io_num = I2C_BUS1_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };

    esp_err_t ret = i2c_param_config(I2C_BUS1_NUM, &conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus1 param config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2c_driver_install(I2C_BUS1_NUM, conf.mode, 0, 0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus1 driver install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_i2c_initialized[idx] = true;

    if (s_i2c_mutex[idx] == NULL) {
        s_i2c_mutex[idx] = xSemaphoreCreateMutex();
        if (s_i2c_mutex[idx] == NULL) {
            ESP_LOGE(TAG, "Failed to create I2C bus1 mutex");
            return ESP_ERR_NO_MEM;
        }
    }

    ESP_LOGI(TAG, "I2C bus1 initialized (SDA=%d, SCL=%d, freq=%dHz)",
             I2C_BUS1_SDA, I2C_BUS1_SCL, I2C_MASTER_FREQ_HZ);
    return ESP_OK;
}

/* ---------- 通用扫描（按 port） ---------- */
static void i2c_bus_scan_port(i2c_port_t port, const char *bus_name)
{
    ESP_LOGI(TAG, "Scanning %s ...", bus_name);
    uint8_t device_count = 0;

    for (uint8_t addr = 1; addr < 127; addr++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);

        esp_err_t ret = i2c_master_cmd_begin(port, cmd,
                                              pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
        i2c_cmd_link_delete(cmd);

        if (ret == ESP_OK) {
            const char *name = "Unknown";
            if (addr == 0x69) name = "MPU6050";
            else if (addr == 0x57) name = "MAX30102";
            else if (addr == 0x3C) name = "SH1106";
            else if (addr == 0x68) name = "DS3231";

            ESP_LOGI(TAG, "[%s] device found at 0x%02X (%s)", bus_name, addr, name);
            device_count++;
        }
    }

    ESP_LOGI(TAG, "[%s] scan complete. Found %d device(s)", bus_name, device_count);
}

void i2c_bus_scan(void)
{
    i2c_bus_scan_port(I2C_BUS0_NUM, "Bus0");
}

void i2c_bus1_scan(void)
{
    i2c_bus_scan_port(I2C_BUS1_NUM, "Bus1");
}

/* ========== 带 port 参数的核心读写 ========== */

esp_err_t i2c_bus_write_port(i2c_port_t port, uint8_t dev_addr, uint8_t reg_addr,
                             const uint8_t *data, size_t len)
{
    int idx = port_idx(port);
    if (s_i2c_mutex[idx] &&
        xSemaphoreTake(s_i2c_mutex[idx], pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (dev_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg_addr, true);
    if (data != NULL && len > 0) {
        i2c_master_write(cmd, data, len, true);
    }
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(port, cmd,
                                          pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);

    if (s_i2c_mutex[idx]) {
        xSemaphoreGive(s_i2c_mutex[idx]);
    }
    return ret;
}

esp_err_t i2c_bus_read_port(i2c_port_t port, uint8_t dev_addr, uint8_t reg_addr,
                            uint8_t *data, size_t len)
{
    if (data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    int idx = port_idx(port);
    if (s_i2c_mutex[idx] &&
        xSemaphoreTake(s_i2c_mutex[idx], pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (dev_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg_addr, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (dev_addr << 1) | I2C_MASTER_READ, true);
    if (len > 1) {
        i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(port, cmd,
                                          pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);

    if (s_i2c_mutex[idx]) {
        xSemaphoreGive(s_i2c_mutex[idx]);
    }
    return ret;
}

esp_err_t i2c_bus_write_byte_port(i2c_port_t port, uint8_t dev_addr,
                                  uint8_t reg_addr, uint8_t data)
{
    return i2c_bus_write_port(port, dev_addr, reg_addr, &data, 1);
}

esp_err_t i2c_bus_read_byte_port(i2c_port_t port, uint8_t dev_addr,
                                 uint8_t reg_addr, uint8_t *data)
{
    return i2c_bus_read_port(port, dev_addr, reg_addr, data, 1);
}

esp_err_t i2c_bus_lock_port(i2c_port_t port, uint32_t timeout_ms)
{
    int idx = port_idx(port);
    if (s_i2c_mutex[idx] == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_i2c_mutex[idx], pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

void i2c_bus_unlock_port(i2c_port_t port)
{
    int idx = port_idx(port);
    if (s_i2c_mutex[idx] != NULL) {
        xSemaphoreGive(s_i2c_mutex[idx]);
    }
}

/* ========== 向后兼容包装 (默认 Bus 0) ========== */

esp_err_t i2c_bus_write(uint8_t dev_addr, uint8_t reg_addr, const uint8_t *data, size_t len)
{
    return i2c_bus_write_port(I2C_BUS0_NUM, dev_addr, reg_addr, data, len);
}

esp_err_t i2c_bus_read(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, size_t len)
{
    return i2c_bus_read_port(I2C_BUS0_NUM, dev_addr, reg_addr, data, len);
}

esp_err_t i2c_bus_write_byte(uint8_t dev_addr, uint8_t reg_addr, uint8_t data)
{
    return i2c_bus_write_byte_port(I2C_BUS0_NUM, dev_addr, reg_addr, data);
}

esp_err_t i2c_bus_read_byte(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data)
{
    return i2c_bus_read_byte_port(I2C_BUS0_NUM, dev_addr, reg_addr, data);
}

esp_err_t i2c_bus_lock(uint32_t timeout_ms)
{
    return i2c_bus_lock_port(I2C_BUS0_NUM, timeout_ms);
}

void i2c_bus_unlock(void)
{
    i2c_bus_unlock_port(I2C_BUS0_NUM);
}
