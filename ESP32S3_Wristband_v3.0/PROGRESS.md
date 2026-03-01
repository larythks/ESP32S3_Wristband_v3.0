# PROGRESS.md - 任务进度追踪

---

## 2026-03-01：双 I2C 总线拆分（消除 OLED 刷屏对传感器采样的阻塞）

**任务简述**：
将 4 个 I2C 设备从单总线（I2C_NUM_0）拆分为双总线，利用 ESP32-S3 的第二路 I2C 控制器。

**最终分配**：
- Bus 0 (I2C_NUM_0, GPIO 8/9)：MAX30102 + MPU6050 — 保持原接线
- Bus 1 (I2C_NUM_1, GPIO 10/11)：SH1106 OLED + DS3231 RTC — 改接新线

**对应模块**：硬件驱动层 / I2C 总线管理

**修改/新增文件**：
- `components/drivers/common/include/i2c_bus.h` — 新增 Bus 1 定义 + 带 port 参数的 API
- `components/drivers/common/i2c_bus.c` — 双总线初始化 + 双互斥锁 + port API 实现
- `components/drivers/sh1106/sh1106.c` — 切换到 Bus 1（I2C_BUS1_NUM + lock_port/unlock_port）
- `components/drivers/ds3231/ds3231.c` — 切换到 Bus 1（_port API）
- `components/drivers/max30102/max30102.c` — 使用 Bus 0 显式 port API
- `components/drivers/mpu6050/mpu6050.c` — 使用 Bus 0 显式 port API
- `main/main.c` — 新增 Bus 1 初始化 + 扫描

**验收状态**：待验收（编译通过，需硬件接线验证）

**遗留问题**：
- 需要将 SH1106 OLED 和 DS3231 RTC 的 SDA/SCL 硬件接线从 GPIO 8/9 改接到 GPIO 10/11
- 新 GPIO 需确认有合适的上拉电阻（内部上拉已在代码中启用，但外部上拉效果更好）
