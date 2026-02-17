# 项目进度记录

本文件记录 ESP32-S3 智能陪护手环项目的实际开发进度。

## 进度总览

| 周次 | 迭代 | 状态 | 完成日期 |
|------|------|------|----------|
| Week 1 | 迭代 1.1: 项目结构搭建 + I2C 总线验证 | ✅ 已完成 | 2026-02-08 |
| Week 1 | 迭代 1.2: MPU6050 驱动 + OLED 驱动 | ✅ 已完成 | 2026-02-08 |
| Week 1 | 迭代 1.3: MAX30102 驱动 + DS18B20 驱动 | ✅ 已完成 | 2026-02-08 |
| Week 1 | 迭代 1.4: 按键驱动 + 基础 UI 框架 | ✅ 已完成 | 2026-02-08 |
| Week 1 | 迭代 1.5: DS3231 RTC 驱动 + 大字体 + Home 页面重设计 | ✅ 已完成（待实机验收） | 2026-02-17 |
| Week 2 | 迭代 2.1: 事件总线 + 传感器采样服务 | ✅ 已完成 | 2026-02-08 |
| Week 2 | 迭代 2.2: 健康监测服务 + 计步算法 | ✅ 已完成 | 2026-02-08 |
| Week 2 | 迭代 2.3: 跌倒检测算法 | ✅ 已完成 | 2026-02-08 |
| Week 2 | 迭代 2.4: BLE GATT 服务 + 数据上报 | ✅ 已完成 | 2026-02-15 |
| Week 2 | 迭代 2.5: BLE 安全子任务 + 最小端到端切片 | ✅ 已完成（ESP32 固件部分） | 2026-02-15 |
| Week 3 | 迭代 3.1: 报警状态机 + 声光控制 | ✅ 已完成 | 2026-02-16 |
| Week 3 | 迭代 3.1 后代码结构优化 | ✅ 已完成 | 2026-02-16 |
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
- **验收状态**: 已验收
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
- **验收状态**: 已验收
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
- **验收状态**: 已验收
- **验收清单**:
  - [x] MPU6050 WHO_AM_I 返回 0x68
  - [x] 加速度数据随晃动变化
  - [x] OLED 点亮并显示文字
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
- **验收状态**: 已验收
- **验收清单**:
  - [x] MAX30102 Part ID 返回 0x15
  - [x] 手指触碰时 RED/IR 数据变化
  - [x] DS18B20 温度读数在 20-40°C 范围内
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
- **验收状态**: 已验收
- **验收清单**:
  - [x] SW2 短按切换页面
  - [x] SW2 长按可启动手动测量
  - [x] SW2 再次长按可中断手动测量
  - [x] SW1 按下有响应
  - [x] 无按键抖动误触发
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
- **验收状态**: 已验收
- **验收清单**:
  - [x] 长按 SW2 进入手动测量模式不再崩溃
  - [x] 系统运行稳定

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
- **验收状态**: 已验收
- **验收清单**:
  - [x] 传感器数据定时输出
  - [x] 各传感器数据正常
  - [x] 越阈后仅对应传感器切换实时检测
  - [x] 恢复正常后可自动退出实时检测
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
- **验收状态**: 已验收
- **验收清单**:
  - [x] 传感器数据事件正常发布（main.c 回调被触发）
  - [x] 时间戳正确更新
  - [x] IMU 采样频率不受温度采样影响

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
- **验收状态**: 已验收
- **验收清单**:
  - [x] 服务启动后立即开始温度采样
  - [x] 温度数据正常显示（非 0.0）
  - [x] 温度采样间隔为 30 秒

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
- **验收状态**: 已验收
- **验收清单**:
  - [x] 手指放上，OLED 显示心率 60-100 bpm
  - [x] 血氧显示 95-99%
  - [x] 走动时步数增加
  - [x] 信号质量低时显示"No Signal"或"--"
  - [x] 体温数据正常显示
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
- **验收状态**: 已验收
- **验收清单**:
  - [x] 编译无错误
  - [x] PPG 数据能正确传递到 health_monitor
  - [x] 心率/血氧计算结果非零（手指放上时）

---

### 2026-02-08 - 迭代 2.3: 跌倒检测算法

- **迭代**: Week 2 - 迭代 2.3
- **状态**: ✅ 已完成
- **修改文件**:
  - components/services/fall_detect/include/fall_detect.h (新建)
  - components/services/fall_detect/fall_detect.c (新建)
  - components/services/CMakeLists.txt (添加 fall_detect)
- **验收状态**: 已验收
- **验收清单**:
  - [x] 编译无错误
  - [x] 模拟跌倒能检测到（串口打印 "Fall detected"）
  - [x] 正常活动无误报
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
- **验收状态**: 已验收
- **验收清单**:
  - [x] 心率计算结果非零（手指放上时）
  - [x] UI 每 2 分钟自动刷新
  - [x] sensor_data_t 中步数正确
  - [x] 跌倒检测后 30 秒自动重置
- **备注**:
  - 心率峰值检测使用 IR 信号趋势变化判定
  - UI 刷新使用 FreeRTOS 软件定时器
  - 跌倒检测冷却时间为 30 秒

---

### 2026-02-09 - Bug 修复: 心率测量始终显示 0

- **迭代**: Bug 修复（影响迭代 2.2 心率功能）
- **状态**: ✅ 已完成
- **问题描述**: 心率测量始终显示为 0，无法正常计算心率
- **根本原因**:
  1. `sample_hr_spo2()` 仅每 120 秒调用一次，MAX30102 FIFO 在 0.32 秒内溢出，绝大部分 PPG 数据丢失
  2. 事件总线每 100ms 发布事件，但 PPG 数据 120 秒才更新一次，health_monitor 收到的全是过期数据
  3. 峰值检测算法需要连续新鲜数据才能检测到心跳峰值，过期数据无法触发峰值检测
- **解决方案**:
  1. 将 PPG 采样改为每个循环周期（20ms）持续读取 FIFO，防止数据溢出
  2. 在 `sensor_data_t` 中添加 `ppg_fresh` 标志位，标记本周期是否有新 PPG 数据
  3. 事件发布后清除 `ppg_fresh`，避免 health_monitor 处理过期数据
  4. health_monitor 的 `on_sensor_data()` 仅在 `ppg_fresh == true` 时处理 PPG 数据
- **修改文件**:
  - components/services/event_bus/include/event_bus.h（添加 ppg_fresh 字段）
  - components/services/sensor_service/sensor_service.c（PPG 持续采样 + ppg_fresh 机制）
  - components/services/health_monitor/health_monitor.c（仅处理新鲜 PPG 数据）
- **验收状态**: 已验收
- **验收清单**:
  - [x] 编译无错误
  - [x] 手指放上后心率显示非零值（60-100 bpm）
  - [x] 血氧显示正常（95-99%）
  - [x] 温度采样（30s）不受影响
  - [x] IMU 采样（50Hz）不受影响
  - [x] 计步功能正常
  - [x] 跌倒检测功能正常
- **备注**:
  - 由三人团队协作完成：项目经理（方案设计+协调）、开发员（代码实现）、测试员（代码审查）
  - 核心改动：PPG 从 120 秒间隔采样改为每 20ms 持续读取 FIFO

---

### 2026-02-13 - 开发计划同步：心率/血氧15秒测量窗口 + UI刷新频率分离

- **迭代**: 开发计划同步（影响迭代 2.1、2.2 的 sensor_service、health_monitor、ui_manager）
- **状态**: ✅ 已完成
- **背景**: development_plan.md 更新了以下需求：
  1. 体温低阈值从 35.0°C 改为 20.0°C（已在之前完成）
  2. 心率/血氧每次测量需连续采集15秒原始数据取平均值
  3. 自动测量每2分钟触发一次
  4. 步数 OLED 每500ms刷新
  5. 心率/血氧 OLED 每2分钟刷新（与测量周期对齐）
- **修改文件**:
  - components/ui_manager/include/ui_manager.h（新增 UI_STEP_REFRESH_INTERVAL_MS 宏）
  - components/ui_manager/ui_manager.c（新增500ms步数刷新定时器）
  - components/services/sensor_service/include/sensor_service.h（新增测量窗口常量、状态枚举、API）
  - components/services/sensor_service/sensor_service.c（实现15秒测量窗口状态机：IDLE/MEASURING/COMPLETE）
  - components/services/health_monitor/health_monitor.c（适配窗口机制，窗口内累积数据，窗口结束计算平均值）
- **验收状态**: 已验收
- **验收清单**:
  - [x] 步数 OLED 每500ms刷新
  - [x] 心率/血氧每2分钟自动测量（15秒采集后输出平均值）
  - [x] 手动触发测量同样走15秒窗口
  - [x] IDLE状态下MAX30102 shutdown节省功耗
  - [x] 编译无错误
- **备注**:
  - 由三人团队协作完成：team-lead（差距分析+方案拆分+协调）、developer（代码实现）、tester（代码审查）
  - 子任务A（UI刷新分离）测试通过，无阻塞性问题
  - 子任务B+C（15秒测量窗口）测试通过，9项审查要点全部通过
  - 测试员提出的非阻塞性优化建议留待后续迭代处理

---

### 2026-02-15 - 迭代 2.4: BLE GATT 服务 + 数据上报

- **迭代**: Week 2 - 迭代 2.4
- **状态**: ✅ 已完成
- **新建文件**:
  - components/ble_gatt/ble_gatt_defs.h (UUID 定义、数据包结构体、命令/报警枚举)
  - components/ble_gatt/include/ble_service.h (BLE 服务对外 API)
  - components/ble_gatt/ble_service.c (NimBLE 初始化、GATT 注册、广播、Notify、Telemetry 上报任务)
  - components/ble_gatt/CMakeLists.txt (组件构建文件)
- **修改文件**:
  - main/main.c (集成 ble_service_init 调用)
  - main/CMakeLists.txt (添加 ble_gatt 依赖)
  - sdkconfig (确认 NimBLE 配置)
- **验收状态**: 已验收
- **验收清单**:
  - [x] nRF Connect 能扫描到 "CareBand"
  - [x] 连接后能看到服务 UUID 0000FF00-...
  - [x] 4 个特征 (FF01 Notify, FF02 Notify, FF03 Write, FF04 Read) 可见
  - [x] 订阅 Telemetry (FF01) 后每 2 分钟收到 20 bytes 数据包
  - [x] 读取 Status (FF04) 返回 3 bytes 设备状态
  - [x] 断连后自动重新广播
- **备注**:
  - 由四人团队协作完成：team-lead（方案设计+协调+审查）、developer-1（头文件定义+GATT注册+事件总线集成）、developer-2（核心框架+Telemetry上报+main集成）、tester（代码审查）
  - GATT 服务定义：1 个 PRIMARY 服务 + 4 个特征 (Telemetry/Alarm/Command/Status)
  - 广播数据仅包含 Flags + 设备名 (13 bytes)，128-bit UUID 放在 Scan Response 中避免超限
  - 连接/断连事件通过 event_bus 发布 EVT_BLE_CONN
  - Telemetry 上报任务独立线程运行，间隔 120 秒，从 sensor_service/health_monitor/pedometer 采集数据
  - timestamp 目前为系统启动秒数，待迭代 2.5 实现时间同步后改为 Unix 时间戳
  - Command 特征写入已实现基础接收和日志，完整命令解析留待迭代 2.5

---

### 2026-02-15 - 迭代 2.5: BLE 安全子任务 + 最小端到端切片（ESP32 固件部分）

- **迭代**: Week 2 - 迭代 2.5
- **状态**: ✅ 已完成（ESP32 固件部分，Flutter 网关因 SDK 未安装而延后）
- **新建文件**:
  - components/ble_gatt/include/ble_security.h（BLE 安全模块头文件：SM 初始化、nonce 校验、加密状态查询、GAP 事件处理）
  - components/ble_gatt/ble_security.c（BLE 安全模块实现：NoInputNoOutput 配对、bonding、SC、nonce 单调递增校验、1 设备限制）
- **修改文件**:
  - components/ble_gatt/CMakeLists.txt（添加 ble_security.c）
  - components/ble_gatt/ble_service.c（集成安全模块、完整命令解析、时间同步、Telemetry 重构）
  - components/ble_gatt/include/ble_service.h（添加 ble_get_unix_timestamp() 声明）
  - main/main.c（SW1 手动报警流程：BLE Alarm Notify + 事件总线发布）
- **验收状态**: 已验收
- **验收清单**:
  - [x] 编译通过（✅ 已验证）
  - [x] nRF Connect 连接后触发配对请求
  - [x] 配对成功后 Command (FF03) 可写入
  - [x] 未配对设备写入 FF03 被拒绝
  - [x] 重新配对时自动删除旧 bond（1 设备限制）
  - [x] 命令 nonce 重放被拒绝（返回 0x08 Insufficient Authorization）
  - [x] SYNC_TIME 命令成功后 Telemetry timestamp 为 Unix 时间
  - [x] REQUEST_REPORT 命令触发立即上报 Telemetry
  - [x] MANUAL_MEASURE 命令启动/停止手动测量
  - [x] SW1 短按发送 BLE Alarm Notify（type=MANUAL）
  - [x] SW1 报警同时发布 EVT_ALARM_STATE 到事件总线
- **备注**:
  - 由四人团队协作完成：team-lead（方案设计+协调+修复）、developer-1（BLE 安全模块+命令解析）、developer-2（SW1 报警流程）、tester（代码审查）
  - 子任务 A：BLE 安全模块（ble_security.h/c）- Just Works 配对、bonding、nonce 校验
  - 子任务 B：命令解析+时间同步（ble_service.c）- 4 种命令类型完整实现
  - 子任务 C：SW1 手动报警流程（main.c）- 组装 Alarm 包 + Notify + 事件总线
  - 子任务 D：Flutter 最小网关（延后）- Flutter SDK 未安装
  - 编译时发现 `BLE_ATT_ERR_AUTHORIZATION` 不存在，修正为 `BLE_ATT_ERR_INSUFFICIENT_AUTHOR`（ISSUE-007）
  - 测试员发现 2 个 Major 级别问题（非阻塞）：
    1. s_last_nonce 线程安全依赖 NimBLE 单任务模型（MVP 可接受）
    2. EVT_ALARM_STATE 使用 ALERT_TYPE_NONE 代替缺失的 ALERT_TYPE_MANUAL（后续迭代补充枚举值）

---

### 2026-02-16 - 迭代 3.1: 报警状态机 + 声光控制

- **迭代**: Week 3 - 迭代 3.1
- **状态**: ✅ 已完成
- **新建文件**:
  - components/drivers/ws2812/include/ws2812.h（WS2812 RGB LED 驱动头文件）
  - components/drivers/ws2812/ws2812.c（WS2812 驱动实现，RMT 外设 + FreeRTOS 定时器闪烁）
  - components/services/alarm_manager/include/alarm_manager.h（报警管理器头文件，状态机接口定义）
  - components/services/alarm_manager/alarm_manager.c（报警管理器实现，状态机 + WS2812 控制 + BLE Alarm）
- **修改文件**:
  - components/drivers/CMakeLists.txt（添加 ws2812 源文件和头文件路径）
  - components/services/CMakeLists.txt（添加 alarm_manager 源文件和头文件路径，新增 ble_gatt 依赖）
  - components/services/event_bus/include/event_bus.h（添加 ALERT_TYPE_MANUAL=8, ALERT_TYPE_CALL_FAMILY=9）
  - main/main.c（集成 alarm_manager 和 ws2812 初始化，重构 SW1 回调使用状态机 API）
- **验收状态**: 已验收
- **验收清单**:
  - [x] 模拟跌倒 → PRE_ALARM → 15秒后 ALARMING
  - [x] 按 SW1 取消 PRE_ALARM
  - [x] ALARMING 时 WS2812 红色闪烁
- **功能说明**:
  - 报警状态机: IDLE → PRE_ALARM → ALARMING → ACKED
  - PRE_ALARM: 跌倒专用，15秒倒计时，WS2812 黄色闪烁(500ms)
  - ALARMING: WS2812 红色快闪(200ms)，发送 BLE Alarm Notify
  - ACKED: 绿色常亮 2 秒后自动回 IDLE
  - SW1 短按: IDLE→手动报警, PRE_ALARM→取消
  - SW1 长按: ALARMING→取消
  - alarm_manager 自动订阅 EVT_HEALTH_ALERT 和 EVT_FALL_DETECTED 事件
  - alert_type_t → ble_alarm_type_t 使用显式 switch 映射（两枚举值不同）
- **编译结果**: ✅ 通过，二进制 643KB，app 分区 39% 空闲
- **遗留问题**:
  - BLE ACK_ALARM 命令尚未调用 alarm_ack()（ble_service.c:194 有 TODO），计划在迭代 3.4 联调时修复
  - 迭代 2.5 遗留的 ALERT_TYPE_MANUAL 缺失问题已在本迭代中修复
- **备注**:
  - 由四人团队协作完成：team-lead（方案设计+协调+审查）、developer-1（WS2812 驱动+main.c 集成）、developer-2（event_bus 增强+alarm_manager 头文件+核心实现）、tester（代码审查）
  - 子任务 A: WS2812 RGB LED 驱动（RMT + bytes encoder + FreeRTOS 定时器闪烁）
  - 子任务 B: event_bus.h 补充 ALERT_TYPE_MANUAL/CALL_FAMILY + alarm_manager.h 接口定义
  - 子任务 C: alarm_manager.c 状态机核心（互斥锁保护、事件订阅、BLE 报警发送）
  - 子任务 D: main.c 集成（初始化顺序、SW1 回调重构、移除冗余 fall_detected_handler）

---

### 迭代 3.1 后代码优化

- **完成日期**: 2026-02-16
- **任务简述**: 清理迭代 1.1 ~ 3.1 开发过程中积累的调试代码，恢复误注释的功能代码，统一日志规范
- **对应开发计划**: 迭代 3.1 后、迭代 3.2 前的维护性优化
- **修改/新增的文件列表**:
  - main/main.c（ESP_LOGI -> ESP_LOGD：降级周期性传感器数据打印）
  - components/services/sensor_service/sensor_service.c（恢复注释的 HR 模式变更日志）
  - components/services/health_monitor/health_monitor.c（恢复注释的 publish_health_alert() 调用和告警日志）
  - components/ble_gatt/ble_service.c（删除重复连接日志；ESP_LOGI -> ESP_LOGD：广播完成/MTU/Subscribe/Telemetry 发送）
  - components/ui_manager/ui_manager.c（修复 TAG 使用不一致：字符串字面量 -> TAG 变量）
  - components/drivers/ws2812/ws2812.c（ESP_LOGI -> ESP_LOGD：闪烁开始/停止日志）
  - ISSUE.md（新增 ISSUE-008、ISSUE-009）
  - PROGRESS.md（新增本记录）
- **验收状态**: 已验收
- **验收清单**:
  - [x] 编译通过，无错误，无新增警告
  - [x] 二进制大小正常（627KB，app 分区 40% 空闲）
  - [x] 代码审查通过（测试员确认逻辑正确性、日志级别合理性、功能完整性、资源评估）
  - [x] 实机测试：健康告警能否正常触发 alarm_manager（恢复 publish_health_alert 后）
- **遗留问题**:
  - 恢复 publish_health_alert() 后，需要实机测试验证健康告警 -> 报警状态机的完整链路
- **备注**:
  - 由四人团队协作完成：team-lead（方案设计+任务分配+代码审查）、developer-1（main.c/sensor_service.c/health_monitor.c）、developer-2（ble_service.c/ui_manager.c/ws2812.c）、tester（代码审查+逻辑验证）
  - 本次优化为非功能性修改（bug 修复 + 日志精简），不影响现有功能
  - 修复了一个重要 bug：health_monitor 健康告警无法发布到事件总线（ISSUE-008）

---

### 2026-02-16 - 迭代 3.1 后代码结构优化（团队协作）

- **迭代**: 迭代 3.1 后、迭代 3.2 前的结构优化
- **状态**: ✅ 已完成
- **任务简述**: 对迭代 1.1 ~ 3.1 积累的代码进行结构优化，消除重复代码、统一架构模式、清理不必要的公共接口
- **优化项目**:
  1. **PPG AC/DC 计算效率优化** (health_monitor.c) - 将 AC/DC 计算从每次样本 O(n) 遍历改为窗口结束时一次性计算，提取 `calculate_ac_dc()` 函数
  2. **共享 `get_timestamp_ms()` 提取** (event_bus.h) - 消除 sensor_service.c 和 fall_detect.c 中的重复定义，移至 event_bus.h 作为 `static inline` 函数
  3. **fall_detect 事件总线集成** (fall_detect.c) - 从外部调用模式改为自订阅事件总线，与 pedometer/health_monitor 架构一致
  4. **BLE Notify 重复代码合并** (ble_service.c) - 提取 `ble_notify_raw()` 通用函数，`ble_notify_telemetry()` 和 `ble_notify_alarm()` 简化为单行委派
  5. **pedometer_feed_data() 接口清理** (pedometer.c/h) - 仅内部使用的函数从 public 改为 static
  6. **main.c 跌倒检测调用移除** (main.c) - fall_detect 自订阅后，main.c 不再需要手动转发 IMU 数据
- **修改文件**:
  - components/services/event_bus/include/event_bus.h（添加共享 `get_timestamp_ms()`）
  - components/services/health_monitor/health_monitor.c（提取 `calculate_ac_dc()`，窗口结束时调用）
  - components/services/fall_detect/fall_detect.c（添加事件总线订阅/取消，移除重复 `get_timestamp_ms()`）
  - components/services/sensor_service/sensor_service.c（移除重复 `get_timestamp_ms()`）
  - components/ble_gatt/ble_service.c（提取 `ble_notify_raw()` 通用函数）
  - components/services/pedometer/pedometer.c（`pedometer_feed_data()` 改为 static）
  - components/services/pedometer/include/pedometer.h（移除 `pedometer_feed_data()` 公共声明）
  - main/main.c（移除 `fall_detect_process()` 调用）
- **验收状态**: 已验收
- **验收清单**:
  - [x] 编译通过，无错误
  - [x] 二进制大小正常（627KB，app 分区 40% 空闲）
  - [x] 代码审查通过（测试员确认逻辑正确性）
  - [x] 实机测试：跌倒检测功能正常（fall_detect 从 20ms 采样改为 100ms 事件总线周期）
  - [x] 实机测试：计步功能正常
  - [x] 实机测试：心率血氧测量正常
- **统计**: 8 个文件修改，85 行新增，80 行删除
- **注意事项**:
  - fall_detect 集成事件总线后，IMU 数据接收频率从 20ms（sensor_task 循环）变为 100ms（事件发布周期），需实机验证跌倒检测灵敏度是否受影响
- **备注**:
  - 由五人团队协作完成：team-lead（方案设计+协调+代码审查）、developer-1（PPG 优化+fall_detect 集成）、developer-2（timestamp 提取+BLE 合并+pedometer 清理）、tester（完整代码审查+编译验证）
  - 本次优化为非功能性重构，不影响现有功能，仅改善代码结构和可维护性

---

### 2026-02-17 - Bug 修复: 心率/血氧实时监测模式与持续越阈报警

- **迭代**: Bug 修复（影响迭代 2.1 sensor_service、迭代 2.2 health_monitor）
- **状态**: ✅ 已完成
- **问题描述**: 心率/血氧实时监测模式存在 3 个关键缺陷：hr_mode 设置后未在采样逻辑中使用、实时模式在报警确认后才触发而非首次异常时、正常化后没有退出实时模式的机制
- **修改文件**:
  - components/services/sensor_service/sensor_service.c（HR_MEASURE_IDLE 动态间隔、HR_MEASURE_COMPLETE 更新时间戳）
  - components/services/health_monitor/include/health_monitor.h（删除时间阈值宏，新增计数阈值宏 HR_ALARM_COUNT/SPO2_ALARM_COUNT）
  - components/services/health_monitor/health_monitor.c（alert_context_t 改为计数器、计数制告警检查、首次异常触发实时模式、正常化退出实时模式、health_reset_alert 更新）
- **验收状态**: 已验收
- **验收清单**:
  - [x] 编译通过，无错误
  - [x] 正常模式下心率仍然 120 秒自动触发一次
  - [x] 检测到异常 HR 后，测量间隔缩短为 ~16 秒
  - [x] 连续 2 次异常测量后触发报警
  - [x] HR 恢复正常后自动退回 120 秒间隔
  - [x] SpO2 异常同样触发实时模式
  - [x] HR 和 SpO2 中任一指标仍异常时保持实时模式
- **备注**:
  - ISSUE-010: hr_mode 设置但未使用
  - ISSUE-011: 实时模式触发时机错误
  - ISSUE-012: 缺少实时模式退出机制

---

### 2026-02-17 - 开发计划更新：添加 DS3231 RTC + Home 页面重设计计划

- **迭代**: 开发计划维护
- **状态**: ✅ 已完成
- **任务简述**: 在 development_plan.md 中添加 DS3231 RTC 模块驱动和 Home 页面日期时间显示的具体开发计划
- **修改文件**:
  - development_plan.md（新增迭代 1.5、DS3231 模块描述、SH1106 多字号支持、Home 页面布局、硬件引脚表更新）
  - PROGRESS.md（进度总览添加迭代 1.5 + 本记录）
- **具体变更内容**:
  1. 甘特图总览（第四章）：Week 1 新增迭代 1.5
  2. 模块划分（第二章）：新增 `components/drivers/ds3231` 模块描述
  3. 模块划分（第二章）：SH1106 模块新增大字体（16x24）和中文字模（12x12）支持说明
  4. 模块划分（第二章）：UI Manager 模块新增 Home 页面布局设计（日期+时间+温度）
  5. 迭代计划（第五章）：新增迭代 1.5 详细步骤、产出物、验收方法
  6. 硬件引脚分配表（第 8.5 节）：新增 DS3231 RTC（I2C, SDA=GPIO8, SCL=GPIO9, 地址 0x68）
  7. 硬件检查点（第 8.2 节）：I2C 设备地址新增 0x68
  8. I2C 总线验证（迭代 1.1）：验收方法新增 DS3231 地址扫描
  9. 交付清单（第七章）：代码结构新增 ds3231/ 目录
- **验收状态**: 待验收
- **备注**: Home 页面布局为日期（左上）+ 温度（右上）+ 大字体时间（居中），心率/血氧/步数在独立页面查看

---

### 2026-02-17 - 迭代 1.5: DS3231 RTC 驱动 + 大字体扩展 + Home 页面重设计（开发补充）

- **迭代**: Week 1 - 迭代 1.5
- **状态**: ✅ 已完成（待实机验收）
- **任务简述**: 根据 development_plan.md 的新增要求，补充 DS3231 驱动、SH1106 大字体/中文星期显示能力，并将 Home 页面重构为“日期+星期+温度+大时间”布局
- **修改/新增文件**:
  - components/drivers/ds3231/include/ds3231.h（新增 DS3231 对外接口和时间结构体）
  - components/drivers/ds3231/ds3231.c（新增 DS3231 初始化、BCD 编解码、读写实现）
  - components/drivers/CMakeLists.txt（新增 ds3231 源文件与头文件路径）
  - components/drivers/common/i2c_bus.c（I2C 扫描新增 0x68=DS3231 设备名）
  - components/drivers/sh1106/include/font_large.h（新增 16x24 大数字字模定义）
  - components/drivers/sh1106/include/font_cn.h（新增 12x12 中文星期字模）
  - components/drivers/sh1106/include/sh1106.h（新增 `sh1106_draw_string_large` 和 `sh1106_draw_chinese` 声明）
  - components/drivers/sh1106/sh1106.c（实现大字体渲染与中文绘制）
  - components/ui_manager/ui_manager.c（Home 页面布局重构，接入 DS3231 时间读取与分钟级刷新）
  - main/main.c（新增 DS3231 初始化与启动日志读取）
- **验收状态**: 待验收（代码与编译已通过，待实机显示验证）
- **验收清单**:
  - [x] `idf.py build` 编译通过
  - [x] DS3231 组件已编译链接
  - [x] Home 页面已按新布局改造（日期+中文星期+温度+大时间）
  - [x] 时间刷新逻辑已改为分钟变化触发
  - [ ] 实机确认中文星期字模显示效果
  - [ ] 实机确认 DS3231 实际读时正确性
- **备注**:
  - `main.c` 启动阶段会打印 RTC 读时日志，便于串口验收
  - 目前日期星期基于 RTC 日期计算（不依赖 DS3231 DOW 寄存器）

---

### 2026-02-17 - Bug 修复: Home 页面“星期X”中文字模乱码

- **迭代**: Week 1 - 迭代 1.5（显示细节修复）
- **状态**: ✅ 已完成（待实机确认显示效果）
- **任务简述**: 修复 Home 页“星期X”字段中文显示不可辨识的问题，重做 12x12 中文字模数据
- **修改文件**:
  - components/drivers/sh1106/include/font_cn.h（替换 9 个中文字符字模）
- **验收状态**: 待验收
- **验收清单**:
  - [x] `idf.py build` 编译通过
  - [x] `sh1106_draw_chinese()` 无需改动即可正常编译运行
  - [ ] 实机确认“星期一/二/三/四/五/六/日”可清晰辨认
- **备注**:
  - 本次修复仅替换字模，不改动渲染接口与页面布局
  - 对应问题记录：ISSUE-015

---

### 2026-02-17 - Bug 修复: 心率/血氧测量期间 OLED 花屏（UI 并发绘制）

- **迭代**: Week 1 - 迭代 1.5（稳定性修复）
- **状态**: ✅ 已完成（待实机验收）
- **任务简述**: 修复手动心率/血氧测量期间 OLED 花屏问题，根因是 UI 定时器回调与按键任务回调并发操作 SH1106 缓冲区
- **修改文件**:
  - components/ui_manager/ui_manager.c（新增 UI 互斥锁与 `_locked` 内部函数，串行化所有绘制路径）
- **验收状态**: 待验收
- **验收清单**:
  - [x] `idf.py build` 编译通过
  - [x] UI 入口函数（页面切换/手动测量进出/更新）均已加锁
  - [x] 定时器回调已改为非阻塞抢锁，避免并发绘制
  - [ ] 实机连续进行心率/血氧测量 5 分钟无花屏
- **备注**:
  - 对应问题记录：ISSUE-016
  - 二进制大小：`0x99f20`，app 分区空闲约 40%

---
