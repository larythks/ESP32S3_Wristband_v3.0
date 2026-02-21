# 项目进度记录

本文件记录 ESP32-S3 智能陪护手环项目的实际开发进度。

## 进度总览

| 周次 | 迭代 | 状态 | 完成日期 |
|------|------|------|----------|
| Week 1 | 迭代 1.1: 项目结构搭建 + I2C 总线验证 | ✅ 已完成 | 2026-02-08 |
| Week 1 | 迭代 1.2: MPU6050 驱动 + OLED 驱动 | ✅ 已完成 | 2026-02-08 |
| Week 1 | 迭代 1.3: MAX30102 驱动 + DS18B20 驱动 | ✅ 已完成 | 2026-02-08 |
| Week 1 | 迭代 1.4: 按键驱动 + 基础 UI 框架 | ✅ 已完成 | 2026-02-08 |
| Week 1 | 迭代 1.5: DS3231 RTC 驱动 + 大字体 + Home 页面重设计 | ✅ 已完成 | 2026-02-17 |
| Week 2 | 迭代 2.1: 事件总线 + 传感器采样服务 | ✅ 已完成 | 2026-02-08 |
| Week 2 | 迭代 2.2: 健康监测服务 + 计步算法 | ✅ 已完成 | 2026-02-08 |
| Week 2 | 迭代 2.3: 跌倒检测算法 | ✅ 已完成 | 2026-02-08 |
| Week 2 | 迭代 2.4: BLE GATT 服务 + 数据上报 | ✅ 已完成 | 2026-02-15 |
| Week 2 | 迭代 2.5: BLE 安全子任务 + 最小端到端切片 | ✅ 已完成（ESP32 固件部分） | 2026-02-15 |
| Week 3 | 迭代 3.1: 报警状态机 + 声光控制 | ✅ 已完成 | 2026-02-16 |
| Week 3 | 迭代 3.1 后代码结构优化 | ✅ 已完成 | 2026-02-16 |
| Week 3 | 迭代 3.2: I2S 音频播放 | ✅ 已完成 | 2026-02-18 |
| Week 3 | 迭代 3.3: ESP-SR 语音识别集成 | ✅ 已完成 | 2026-02-18 |
| Week 3 | 迭代 3.4: 完整报警流程联调 | ✅ 已完成 | 2026-02-19 |
| Week 4 | 迭代 4.1: Flutter 项目搭建 + BLE 连接 | ✅ 已完成 | 2026-02-20 |
| Week 4 | 迭代 4.2: 数据展示 UI + 报警 UI | ✅ 已完成 | 2026-02-21 |
| Week 4 | 迭代 4.3: MQTT 网关 + EMQX Cloud 部署 | ✅ 已完成 | 2026-02-21 |
| Week 4 | 迭代 4.4: 端到端联调 + 问题修复 | 🔲 待开始 | - |

## 详细记录

---

### 2026-02-08 - 项目进度追踪机制建立

- **迭代**: 项目基础设施
- **状态**: ✅ 已完成
- **主要改动**:
  - CLAUDE.md 添加任务完成与进度追踪规则
  - 新建 PROGRESS.md
- **关键文件**: CLAUDE.md, PROGRESS.md

---

### 2026-02-08 - 迭代 1.1: 项目结构搭建 + I2C 总线验证

- **迭代**: Week 1 - 迭代 1.1
- **状态**: ✅ 已完成
- **主要改动**:
  - 实现 I2C 总线初始化、设备扫描、读写功能
  - 搭建项目基础目录结构
- **关键文件**: components/common/i2c_bus.h, components/common/i2c_bus.c, main/main.c

---

### 2026-02-08 - 迭代 1.2: MPU6050 驱动 + OLED 驱动

- **迭代**: Week 1 - 迭代 1.2
- **状态**: ✅ 已完成
- **主要改动**:
  - 实现 MPU6050 加速度/陀螺仪读取驱动
  - 实现 SH1106 OLED 显示驱动（含 5x7 ASCII 字体）
- **关键文件**: components/drivers/mpu6050/, components/drivers/sh1106/, main/main.c

---

### 2026-02-08 - 迭代 1.3: MAX30102 驱动 + DS18B20 驱动

- **迭代**: Week 1 - 迭代 1.3
- **状态**: ✅ 已完成
- **主要改动**:
  - 实现 MAX30102 心率血氧传感器驱动
  - 实现 DS18B20 温度传感器驱动（含 1-Wire 协议）
- **关键文件**: components/drivers/max30102/, components/drivers/ds18b20/, main/main.c

---

### 2026-02-08 - 迭代 1.4: 按键驱动 + 基础 UI 框架

- **迭代**: Week 1 - 迭代 1.4
- **状态**: ✅ 已完成
- **主要改动**:
  - 按键驱动：FreeRTOS 定时器实现消抖和长按检测
  - UI 管理器：支持 4 个页面（主页、心率页、步数页、手动测量页）
  - 手动测量模式可通过 SW2 长按进入/退出
- **关键文件**: components/drivers/button/, components/ui_manager/, main/main.c
- **附带修复**: ISSUE-相关 — Timer Service 栈溢出（`CONFIG_FREERTOS_TIMER_TASK_STACK_DEPTH` 从 2048 增大到 4096，定时器回调中 UI 绘制函数链导致栈溢出）

---

### 2026-02-17 - 迭代 1.5: DS3231 RTC + 大字体 + Home 页面重设计

- **迭代**: Week 1 - 迭代 1.5
- **状态**: ✅ 已完成
- **主要改动**:
  - DS3231 RTC 驱动：初始化、BCD 编解码、时间读写
  - SH1106 扩展：16x24 大数字字模、12x12 中文星期字模
  - Home 页面布局重构：日期+中文星期+温度+大字体时间，分钟级刷新
  - development_plan.md 新增迭代 1.5 详细计划
- **关键文件**: components/drivers/ds3231/, components/drivers/sh1106/include/font_large.h, components/drivers/sh1106/include/font_cn.h, components/ui_manager/ui_manager.c
- **附带修复**:
  - ISSUE-015: 中文字模数据不可辨识，替换 9 个字符的 12x12 位图字模
  - ISSUE-016: UI 并发绘制导致 OLED 花屏，新增 UI 互斥锁串行化绘制路径
  - ISSUE-017/018: BLE SYNC_TIME 未同步到 DS3231 RTC，新增 Unix→本地时间转换+写入 RTC

---

### 2026-02-08 - 迭代 2.1: 事件总线 + 传感器采样服务

- **迭代**: Week 2 - 迭代 2.1
- **状态**: ✅ 已完成
- **主要改动**:
  - 事件总线：基于 FreeRTOS Queue 的发布/订阅模式
  - 传感器服务：常规采样（温度 30s、心率血氧 480s、IMU 50Hz）和实时采样（16s/次）两种模式
  - DS18B20 采样改为异步状态机模式（非阻塞）
- **关键文件**: components/services/event_bus/, components/services/sensor_service/, main/main.c
- **附带修复**:
  - 传感器数据未通过事件总线发布 EVT_SENSOR_DATA 事件
  - timestamp 字段未更新
  - DS18B20 阻塞 750ms 影响 IMU 50Hz 采样（CLAUDE.md 新增规则 9、10）
  - 温度采样初始化问题：`temp_last_sample` 初始化为 0 导致首次采样延迟 60 秒

---

### 2026-02-08 - 迭代 2.2: 健康监测服务 + 计步算法

- **迭代**: Week 2 - 迭代 2.2
- **状态**: ✅ 已完成
- **主要改动**:
  - 健康监测服务：心率峰值检测算法 + 血氧 R 值计算
  - 计步服务：加速度 SVM 峰值检测算法
  - UI 集成真实数据显示
- **关键文件**: components/services/health_monitor/, components/services/pedometer/, components/ui_manager/ui_manager.c
- **附带修复**:
  - PPG 数据流断裂（ISSUE 相关）：sensor_data_t 缺少 PPG 原始数据字段、sample_hr_spo2 未存储 PPG 数据、on_sensor_data 未调用 process_ppg_data、publish_health_alert 被注释
  - 心率始终显示 0：PPG 采样从 120 秒间隔改为每 20ms 持续读取 FIFO，新增 ppg_fresh 机制防止处理过期数据
  - 代码审查 4 问题：心率缺少峰值检测逻辑、UI 不自动刷新、步数未同步到 sensor_data_t、跌倒检测后状态未自动重置

---

### 2026-02-08 - 迭代 2.3: 跌倒检测算法

- **迭代**: Week 2 - 迭代 2.3
- **状态**: ✅ 已完成
- **主要改动**:
  - SVM 加速度矢量幅值计算
  - 四阶段检测：自由落体（< 0.4g）→ 冲击（> 2.5g）→ 静止（0.8-1.2g, 1s）→ 姿态变化（> 60°）
- **关键文件**: components/services/fall_detect/

---

### 2026-02-13 - 开发计划同步: 15 秒测量窗口 + UI 刷新分离

- **迭代**: 开发计划同步（影响迭代 2.1、2.2）
- **状态**: ✅ 已完成
- **主要改动**:
  - 心率/血氧每次测量连续采集 15 秒原始数据取平均值
  - 自动测量每 2 分钟触发一次
  - 步数 OLED 每 500ms 刷新（新增独立定时器）
  - 传感器服务实现 IDLE/MEASURING/COMPLETE 三态测量窗口状态机
  - IDLE 状态下 MAX30102 shutdown 节省功耗
- **关键文件**: components/ui_manager/, components/services/sensor_service/, components/services/health_monitor/

---

### 2026-02-15 - 迭代 2.4: BLE GATT 服务 + 数据上报

- **迭代**: Week 2 - 迭代 2.4
- **状态**: ✅ 已完成
- **主要改动**:
  - NimBLE 初始化、GATT 注册：1 个 PRIMARY 服务 + 4 个特征（Telemetry/Alarm/Command/Status）
  - 广播数据仅 Flags + 设备名，128-bit UUID 放入 Scan Response（ISSUE-005）
  - Telemetry 上报任务：独立线程，间隔 480 秒
  - 连接/断连事件通过 event_bus 发布 EVT_BLE_CONN
- **关键文件**: components/ble_gatt/

---

### 2026-02-15 - 迭代 2.5: BLE 安全 + 端到端切片

- **迭代**: Week 2 - 迭代 2.5
- **状态**: ✅ 已完成（ESP32 固件部分，Flutter 延后）
- **主要改动**:
  - BLE 安全模块：Just Works 配对、bonding、nonce 单调递增校验、1 设备限制
  - 完整命令解析：SYNC_TIME / REQUEST_REPORT / MANUAL_MEASURE / ACK_ALARM
  - SW1 手动报警流程：BLE Alarm Notify + 事件总线发布
- **关键文件**: components/ble_gatt/ble_security.c, components/ble_gatt/ble_service.c, main/main.c
- **附带修复**: ISSUE-007（BLE_ATT_ERR_AUTHORIZATION 宏不存在，改为 BLE_ATT_ERR_INSUFFICIENT_AUTHOR）

---

### 2026-02-16 - 迭代 3.1: 报警状态机 + 声光控制 + 代码优化

- **迭代**: Week 3 - 迭代 3.1
- **状态**: ✅ 已完成
- **主要改动**:
  - WS2812 RGB LED 驱动（RMT 外设 + FreeRTOS 定时器闪烁）
  - 报警状态机：IDLE → PRE_ALARM → ALARMING → ACKED
  - PRE_ALARM: 跌倒专用 15 秒倒计时，黄色闪烁；ALARMING: 红色快闪 + BLE Alarm
  - SW1 短按: IDLE→手动报警, PRE_ALARM→取消；SW1 长按: ALARMING→取消
  - 迭代后代码清理：恢复误注释的 publish_health_alert()、日志精简（ESP_LOGI→ESP_LOGD）
  - 代码结构优化：PPG AC/DC 计算效率优化、共享 get_timestamp_ms() 提取、fall_detect 事件总线集成、BLE Notify 合并、pedometer_feed_data 改为 static
  - ALARMING 状态报警音频从单次播放改为循环播放（ISSUE-035）
- **关键文件**: components/drivers/ws2812/, components/services/alarm_manager/, components/services/event_bus/include/event_bus.h, main/main.c
- **附带修复**:
  - ISSUE-008: ALARMING 状态下新报警不发布 EVT_ALARM_STATE 事件
  - ISSUE-009: ui_manager 日志 TAG 使用不一致
  - ISSUE-035: ALARMING 报警音频仅播放一次

---

### 2026-02-17 - Bug 修复: 心率/血氧实时监测模式

- **迭代**: Bug 修复（影响迭代 2.1、2.2）
- **状态**: ✅ 已完成
- **主要改动**:
  - hr_mode 设置后在采样逻辑中实际使用（动态选择间隔）
  - 告警判定从时间制改为计数制（连续 2 次异常测量后触发）
  - 首次异常立即触发实时模式，正常化后自动退出
- **关键文件**: components/services/sensor_service/sensor_service.c, components/services/health_monitor/
- **附带修复**: ISSUE-010, ISSUE-011, ISSUE-012

---

### 2026-02-18 - 需求变更: 体温 → 环境温度

- **迭代**: 全局需求变更
- **状态**: ✅ 已完成
- **主要改动**:
  - 所有"体温监测"概念改为"环境温度监测"
  - 高温报警阈值从 37.8°C 改为 35.0°C，低温阈值保持 20.0°C
  - sensor_service 滞回阈值从 37.5/37.3 改为 34.8/34.5
  - 默认温度从 36.5 改为 25.0（室温）
- **关键文件**: development_plan.md, health_monitor.h, sensor_service.h, health_monitor.c, event_bus.h, ble_gatt_defs.h, ui_manager.c

---

### 2026-02-18 - 迭代 3.2: I2S 音频播放

- **迭代**: Week 3 - 迭代 3.2
- **状态**: ✅ 已完成
- **主要改动**:
  - SPIFFS 挂载 + I2S 扬声器驱动 + WAV 解码播放
  - 数字拼接 TTS 引擎（中文数字分解播报）
  - 报警管理器音频集成（异步任务模式，不阻塞状态机）
  - I2S TX/RX 通道按需创建/销毁（共享 GPIO17/16）
- **关键文件**: components/drivers/audio/audio_player.c, components/drivers/audio/simple_tts.c, components/services/alarm_manager/alarm_manager.c
- **附带修复**:
  - ISSUE-030: WAV 文件头解析不兼容（固定 44 字节结构体→chunk 遍历模式，兼容 FFmpeg 生成的 78 字节头）
  - 日志降级：14 处 ESP_LOGI → ESP_LOGD（audio_player + simple_tts + voice_cmd）

---

### 2026-02-18 - 迭代 3.3: ESP-SR 语音识别

- **迭代**: Week 3 - 迭代 3.3
- **状态**: ✅ 已完成
- **主要改动**:
  - ESP-SR 组件引入（^1.4.0）+ INMP441 麦克风驱动
  - 双任务架构：vc_feed (core 0) + vc_detect (core 1)
  - 唤醒词 "Hi 乐鑫" + 4 条中文命令（救命/查询心率/查询步数/呼叫家人）
  - I2S 切换：EventGroup 信号协调 feed 任务暂停/恢复
  - 新增 model 分区 5MB
- **关键文件**: components/services/voice_cmd/, components/drivers/audio/, partitions.csv
- **附带修复**:
  - ISSUE-034: "查询时间"命令已注册但缺少响应处理，新增 tts_speak_time() 函数

---

### 2026-02-19 - 迭代 3.4: 完整报警流程联调

- **迭代**: Week 3 - 迭代 3.4
- **状态**: ✅ 已完成
- **主要改动**:
  - 报警缓存环形队列（N=16）+ BLE 重连补发任务
  - ACK_ALARM 命令对接 alarm_ack(event_id)
  - Telemetry 实时频率切换（xTaskNotifyWait，正常 480s / 实时 16s）
  - Status 特征返回真实 alarm_state
  - 删除 SPO2_WARNING 功能（ISSUE-036）
  - PRE_ALARM 播放 "pre_alarm_fall"，ALARMING 跌倒播放 "alarm_help"（ISSUE-037）
- **关键文件**: components/services/alarm_manager/, components/ble_gatt/, components/services/health_monitor/

---

### 2026-02-19~20 - Bug 修复: I2C 总线稳定性改进

- **迭代**: Bug 修复（影响迭代 1.1、1.2、1.5）
- **状态**: ✅ 已完成
- **主要改动**:
  - I2C 总线新增全局 mutex 保护 read/write，新增 lock/unlock 公开接口（ISSUE-032）
  - SH1106 从整体加锁改为逐页加锁（最大持锁 ~4ms 而非 ~30ms），防止 RTC 读取饥饿（ISSUE-033）
  - ds3231_get_time 添加 3 次重试机制，间隔 5ms（ISSUE-041）
  - Home 页 RTC 读取频率从每 500ms 降为每 10 秒，降低 I2C 竞争 20 倍（ISSUE-042）
  - RTC 读取失败时重置 s_home_last_minute 强制下次重绘
- **关键文件**: components/drivers/common/i2c_bus.c, components/drivers/sh1106/sh1106.c, components/drivers/ds3231/ds3231.c, components/ui_manager/ui_manager.c
- **附带修复**: ISSUE-032, ISSUE-033, ISSUE-041, ISSUE-042

---

### 2026-02-20 - Bug 修复: PPG 信号处理优化

- **迭代**: Bug 修复（影响迭代 2.2 健康监测服务）
- **状态**: ✅ 已完成
- **主要改动**:
  - PPG 环形缓冲区（64 样本）确保所有 FIFO 样本到达 health_monitor，0% 丢失（ISSUE-038）
  - IR/RED 双通道 IIR 带通滤波器（0.5-4Hz），隔离心率信号频段
  - 自适应峰值检测阈值（最近 2 秒峰峰值 × 0.3）替代固定阈值
  - 中值 ±30% 离群值剔除消除误检
  - IMU 运动伪影抑制，运动期间跳过峰值检测
  - AC/DC 从原始信号遍历改为增量式滤波信号累加，运动样本排除（ISSUE-039）
  - 校准公式从线性改为 Maxim 二次多项式，AC/DC 改用 float 精度（ISSUE-040）
  - 前 25 样本（1 秒）为滤波器建立期，不参与累加
  - 信号幅度校验：AC < 100 标记无效
- **关键文件**: components/services/sensor_service/sensor_service.c, components/services/health_monitor/health_monitor.c
- **附带修复**: ISSUE-038, ISSUE-039, ISSUE-040

---

### 2026-02-20 - 重构: UI 定时刷新迁移到独立任务

- **迭代**: 重构（影响迭代 1.4、1.5）
- **状态**: ✅ 已完成
- **主要改动**:
  - 删除两个 FreeRTOS 软件定时器（s_refresh_timer + s_step_refresh_timer）
  - 新增独立 ui_task（栈 4096，优先级 2），xTaskNotifyWait 500ms 超时作为快速刷新
  - 内部计数器追踪 2 分钟全量刷新（240 × 500ms）
  - Timer Service 从此仅处理轻量级回调（按键消抖、WS2812 闪烁等）
  - 宏 UI_STEP_REFRESH_INTERVAL_MS → UI_FAST_REFRESH_INTERVAL_MS
- **关键文件**: components/ui_manager/include/ui_manager.h, components/ui_manager/ui_manager.c
- **附带修复**: ISSUE-043

---

### 2026-02-20~21 - 迭代 4.1: Flutter 项目搭建 + BLE 连接

- **迭代**: Week 4 - 迭代 4.1
- **状态**: ✅ 已完成
- **主要改动**:
  - 项目基础配置: pubspec.yaml 添加 flutter_blue_plus/provider/permission_handler/intl 依赖
  - Android 配置: minSdk=23, applicationId=com.careband.app, BLE/定位权限声明
  - 数据模型: BleConnectionState/AlarmType 枚举, TelemetryData/AlarmData/DeviceStatus 数据类
  - BLE 通信层: 单例 BleManager (扫描/连接/GATT/Notify/命令), BleParser (二进制解析 Telemetry 20B/Alarm 16B/Status 3B), BleCommand (ACK/SYNC_TIME/REQUEST_REPORT/MANUAL_MEASURE + nonce 递增)
  - 状态管理: BleProvider (ChangeNotifier) 封装 BleManager streams
  - UI 页面: ScanPage (权限检查+CareBand 过滤+自动跳转+已连接设备入口), DevicePage (卡片式 Telemetry+断连自动返回), DeviceTile
  - Android 前台服务 BleKeepAliveService 保活 BLE 连接，防止系统回收进程
  - ScanPage 返回键改为 moveTaskToBack，退到后台而非销毁 Activity
  - 单元测试: 63 个测试全部通过
- **关键文件**: mobile_flutter/lib/ble/, mobile_flutter/lib/data/, mobile_flutter/lib/ui/, mobile_flutter/android/app/src/main/kotlin/com/careband/app/
- **验收状态**: ✅ 已验收
  - `flutter pub get` ✅ | `flutter analyze` ✅ | `flutter test` ✅ 63 tests passed
- **附带修复**:
  - ISSUE-flutter-001: widget_test.dart 引用旧 MyApp 类
  - ISSUE-flutter-002: ScanPage 每次数据更新重复 push DevicePage
  - ISSUE-flutter-003: 断开连接按钮未真正断开 BLE（重构 disconnect() 消除竞态）
  - ISSUE-flutter-004: 连接过程瞬态断连事件导致 _device 被清空（添加 _isConnecting 标志）
  - ISSUE-flutter-007: ScanPage 返回后显示旧扫描结果
  - ISSUE-flutter-009: 后台 BLE 连接被系统回收（前台服务保活）

---

### 2026-02-21 - 迭代 4.2: 数据展示 UI + 报警 UI

- **迭代**: Week 4 - 迭代 4.2
- **状态**: ✅ 已完成
- **主要改动**:
  - 设计系统: 主题色 (信赖蓝 #2D7DD2)、品牌色、Material 3 CardThemeData + NavigationBar
  - DevicePage 重写为三 Tab 导航壳 (IndexedStack): 数据面板 / 报警记录 / 设置
  - DashboardTab: 2×2 健康数据卡片 + fl_chart 趋势折线图 (心率/血氧/温度切换) + 手动测量按钮 (15s 倒计时)
  - AlarmTab: 报警历史列表 (倒序)，未确认项红色左边框 + ACK 按钮
  - SettingsTab: 设备信息卡、同步时间/请求上报、断开连接、版本号
  - 报警弹窗升级为 AlertDialog (AlarmDialog)，含图标/类型名/触发值/ACK 按钮，Set 追踪防重复弹出
  - BleProvider 新增 telemetryHistory (max 30) / alarmHistory (max 50)
  - DataRepository 抽象接口 + InMemoryDataRepository（为 4.3 MQTT 预留）
- **关键文件**: mobile_flutter/lib/ui/tabs/, mobile_flutter/lib/ui/widgets/, mobile_flutter/lib/data/data_repository.dart, mobile_flutter/lib/ui/device_page.dart
- **验收状态**: ✅ 已验收
  - `flutter pub get` ✅ | `flutter analyze` ✅ | `flutter test` ✅ 63 tests passed
- **附带修复**:
  - ISSUE-flutter-005: CardTheme→CardThemeData
  - ISSUE-flutter-006: unnecessary non-null assertion
  - ISSUE-flutter-008: 报警确认后 UI 未更新（ackAlarm 成功后更新本地 isAcked + notifyListeners）
  - ISSUE-flutter-010: 报警弹窗重复弹出已确认报警（Set<int> _shownAlarmIds 追踪）

---

### 2026-02-21 - 迭代 4.3: MQTT 网关 + EMQX Cloud 部署

- **迭代**: Week 4 - 迭代 4.3
- **状态**: ✅ 已完成
- **主要改动**:
  - MqttConfig 常量类：broker/port/username/password/deviceId/clientId + 5 个 Topic getter
  - MqttGateway 单例核心：TLS 连接（CA 证书 rootBundle 加载）、LWT 遗嘱消息、指数退避断线重连（2s→30s max）
  - 上行转发：Telemetry→JSON QoS0、Alarm→JSON QoS1（alarm_type 枚举→字符串映射）
  - 下行命令：订阅 cmd topic，解析 JSON 后调用 BleManager 对应方法（ack_alarm/sync_time/request_report/manual_measure）
  - 在线状态：连接成功发布 online status（retained），主动断开发布 offline status
  - BleProvider 集成：BLE connected→启动 MQTT，BLE disconnected→停止 MQTT；telemetry/alarm 监听中同步转发
  - SettingsTab 新增"云端连接"状态行（绿色已连接/红色未连接/灰色未启用）
  - pubspec.yaml 添加 mqtt_client ^10.6.0 依赖 + CA 证书 asset 声明
- **关键文件**: mobile_flutter/lib/mqtt/mqtt_config.dart, mobile_flutter/lib/mqtt/mqtt_gateway.dart, mobile_flutter/lib/data/ble_provider.dart, mobile_flutter/lib/ui/tabs/settings_tab.dart, mobile_flutter/pubspec.yaml
- **验收状态**: ✅ 已验收
  - `flutter pub get` ✅ | `flutter analyze` ✅ (0 error, 3 pre-existing warnings) | `flutter test` ✅ 63 tests passed
- **附带修复**:
  - ISSUE-flutter-011: jsonEncode 异常杀死 Stream 监听器导致后续数据不再转发，整体 try-catch 保护 + 移除 logOnce 标志

---

### 2026-02-21 - 迭代 4.2 增强: SQLite 本地存储 + Android 报警通知

- **迭代**: Week 4 - 迭代 4.2 增强
- **状态**: ✅ 已完成
- **主要改动**:
  - **SQLite 本地存储**:
    - DatabaseHelper 单例：careband.db，telemetry + alarm 两表 + 时间索引 + event_id 唯一约束
    - SqliteDataRepository 实现 DataRepository 接口，包裹 DatabaseHelper CRUD
    - 24h 自动清理：每 100 次 Telemetry 写入触发一次 cleanup
    - BleProvider 集成：构造时异步加载历史（telemetry 360 条 / alarm 100 条），BLE 数据写入同步存库
    - BLE 断连不再清空历史数据，ACK 操作同步更新 SQLite
    - SQLite 初始化失败时自动降级为纯内存模式，不影响核心 BLE 功能
  - **Android 报警通知**:
    - NotificationService 单例：alarm_channel（Importance.high, 不震动），按 AlarmType 生成中文标题和正文
    - BleProvider 添加 WidgetsBindingObserver 检测前后台状态，后台时触发系统通知
    - 通知点击跳转：pendingTabIndex 机制 + navigatorKey 导航到 DevicePage AlarmTab
    - ScanPage 追加 Permission.notification 运行时权限请求（Android 13+）
  - DataRepository 接口新增 updateAlarmAcked 方法
  - main.dart 添加 WidgetsFlutterBinding.ensureInitialized + NotificationService 初始化
- **关键文件**:
  - 新建: mobile_flutter/lib/data/database_helper.dart, mobile_flutter/lib/data/sqlite_repository.dart, mobile_flutter/lib/services/notification_service.dart
  - 修改: mobile_flutter/pubspec.yaml, mobile_flutter/lib/main.dart, mobile_flutter/lib/data/data_repository.dart, mobile_flutter/lib/data/ble_provider.dart, mobile_flutter/lib/ui/device_page.dart, mobile_flutter/lib/ui/scan_page.dart
- **验收状态**: 待验收
  - `flutter pub get` ✅ | `flutter analyze` ✅ (0 error, 3 pre-existing warnings) | `flutter test` ✅ 63 tests passed
