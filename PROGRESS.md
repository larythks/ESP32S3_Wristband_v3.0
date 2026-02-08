# 项目进度记录

本文件记录 ESP32-S3 智能陪护手环项目的实际开发进度。

## 进度总览

| 周次 | 迭代 | 状态 | 完成日期 |
|------|------|------|----------|
| Week 1 | 迭代 1.1: 项目结构搭建 + I2C 总线验证 | ✅ 已完成 | 2026-02-08 |
| Week 1 | 迭代 1.2: MPU6050 驱动 + OLED 驱动 | ✅ 已完成 | 2026-02-08 |
| Week 1 | 迭代 1.3: MAX30102 驱动 + DS18B20 驱动 | ✅ 已完成 | 2026-02-08 |
| Week 1 | 迭代 1.4: 按键驱动 + 基础 UI 框架 | ✅ 已完成 | 2026-02-08 |
| Week 2 | 迭代 2.1: 事件总线 + 传感器采样服务 | ✅ 已完成 | 2026-02-08 |
| Week 2 | 迭代 2.2: 健康监测服务 + 计步算法 | ✅ 已完成 | 2026-02-08 |
| Week 2 | 迭代 2.3: 跌倒检测算法 | ✅ 已完成 | 2026-02-08 |
| Week 2 | 迭代 2.4: BLE GATT 服务 + 数据上报 | 🔲 待开始 | - |
| Week 2 | 迭代 2.5: BLE 安全子任务 + 最小端到端切片 | 🔲 待开始 | - |
| Week 3 | 迭代 3.1: 报警状态机 + 声光控制 | 🔲 待开始 | - |
| Week 3 | 迭代 3.2: I2S 音频播放 | 🔲 待开始 | - |
| Week 3 | 迭代 3.3: ESP-SR 语音识别集成 | 🔲 待开始 | - |
| Week 3 | 迭代 3.4: 完整报警流程联调 | 🔲 待开始 | - |
| Week 4 | 迭代 4.1: Flutter 项目搭建 + BLE 连接 | 🔲 待开始 | - |
| Week 4 | 迭代 4.2: 数据展示 UI + 报警 UI | 🔲 待开始 | - |
| Week 4 | 迭代 4.3: MQTT 网关 + EMQX Cloud 部署 | 🔲 待开始 | - |
| Week 4 | 迭代 4.4: 端到端联调 + 问题修复 | 🔲 待开始 | - |

## 详细记录

---

### 2026-02-08 - 项目进度追踪机制建立

- **迭代**: 项目基础设施
- **状态**: ✅ 已完成
- **修改文件**:
  - CLAUDE.md (添加任务完成与进度追踪规则)
  - PROGRESS.md (新建)
- **验收状态**: 待验收
- **备注**: 建立了项目进度追踪机制，要求 AI 在完成任务后自动记录进度

---

### 2026-02-08 - 迭代 1.1: 项目结构搭建 + I2C 总线验证

- **迭代**: Week 1 - 迭代 1.1
- **状态**: ✅ 已完成
- **修改文件**:
  - components/common/i2c_bus.h (I2C 总线头文件)
  - components/common/i2c_bus.c (I2C 总线实现)
  - components/common/CMakeLists.txt
  - main/main.c (入口程序)
  - main/CMakeLists.txt
- **验收状态**: 待验收
- **备注**: 已实现 I2C 总线初始化、设备扫描、读写功能

---

### 2026-02-08 - 迭代 1.2: MPU6050 驱动 + OLED 驱动

- **迭代**: Week 1 - 迭代 1.2
- **状态**: ✅ 已完成
- **修改文件**:
  - components/drivers/mpu6050/include/mpu6050.h
  - components/drivers/mpu6050/mpu6050.c
  - components/drivers/mpu6050/CMakeLists.txt
  - components/drivers/sh1106/include/sh1106.h
  - components/drivers/sh1106/include/font.h (5x7 ASCII 字体)
  - components/drivers/sh1106/sh1106.c
  - components/drivers/sh1106/CMakeLists.txt
  - main/main.c (集成测试代码)
  - main/CMakeLists.txt (添加依赖)
- **验收状态**: 待验收
- **验收清单**:
  - [ ] MPU6050 WHO_AM_I 返回 0x68
  - [ ] 加速度数据随晃动变化
  - [ ] OLED 点亮并显示文字
- **备注**: 实现了 MPU6050 加速度/陀螺仪读取，SH1106 OLED 显示驱动

---

### 2026-02-08 - 迭代 1.3: MAX30102 驱动 + DS18B20 驱动

- **迭代**: Week 1 - 迭代 1.3
- **状态**: ✅ 已完成
- **修改文件**:
  - components/drivers/max30102/include/max30102.h
  - components/drivers/max30102/max30102.c
  - components/drivers/max30102/CMakeLists.txt
  - components/drivers/ds18b20/include/onewire.h
  - components/drivers/ds18b20/onewire.c
  - components/drivers/ds18b20/include/ds18b20.h
  - components/drivers/ds18b20/ds18b20.c
  - components/drivers/ds18b20/CMakeLists.txt
  - main/main.c (集成测试代码)
  - main/CMakeLists.txt (添加依赖)
- **验收状态**: 待验收
- **验收清单**:
  - [ ] MAX30102 Part ID 返回 0x15
  - [ ] 手指触碰时 RED/IR 数据变化
  - [ ] DS18B20 温度读数在 20-40°C 范围内
- **备注**: 实现了 MAX30102 心率血氧传感器驱动和 DS18B20 体温传感器驱动（含 1-Wire 协议）

---

### 2026-02-08 - 迭代 1.4: 按键驱动 + 基础 UI 框架

- **迭代**: Week 1 - 迭代 1.4
- **状态**: ✅ 已完成
- **修改文件**:
  - components/drivers/button/include/button.h (按键驱动头文件)
  - components/drivers/button/button.c (按键驱动实现)
  - components/drivers/CMakeLists.txt (添加 button)
  - components/ui_manager/include/ui_manager.h (UI 管理器头文件)
  - components/ui_manager/ui_manager.c (UI 管理器实现)
  - components/ui_manager/CMakeLists.txt
  - main/main.c (集成按键和 UI)
  - main/CMakeLists.txt (添加依赖)
- **验收状态**: 待验收
- **验收清单**:
  - [ ] SW2 短按切换页面
  - [ ] SW2 长按可启动手动测量
  - [ ] SW2 再次长按可中断手动测量
  - [ ] SW1 按下有响应
  - [ ] 无按键抖动误触发
- **备注**:
  - 按键驱动使用 FreeRTOS 定时器实现消抖和长按检测
  - UI 管理器支持 4 个页面：主页、心率页、步数页、手动测量页
  - 手动测量模式可通过 SW2 长按进入/退出

---

### 2026-02-08 - Bug 修复: Timer Service 栈溢出

- **迭代**: Bug 修复
- **状态**: ✅ 已完成
- **问题描述**: 长按按钮进入手动测量模式时，系统报错 "A stack overflow in task Tmr Svc has been detected" 并重启
- **根本原因**: FreeRTOS Timer Service 任务栈大小默认为 2048 字节，按键定时器回调中调用 UI 绘制函数链（ui_update → sh1106_clear → I2C 操作）导致栈溢出
- **解决方案**: 将 `CONFIG_FREERTOS_TIMER_TASK_STACK_DEPTH` 从 2048 增大到 4096
- **修改文件**:
  - sdkconfig (修改 Timer Service 栈大小)
  - CLAUDE.md (添加规则 8: 定时器回调中避免执行重量级操作)
- **验收状态**: 待验收
- **验收清单**:
  - [ ] 长按 SW2 进入手动测量模式不再崩溃
  - [ ] 系统运行稳定

---

### 2026-02-08 - 迭代 2.1: 事件总线 + 传感器采样服务

- **迭代**: Week 2 - 迭代 2.1
- **状态**: ✅ 已完成
- **修改文件**:
  - components/services/event_bus/include/event_bus.h (事件总线头文件)
  - components/services/event_bus/event_bus.c (事件总线实现)
  - components/services/event_bus/CMakeLists.txt
  - components/services/sensor_service/include/sensor_service.h (传感器服务头文件)
  - components/services/sensor_service/sensor_service.c (传感器服务实现)
  - components/services/sensor_service/CMakeLists.txt
  - main/main.c (集成事件总线和传感器服务)
  - main/CMakeLists.txt (添加依赖)
- **验收状态**: 待验收
- **验收清单**:
  - [ ] 传感器数据定时输出
  - [ ] 各传感器数据正常
  - [ ] 越阈后仅对应传感器切换实时检测
  - [ ] 恢复正常后可自动退出实时检测
- **备注**:
  - 事件总线基于 FreeRTOS Queue 实现发布/订阅模式
  - 传感器服务支持常规采样和实时采样两种模式
  - 常规采样间隔：体温 30s，心率血氧 120s，IMU 50Hz
  - 实时采样间隔：1s/次

---

### 2026-02-08 - Bug 修复: 迭代 2.1 代码审查问题

- **迭代**: Bug 修复
- **状态**: ✅ 已完成
- **问题描述**: 代码审查发现迭代 2.1 存在 3 个逻辑问题
- **问题列表**:
  1. 传感器数据未通过事件总线发布 `EVT_SENSOR_DATA` 事件
  2. `sensor_data_t.timestamp` 字段未更新
  3. DS18B20 采样阻塞 750ms 影响 IMU 50Hz 采样
- **解决方案**:
  1. 在 sensor_task 中定期发布 EVT_SENSOR_DATA 事件
  2. 每次采样循环更新 timestamp 字段
  3. 将 DS18B20 采样改为异步状态机模式（非阻塞）
- **修改文件**:
  - components/services/sensor_service/sensor_service.c (异步采样+事件发布)
  - components/drivers/ds18b20/include/ds18b20.h (添加 ds18b20_read_scratchpad)
  - components/drivers/ds18b20/ds18b20.c (实现 ds18b20_read_scratchpad)
  - CLAUDE.md (添加规则 9、10)
- **验收状态**: 待验收
- **验收清单**:
  - [ ] 传感器数据事件正常发布（main.c 回调被触发）
  - [ ] 时间戳正确更新
  - [ ] IMU 采样频率不受温度采样影响

---

### 2026-02-08 - Bug 修复: 温度采样初始化问题 + 采样间隔调整

- **迭代**: Bug 修复
- **状态**: ✅ 已完成
- **问题描述**: 温度数据始终显示 0.0
- **根本原因**: `sensor_service_start()` 中 `temp_last_sample` 初始化为 0，导致需等待 60 秒后才会触发第一次采样
- **解决方案**: 将 `temp_last_sample` 初始化为 `当前时间 - 采样间隔`，确保服务启动后立即触发第一次采样
- **额外修改**: 将温度采样间隔从 60 秒调整为 30 秒
- **修改文件**:
  - components/services/sensor_service/sensor_service.c (修复初始化逻辑)
  - components/services/sensor_service/include/sensor_service.h (采样间隔改为 30s)
  - PROGRESS.md (更新采样间隔说明)
- **验收状态**: 待验收
- **验收清单**:
  - [ ] 服务启动后立即开始温度采样
  - [ ] 温度数据正常显示（非 0.0）
  - [ ] 温度采样间隔为 30 秒

---

### 2026-02-08 - 迭代 2.2: 健康监测服务 + 计步算法

- **迭代**: Week 2 - 迭代 2.2
- **状态**: ✅ 已完成
- **修改文件**:
  - components/services/health_monitor/include/health_monitor.h (新建)
  - components/services/health_monitor/health_monitor.c (新建)
  - components/services/pedometer/include/pedometer.h (新建)
  - components/services/pedometer/pedometer.c (新建)
  - components/services/CMakeLists.txt (添加 health_monitor 和 pedometer)
  - components/ui_manager/ui_manager.c (使用真实数据)
  - components/ui_manager/CMakeLists.txt (添加 services 依赖)
  - main/main.c (初始化健康监测和计步服务)
- **验收状态**: 待验收
- **验收清单**:
  - [ ] 手指放上，OLED 显示心率 60-100 bpm
  - [ ] 血氧显示 95-99%
  - [ ] 走动时步数增加
  - [ ] 信号质量低时显示"No Signal"或"--"
  - [ ] 体温数据正常显示
- **备注**:
  - 健康监测服务实现了心率/血氧算法（峰值检测+R值计算）
  - 计步服务实现了加速度 SVM 峰值检测算法
  - UI 已集成真实数据显示
  - 告警事件发布功能已预留（TODO 注释）

---

### 2026-02-08 - Bug 修复: 心率血氧数据流断裂

- **迭代**: Bug 修复
- **状态**: ✅ 已完成
- **问题描述**: 代码审查发现心率血氧数据无法正常计算，始终为 0
- **根本原因**:
  1. `sensor_data_t` 缺少 PPG 原始数据字段
  2. `sample_hr_spo2()` 未存储 PPG 数据
  3. `on_sensor_data()` 未调用 `process_ppg_data()`
  4. `publish_health_alert()` 事件发布代码被注释
- **解决方案**:
  1. 在 `event_bus.h` 添加 `ppg_red`、`ppg_ir` 字段
  2. 在 `event_bus.h` 添加 `alert_level_t`、`alert_type_t`、`health_alert_t` 定义
  3. 修改 `sample_hr_spo2()` 存储 PPG 原始数据
  4. 修改 `on_sensor_data()` 调用 PPG 处理函数
  5. 启用 `publish_health_alert()` 事件发布
  6. 移除 `health_monitor.h` 中的重复类型定义
- **修改文件**:
  - components/services/event_bus/include/event_bus.h
  - components/services/sensor_service/sensor_service.c
  - components/services/health_monitor/health_monitor.c
  - components/services/health_monitor/include/health_monitor.h
- **验收状态**: 待验收
- **验收清单**:
  - [ ] 编译无错误
  - [ ] PPG 数据能正确传递到 health_monitor
  - [ ] 心率/血氧计算结果非零（手指放上时）

---

### 2026-02-08 - 迭代 2.3: 跌倒检测算法

- **迭代**: Week 2 - 迭代 2.3
- **状态**: ✅ 已完成
- **修改文件**:
  - components/services/fall_detect/include/fall_detect.h (新建)
  - components/services/fall_detect/fall_detect.c (新建)
  - components/services/CMakeLists.txt (添加 fall_detect)
- **验收状态**: 待验收
- **验收清单**:
  - [ ] 编译无错误
  - [ ] 模拟跌倒能检测到（串口打印 "Fall detected"）
  - [ ] 正常活动无误报
- **算法说明**:
  - SVM 计算加速度矢量幅值
  - 自由落体检测：SVM < 0.4g
  - 冲击检测：SVM > 2.5g
  - 静止检测：0.8g < SVM < 1.2g 持续 1 秒
  - 姿态变化检测：角度变化 > 60°
- **备注**:
  - 算法参数可在 fall_detect.h 中调整
  - 需要在 main.c 中集成调用

---

### 2026-02-08 - Bug 修复: 代码审查发现的 4 个问题

- **迭代**: Bug 修复
- **状态**: ✅ 已完成
- **问题列表**:
  1. 心率计算缺少峰值检测逻辑（严重）
  2. UI 不会自动刷新
  3. 步数未同步到 sensor_data_t
  4. 跌倒检测后状态未自动重置
- **修改文件**:
  - components/services/health_monitor/health_monitor.c (添加峰值检测)
  - components/ui_manager/ui_manager.c (添加定时刷新)
  - components/ui_manager/include/ui_manager.h (添加刷新间隔宏)
  - components/services/sensor_service/sensor_service.c (同步步数)
  - components/services/fall_detect/fall_detect.c (自动重置)
  - components/services/fall_detect/include/fall_detect.h (冷却时间)
- **验收状态**: 待验收
- **验收清单**:
  - [ ] 心率计算结果非零（手指放上时）
  - [ ] UI 每 2 分钟自动刷新
  - [ ] sensor_data_t 中步数正确
  - [ ] 跌倒检测后 30 秒自动重置
- **备注**:
  - 心率峰值检测使用 IR 信号趋势变化判定
  - UI 刷新使用 FreeRTOS 软件定时器
  - 跌倒检测冷却时间为 30 秒

---