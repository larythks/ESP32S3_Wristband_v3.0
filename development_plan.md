# ESP32-S3 智能陪护手环 - 1个月软件开发计划

## 一、项目目标与范围

### 1.1 MVP 功能清单（必须完成）
| 功能模块 | 具体内容 |
|---------|---------|
| 环境温度监测 | DS18B20 采集、阈值报警（≥35.0°C / ≤20.0°C） |
| 心率血氧 | MAX30102 采集、心率/血氧显示、阈值报警 |
| 跌倒检测 | MPU6050 加速度分析、疑似跌倒→确认流程 |
| 计步功能 | 基于 MPU6050 的简易计步算法 |
| OLED 显示 | 日期时间、心率、血氧、步数、多页面切换 |
| 报警系统 | 状态机、声光报警、BLE 上报 |
| 语音播报 | WAV 音频播放（预置提示音） |
| 语音识别 | ESP-SR 本地关键词唤醒 |
| BLE 通信 | GATT 服务、数据上报、报警通知 |
| Flutter App | BLE 连接、数据展示、报警 UI、MQTT 网关 |
| 云端 | EMQX Cloud 免费层、Topic 规划、LWT |

### 1.2 不做清单（明确排除）
- BLE OTA 升级功能
- 多设备同时连接
- 复杂的云端数据分析/存储服务
- 手环端 WiFi 功能
- 睡眠监测功能
- GPS 定位功能
- 手环端连续健康数据持久化存储（仅允许最小报警索引 NVS，可选）
- 复杂的 UI 动画效果

### 1.3 阈值判定规则表（MVP）

| 指标 | 报警阈值（仅单点阈值） | 触发条件 | 信号质量门槛 | 常规采样 | 异常实时检测采样 |
|------|----------------------|----------|-------------|----------|------------------|
| 环境温度（DS18B20） | >=35.0°C 或 <=20.0°C | 连续 3 次有效采样越阈 | 温度跳变 >1.0°C/次判无效，需重采样 | 30 秒/次 | 16 秒/次（持续 180 秒或恢复后退出） |
| 心率（MAX30102） | >=120 bpm 或 <=45 bpm | 连续 32 秒有效样本越阈 | PPG 振幅/置信度低时仅提示"测量无效" | 480 秒/次（每次采集 15 秒取平均值，OLED 每 8 分钟更新一次，云端每 2 分钟更新） | 16 秒/次（持续 60 秒或恢复后退出） |
| 血氧（MAX30102） | <=90% ALARM / 90%-92% WARNING | 连续 32 秒有效样本越阈 | 运动伪影或信号不稳时不触发报警 | 480 秒/次（每次采集 15 秒取平均值，OLED 每 8 分钟更新，云端每 2 分钟更新） | 16 秒/次（持续 60 秒或恢复后退出） |
| 跌倒（MPU6050） | SVM+姿态满足跌倒条件 | 进入 PRE_ALARM，10-15 秒未取消升级 ALARMING | SVM+姿态联合判定有效 | 50Hz 连续采样 | 保持 50Hz（不降采样） |

**告警分级约定**:
- `INFO`: 正常数据上报
- `WARNING`: 预警事件，仅 BLE 上报，不触发本地声光
- `ALARM`: 触发本地声光和 BLE Alarm Notify，必要时上云 QoS1

### 1.4 报警判定逻辑（异常触发实时检测）

1. 系统默认低频采样（环境温度 30 秒、心率血氧 480 秒）。
2. 任一指标首次越过报警阈值后，仅对应传感器切换到实时检测模式（16 秒/次）。
3. 在实时检测窗口内满足“持续触发条件”即发布 `ALARM`；否则判定为瞬时异常并退出实时检测。
4. 实时检测窗口结束条件：达到最大持续时间或连续恢复正常。
5. 跌倒检测保持高频连续检测，不依赖低频->高频切换。
6. **心率/血氧测量流程**：每次测量（无论自动还是手动触发）均需连续采集 15 秒原始数据，取平均值后输出为最终心率/血氧值。自动测量每 8 分钟触发一次，结果同步更新到 OLED 和云端。
7. **计步显示与上报频率**：步数在 OLED 上每 500ms 刷新一次；云端上报随 Telemetry 每 2 分钟发送一次。

---

## 二、模块划分与边界表

### 2.1 ESP32 固件模块

#### components/drivers/ds18b20
| 项目 | 内容 |
|-----|------|
| 职责 | 1-Wire 协议实现、DS18B20 温度读取 |
| 不做 | 多传感器级联、ROM 搜索 |
| 对外接口 | `esp_err_t ds18b20_init(gpio_num_t pin)`<br>`esp_err_t ds18b20_read_temp(float *temp_c)` |
| 依赖 | driver/gpio |

#### components/drivers/ds3231
| 项目 | 内容 |
|-----|------|
| 职责 | I2C 通信、DS3231 RTC 时间/日期读写、BCD 编解码 |
| 不做 | 闹钟功能、温度补偿读取、32KHz 输出配置 |
| 对外接口 | `esp_err_t ds3231_init(void)`<br>`esp_err_t ds3231_get_time(ds3231_time_t *time)`<br>`esp_err_t ds3231_set_time(const ds3231_time_t *time)` |
| 依赖 | driver/i2c |

#### components/drivers/max30102
| 项目 | 内容 |
|-----|------|
| 职责 | I2C 通信、原始 RED/IR 数据读取、FIFO 管理 |
| 不做 | 心率/血氧算法（由 services 层处理） |
| 对外接口 | `esp_err_t max30102_init(void)`<br>`esp_err_t max30102_read_fifo(uint32_t *red, uint32_t *ir, uint8_t *count)` |
| 依赖 | driver/i2c |

#### components/drivers/mpu6050
| 项目 | 内容 |
|-----|------|
| 职责 | I2C 通信、加速度/陀螺仪原始数据读取、中断配置 |
| 不做 | 跌倒检测算法、计步算法（由 services 层处理） |
| 对外接口 | `esp_err_t mpu6050_init(void)`<br>`esp_err_t mpu6050_read_accel(int16_t *ax, int16_t *ay, int16_t *az)`<br>`esp_err_t mpu6050_read_gyro(int16_t *gx, int16_t *gy, int16_t *gz)` |
| 依赖 | driver/i2c |

#### components/drivers/sh1106
| 项目 | 内容 |
|-----|------|
| 职责 | I2C 通信、OLED 初始化、帧缓冲写入、基础绘图、多字号字体渲染 |
| 不做 | 复杂 UI 组件、动画 |
| 对外接口 | `esp_err_t sh1106_init(void)`<br>`void sh1106_clear(void)`<br>`void sh1106_draw_string(int x, int y, const char *str, uint8_t color)`<br>`void sh1106_draw_string_large(int x, int y, const char *str, uint8_t color)`<br>`void sh1106_draw_chinese(int x, int y, uint8_t index, uint8_t color)`<br>`void sh1106_update(void)` |
| 字体支持 | 5x7 ASCII 小字体（已有）；16x24 大数字字体（0-9 及冒号，用于时间显示）；12x12 中文字模（仅"星期一二三四五六日"9 个汉字） |
| 依赖 | driver/i2c |

#### components/drivers/audio
| 项目 | 内容 |
|-----|------|
| 职责 | I2S 配置、WAV 文件解析、音频播放、麦克风采集 |
| 不做 | 音频编解码、音效处理 |
| 对外接口 | `esp_err_t audio_player_init(void)`<br>`esp_err_t audio_play_wav(const char *path)`<br>`esp_err_t audio_mic_init(void)`<br>`esp_err_t audio_mic_read(int16_t *buffer, size_t len)` |
| 依赖 | driver/i2s, esp_partition (SPIFFS) |

#### components/drivers/ws2812
| 项目 | 内容 |
|-----|------|
| 职责 | WS2812 RGB LED 驱动、RMT 外设控制、闪烁模式管理 |
| 不做 | 复杂灯效、多 LED 级联动画 |
| 对外接口 | `esp_err_t ws2812_init(gpio_num_t pin)`<br>`esp_err_t ws2812_set_color(uint8_t r, uint8_t g, uint8_t b)`<br>`esp_err_t ws2812_start_blink(uint8_t r, uint8_t g, uint8_t b, uint32_t interval_ms)`<br>`void ws2812_stop_blink(void)` |
| 依赖 | driver/rmt, freertos/timers |

#### components/drivers/button
| 项目 | 内容 |
|-----|------|
| 职责 | GPIO 按键检测、消抖、短按/长按识别 |
| 不做 | 组合键、多击检测 |
| 对外接口 | `esp_err_t button_init(void)`<br>`void button_register_cb(button_id_t id, button_event_t event, button_cb_t cb)` |
| 依赖 | driver/gpio, freertos/timers |

#### components/services/sensor_service
| 项目 | 内容 |
|-----|------|
| 职责 | 传感器采样调度（常规/实时双模式）、滑动平均滤波、数据融合；心率/血氧每次测量采集 15 秒取平均值后输出 |
| 不做 | 直接操作硬件（通过 drivers 层） |
| 对外接口 | `esp_err_t sensor_service_init(void)`<br>`esp_err_t sensor_service_start(void)`<br>`sensor_data_t sensor_get_latest(void)` |
| 依赖 | drivers/*, freertos/task |

#### components/services/health_monitor
| 项目 | 内容 |
|-----|------|
| 职责 | 心率/血氧算法、环境温度阈值检测、异常判定、触发对应传感器实时检测 |
| 不做 | 报警触发（仅产生事件） |
| 对外接口 | `esp_err_t health_monitor_init(void)`<br>`health_status_t health_get_status(void)` |
| 依赖 | services/sensor_service, services/event_bus |

#### components/services/fall_detect
| 项目 | 内容 |
|-----|------|
| 职责 | 跌倒检测算法、疑似跌倒判定 |
| 不做 | 报警状态管理 |
| 对外接口 | `esp_err_t fall_detect_init(void)`<br>`bool fall_detect_check(void)` |
| 依赖 | services/sensor_service, services/event_bus |

#### components/services/pedometer
| 项目 | 内容 |
|-----|------|
| 职责 | 计步算法、步数统计、每日重置、每 500ms 更新 OLED 显示、每 2 分钟随 Telemetry 上报云端 |
| 不做 | 运动类型识别 |
| 对外接口 | `esp_err_t pedometer_init(void)`<br>`uint32_t pedometer_get_steps(void)`<br>`void pedometer_reset(void)` |
| 依赖 | services/sensor_service |

#### components/services/alarm_manager
| 项目 | 内容 |
|-----|------|
| 职责 | 报警状态机管理、声光控制、倒计时处理 |
| 不做 | BLE 通信（通过 ble_gatt 层） |
| 对外接口 | `esp_err_t alarm_manager_init(void)`<br>`void alarm_trigger(alarm_type_t type, alarm_data_t *data)`<br>`void alarm_cancel(void)`<br>`void alarm_ack(void)`<br>`alarm_state_t alarm_get_state(void)` |
| 依赖 | drivers/audio, services/event_bus |

#### components/services/voice_cmd
| 项目 | 内容 |
|-----|------|
| 职责 | ESP-SR 集成、关键词检测、命令映射 |
| 不做 | 语音合成 |
| 对外接口 | `esp_err_t voice_cmd_init(void)`<br>`esp_err_t voice_cmd_start(void)`<br>`void voice_cmd_register_cb(voice_cmd_cb_t cb)` |
| 依赖 | esp-sr, drivers/audio, services/event_bus |

#### components/services/event_bus
| 项目 | 内容 |
|-----|------|
| 职责 | 模块间事件发布/订阅、消息队列管理 |
| 不做 | 业务逻辑处理 |
| 对外接口 | `esp_err_t event_bus_init(void)`<br>`esp_err_t event_publish(event_type_t type, void *data)`<br>`esp_err_t event_subscribe(event_type_t type, event_handler_t handler)` |
| 依赖 | freertos/queue |

#### components/ble_gatt/ble_service
| 项目 | 内容 |
|-----|------|
| 职责 | NimBLE 初始化、GATT 服务注册、连接管理、Notify 发送 |
| 不做 | 数据解析（由 app 层处理） |
| 对外接口 | `esp_err_t ble_service_init(void)`<br>`esp_err_t ble_notify_telemetry(telemetry_t *data)`<br>`esp_err_t ble_notify_alarm(alarm_notify_t *data)`<br>`bool ble_is_connected(void)` |
| 依赖 | nimble, services/event_bus |

#### components/ui_manager
| 项目 | 内容 |
|-----|------|
| 职责 | UI 页面状态管理、页面切换、数据渲染（步数每 500ms 刷新，心率/血氧每 8 分钟刷新）；Home 页从 DS3231 获取日期时间并显示 |
| 不做 | 直接操作 OLED（通过 drivers 层） |
| 对外接口 | `esp_err_t ui_manager_init(void)`<br>`void ui_switch_page(ui_page_t page)`<br>`void ui_update(void)` |
| 依赖 | drivers/sh1106, drivers/ds3231, services/event_bus |

**Home 页面布局（128x64 OLED）**：
```
+----------------------------+
| 02/17 周六         36.5°C  |  ← 5x7 小字体，日期左对齐，温度右对齐
|                            |
|                            |
|          12:30             |  ← 16x24 大数字字体，水平居中，垂直居中
|                            |
|                            |
|                            |
+----------------------------+
```
- 第一行：日期（格式 MM/DD + 中文星期）+ 温度，均使用小字体
- 中间区域：时间 HH:MM，使用大数字字体，垂直水平居中
- 心率、血氧、步数在各自独立页面（Heart Rate 页、Steps 页）通过 SW2 切换查看

### 2.2 Flutter App 模块

#### lib/ble/
| 项目 | 内容 |
|-----|------|
| 职责 | BLE Central 扫描、连接、GATT 读写、Notify 监听 |
| 不做 | 数据业务处理 |
| 对外接口 | `BleManager` 单例类 |
| 依赖 | flutter_blue_plus |

#### lib/data/
| 项目 | 内容 |
|-----|------|
| 职责 | 数据模型定义、本地缓存（SQLite）、数据转换 |
| 不做 | 网络通信 |
| 对外接口 | `TelemetryModel`, `AlarmModel`, `LocalCache` |
| 依赖 | sqflite |

#### lib/mqtt/
| 项目 | 内容 |
|-----|------|
| 职责 | MQTT 客户端、消息发布、断线重连 |
| 不做 | BLE 通信 |
| 对外接口 | `MqttGateway` 单例类 |
| 依赖 | mqtt_client |

#### lib/ui/
| 项目 | 内容 |
|-----|------|
| 职责 | 页面 UI、报警弹窗、数据展示 |
| 不做 | 业务逻辑 |
| 对外接口 | 各 Screen/Widget |
| 依赖 | provider |

### 2.3 云端模块

#### EMQX Cloud
| 项目 | 内容 |
|-----|------|
| 职责 | MQTT Broker、消息路由、LWT 处理 |
| 不做 | 数据存储、复杂规则引擎 |
| 配置 | 免费层 Serverless 实例 |

---

## 三、接口契约定义

### 3.1 BLE GATT 服务定义

**服务 UUID**: `0000FF00-0000-1000-8000-00805F9B34FB`

| 特征名 | UUID | 属性 | 字段 | 编码 | 单位 | 触发条件 |
|-------|------|------|------|------|------|---------|
| Telemetry | `0000FF01-...` | Notify | temp, heart_rate, spo2, steps, data_valid, timestamp | Little-Endian Binary | °C×10, bpm, %, count, bitmap, Unix秒 | 常规每8分钟；任一传感器进入实时检测时每16秒 |
| Alarm | `0000FF02-...` | Notify | event_id, alarm_type, value, timestamp | Little-Endian Binary | - | 报警触发时立即 |
| Command | `0000FF03-...` | Write | cmd_type, payload | Little-Endian Binary | - | App 写入 |
| Status | `0000FF04-...` | Read | device_state, ble_conn_count, alarm_state | Little-Endian Binary | - | App 主动读取 |

**Telemetry 数据包格式 (20 bytes)**:
```
Offset  Size  Field        Type      Unit
0       2     temp         int16     °C × 10
2       1     heart_rate   uint8     bpm
3       1     spo2         uint8     %
4       4     steps        uint32    count
8       1     data_valid   uint8     bitmap
9       4     timestamp    uint32    Unix秒
13      6     reserved     -         -
```

**Alarm 数据包格式 (16 bytes)**:
```
Offset  Size  Field        Type      Description
0       4     event_id     uint32    唯一事件ID
4       1     alarm_type   uint8     1=环境温度高 2=环境温度低 3=心率高 4=心率低 5=血氧低 6=跌倒 7=手动 8=血氧预警(WARNING) 9=呼叫家人
5       2     value        int16     触发值(×10)
7       4     timestamp    uint32    Unix秒
11      4     reserved     -         -
```

**Command 类型**:
| cmd_type | 含义 | payload |
|----------|------|---------|
| 0x01 | ACK 报警 | event_id (4 bytes) + nonce (4 bytes) |
| 0x02 | 同步时间 | Unix timestamp (4 bytes) + nonce (4 bytes) |
| 0x03 | 请求立即上报 | nonce (4 bytes) |
| 0x04 | 手动测量控制 | mode (1 byte: start/stop) + duration_s (1 byte) + nonce (4 bytes) |

### 3.2 MQTT Topic 与 Payload 定义

**Topic 命名规范**: `careband/{device_id}/{message_type}`

| Topic | 方向 | QoS | Retained | 说明 |
|-------|------|-----|----------|------|
| `careband/{id}/telemetry` | 上行 | 0 | false | 定时遥测数据 |
| `careband/{id}/alarm` | 上行 | 1 | false | 报警事件 |
| `careband/{id}/status` | 上行 | 1 | true | 设备状态 |
| `careband/{id}/cmd` | 下行 | 1 | false | 云端命令 |
| `careband/{id}/lwt` | 上行 | 1 | true | LWT 遗嘱消息 |

**Telemetry Payload (JSON)**:
```json
{
  "device_id": "string",
  "timestamp": 1234567890,
  "temp": 36.5,
  "heart_rate": 72,
  "spo2": 98,
  "steps": 1234
}
```

**Alarm Payload (JSON)**:
```json
{
  "device_id": "string",
  "event_id": 12345,
  "alarm_type": "fall",
  "value": 0,
  "timestamp": 1234567890
}
```

**LWT Payload**: `{"device_id": "xxx", "online": false}`

### 3.3 App 内部事件总线定义

| 事件类型 | 数据结构 | 发布者 | 订阅者 |
|---------|---------|--------|--------|
| EVT_SENSOR_DATA | sensor_data_t | sensor_service | health_monitor, ui_manager |
| EVT_HEALTH_ALERT | health_alert_t | health_monitor | alarm_manager |
| EVT_FALL_DETECTED | fall_event_t | fall_detect | alarm_manager |
| EVT_ALARM_STATE | alarm_state_t | alarm_manager | ble_service, ui_manager |
| EVT_VOICE_CMD | voice_cmd_t | voice_cmd | app_main |
| EVT_BUTTON | button_event_t | button | ui_manager, alarm_manager |
| EVT_BLE_CONN | ble_conn_t | ble_service | ui_manager |

### 3.4 BLE 安全最小策略（Week 2 必做）

| 项目 | 策略 |
|------|------|
| 配对与绑定 | 启用配对+绑定，仅允许 1 台手机绑定为 trusted central |
| 加密要求 | Command 特征写入必须在加密链路上完成（未加密直接拒绝） |
| 命令权限 | `ACK/时间同步/手动测量控制` 仅 bonded 设备可写 |
| 重放防护 | 所有命令携带 `nonce`，设备侧维护 `last_nonce`，小于等于历史值则拒绝 |
| 失效处理 | 绑定被清除后必须重新配对，旧会话命令全部拒绝 |

### 3.5 报警缓存与补发策略

| 项目 | 方案 |
|------|------|
| 缓存位置 | RAM 环形队列（默认 `N=16` 条） |
| 单条内容 | event_id、alarm_type、value、timestamp、retry_count、acked |
| 补发时机 | BLE 重连后先补发未 ACK 告警，再恢复实时上报 |
| 重发策略 | 固定间隔 2 秒，最多 5 次；超过次数保留待手动查询 |
| 满队列策略 | 丢弃最旧未 ACK 记录并打印告警日志 |
| 掉电行为 | RAM 缓存丢失（MVP 可接受） |
| 最小 NVS 持久化 | 可选：仅持久化最近 4 条未 ACK 告警索引（<=512B），默认关闭 |

---

## 四、里程碑总览（4周甘特式）

```
Week 1: 基础驱动层
├─ 迭代1.1: 项目结构搭建 + I2C 总线验证
├─ 迭代1.2: MPU6050 驱动 + OLED 驱动
├─ 迭代1.3: MAX30102 驱动 + DS18B20 驱动
├─ 迭代1.4: 按键驱动 + 基础 UI 框架
└─ 迭代1.5: DS3231 RTC 驱动 + 大字体扩展 + Home 页面重设计

Week 2: 服务层 + BLE + 安全 + 最小闭环
├─ 迭代2.1: 事件总线 + 传感器采样服务
├─ 迭代2.2: 健康监测服务 + 计步算法
├─ 迭代2.3: 跌倒检测算法
├─ 迭代2.4: BLE GATT 服务 + 数据上报
└─ 迭代2.5: BLE 安全子任务 + 最小端到端切片

Week 3: 报警系统 + 语音
├─ 迭代3.1: 报警状态机 + 声光控制
├─ 迭代3.2: I2S 音频播放
├─ 迭代3.3: ESP-SR 语音识别集成
└─ 迭代3.4: 完整报警流程联调（在最小闭环基础上扩展）

Week 4: Flutter App + 云端 + 联调
├─ 迭代4.1: Flutter 项目搭建 + BLE 连接
├─ 迭代4.2: 数据展示 UI + 报警 UI
├─ 迭代4.3: MQTT 网关 + EMQX Cloud 部署
└─ 迭代4.4: 端到端联调 + 问题修复
```

---

## 五、逐迭代计划

### Week 1: 基础驱动层

#### 迭代 1.1: 项目结构搭建 + I2C 总线验证

**目标**: 建立组件化项目结构，验证 I2C 总线能扫描到所有设备

**变更范围**:
- 新建 `components/` 目录结构
- 新建 `components/drivers/common/` (I2C 封装)
- 修改 `main/CMakeLists.txt`

**步骤** (每步约30分钟):
1. 创建 components 目录结构和 CMakeLists.txt 模板
2. 实现 I2C 主机初始化封装 (`i2c_master_init`)
3. 实现 I2C 设备扫描函数，打印发现的地址
4. 编译烧录，验证日志输出

**产出物**:
- `components/drivers/common/i2c_bus.h/.c`
- I2C 扫描日志截图

**验收方法**:
```
串口日志应显示:
I2C device found at 0x69 (MPU6050)
I2C device found at 0x57 (MAX30102)
I2C device found at 0x3C (SH1106)
I2C device found at 0x68 (DS3231)
```

**回滚策略**: 删除 components 目录，恢复原始 main.c

**用户验证清单**:
- [ ] 编译无错误
- [ ] 烧录成功
- [ ] 串口看到 4 个 I2C 设备地址
- [ ] 地址与预期一致 (0x69, 0x57, 0x3C, 0x68)

---

#### 迭代 1.2: MPU6050 驱动 + OLED 驱动

**目标**: MPU6050 能读取加速度数据，OLED 能显示文字

**变更范围**:
- 新建 `components/drivers/mpu6050/`
- 新建 `components/drivers/sh1106/`

**步骤**:
1. 实现 MPU6050 初始化（WHO_AM_I 验证）
2. 实现 MPU6050 加速度读取，串口打印原始值
3. 实现 SH1106 初始化序列
4. 实现 SH1106 清屏和字符显示
5. 在 OLED 上显示 "Hello" 和加速度值

**产出物**:
- `components/drivers/mpu6050/mpu6050.h/.c`
- `components/drivers/sh1106/sh1106.h/.c`
- `components/drivers/sh1106/font.h` (5x7 字体)

**验收方法**:
- 串口打印 `WHO_AM_I = 0x70`
- 串口持续打印加速度 X/Y/Z 值
- OLED 显示 "Hello" 文字
- 晃动手环，加速度值变化

**回滚策略**: 删除对应 component 目录

**用户验证清单**:
- [ ] MPU6050 WHO_AM_I 返回 0x70
- [ ] 加速度数据随晃动变化
- [ ] OLED 点亮并显示文字

---

#### 迭代 1.3: MAX30102 驱动 + DS18B20 驱动

**目标**: MAX30102 能读取 RED/IR 原始数据，DS18B20 能读取温度

**变更范围**:
- 新建 `components/drivers/max30102/`
- 新建 `components/drivers/ds18b20/`

**步骤**:
1. 实现 MAX30102 初始化（Part ID 验证）
2. 配置 MAX30102 采样参数（100Hz, LED 电流）
3. 实现 FIFO 读取，串口打印 RED/IR 值
4. 实现 1-Wire 协议基础（复位、读写位）
5. 实现 DS18B20 温度读取

**产出物**:
- `components/drivers/max30102/max30102.h/.c`
- `components/drivers/ds18b20/ds18b20.h/.c`
- `components/drivers/ds18b20/onewire.h/.c`

**验收方法**:
- 串口打印 MAX30102 Part ID = 0x15
- 手指放上去，RED/IR 值有明显变化
- DS18B20 读取温度在 20-40°C 范围内

**回滚策略**: 删除对应 component 目录

**用户验证清单**:
- [ ] MAX30102 Part ID 正确
- [ ] 手指触碰时 RED/IR 数据变化
- [ ] DS18B20 温度读数合理

---

#### 迭代 1.4: 按键驱动 + 基础 UI 框架 + SW2 手动测量流程

**目标**: 按键能检测短按/长按，UI 能切换页面，SW2 长按进入/退出手动测量

**变更范围**:
- 新建 `components/drivers/button/`
- 新建 `components/app/ui_manager/`

**步骤**:
1. 实现 GPIO 按键初始化（SW1=GPIO7, SW2=GPIO6）
2. 实现消抖和短按/长按检测（长按>1秒）
3. 设计 UI 页面枚举（主页、心率页、步数页、手动测量页）
4. 实现页面切换逻辑
5. SW2 短按切换页面，OLED 显示当前页面名
6. SW2 长按启动心率/血氧/环境温度手动测量，再次长按中断；测量结束自动回到常规采样

**产出物**:
- `components/drivers/button/button.h/.c`
- `components/app/ui_manager/ui_manager.h/.c`

**验收方法**:
- 短按 SW2，OLED 页面切换
- 长按 SW2，OLED 进入手动测量页并显示采样中状态
- 手动测量期间再次长按 SW2，测量被中断并退出测量页
- 短按 SW1，串口打印 "Alarm button"

**回滚策略**: 删除对应 component 目录

**用户验证清单**:
- [ ] SW2 短按切换页面
- [ ] SW2 长按可启动手动测量
- [ ] SW2 再次长按可中断手动测量
- [ ] SW1 按下有响应
- [ ] 无按键抖动误触发

---

#### 迭代 1.5: DS3231 RTC 驱动 + 大字体扩展 + Home 页面重设计

**目标**: DS3231 能读取日期时间，OLED 支持大字体和中文星期显示，Home 页面展示日期、时间、温度

**变更范围**:
- 新建 `components/drivers/ds3231/`
- 修改 `components/drivers/sh1106/`（添加大字体和中文字模）
- 修改 `components/ui_manager/`（Home 页面布局重设计）

**步骤**:
1. 实现 DS3231 I2C 初始化，读取设备验证通信正常
2. 实现 DS3231 时间/日期读取（BCD 解码）和设置功能
3. 在 SH1106 驱动中新增 16x24 大数字字体（0-9 及冒号 `:`，共 11 个字符）
4. 在 SH1106 驱动中新增 12x12 中文字模（仅"星期一二三四五六日"9 个汉字）
5. 新增 `sh1106_draw_string_large()` 和 `sh1106_draw_chinese()` 绘图函数
6. 重设计 Home 页面布局：第一行小字体显示日期（MM/DD 周X）和温度，中间大字体显示时间（HH:MM）

**Home 页面布局**:
```
+----------------------------+  128x64 OLED
| 02/17 星期六       36.5°C  |  ← 5x7 小字体 (y=0)
|                            |
|                            |
|          12:30             |  ← 16x24 大数字 (y=20, 居中)
|                            |
|                            |
|                            |
+----------------------------+
```

**产出物**:
- `components/drivers/ds3231/ds3231.h/.c`
- `components/drivers/sh1106/font_large.h`（16x24 大数字字模）
- `components/drivers/sh1106/font_cn.h`（12x12 中文星期字模）
- 修改后的 `sh1106.h/.c`（新增大字体和中文绘图函数）
- 修改后的 `ui_manager.c`（Home 页面布局）

**验收方法**:
- 串口打印 DS3231 读取的日期时间，格式正确
- I2C 扫描新增 0x68 (DS3231)
- Home 页面第一行显示日期（如 `02/17 周六`）和温度（如 `36.5°C`）
- Home 页面中间区域以大字体显示时间（如 `12:30`）
- 时间每分钟自动更新

**回滚策略**: 删除 ds3231 component 目录，恢复 sh1106 和 ui_manager 原有代码

**用户验证清单**:
- [ ] DS3231 I2C 通信正常（0x68 地址可扫描到）
- [ ] 日期时间读取正确
- [ ] 大字体时间显示清晰
- [ ] 中文星期显示正确
- [ ] 温度显示正确
- [ ] Home 页面布局符合设计

---

### Week 2: 服务层 + BLE + 安全 + 最小闭环

#### 迭代 2.1: 事件总线 + 传感器采样服务

**目标**: 建立模块间通信机制，统一传感器采样调度

**变更范围**:
- 新建 `components/services/event_bus/`
- 新建 `components/services/sensor_service/`

**步骤**:
1. 实现事件总线（FreeRTOS Queue）
2. 定义事件类型枚举和数据结构
3. 实现传感器采样任务（常规采样 + 对应传感器实时采样切换）
4. 集成所有传感器驱动到采样服务
5. 通过事件总线发布传感器数据与采样模式变更事件

**产出物**:
- `components/services/event_bus/event_bus.h/.c`
- `components/services/sensor_service/sensor_service.h/.c`

**验收方法**:
- 串口输出当前采样模式（NORMAL/REALTIME）与对应传感器
- 数据格式：`[SENSOR] temp=36.5 hr=0 spo2=0 ax=123 ay=456 az=789`
- 人工制造单指标越阈后，仅该指标进入实时采样（16秒/次）
- 异常消失后自动回到常规采样

**用户验证清单**:
- [ ] 传感器数据定时输出
- [ ] 各传感器数据正常
- [ ] 越阈后仅对应传感器切换实时检测
- [ ] 恢复正常后可自动退出实时检测

---

#### 迭代 2.2: 健康监测服务 + 计步算法

**目标**: 实现心率/血氧计算、环境温度阈值检测、计步功能

**变更范围**:
- 新建 `components/services/health_monitor/`
- 新建 `components/services/pedometer/`

**步骤**:
1. 实现心率算法（峰值检测法）并增加信号有效性判断
2. 实现血氧算法（R 值计算）并增加稳定性判断（运动伪影抑制）
3. **实现 15 秒测量窗口平均值机制**：每次心率/血氧测量（自动或手动）启动后连续采集 15 秒原始数据，计算平均值作为最终输出；自动测量每 8 分钟触发一次
4. 实现环境温度滑动平均 + 连续触发判定（连续 3 次有效样本）
5. 实现单点报警阈值判定（无区间预警）与 ALARM 事件发布
6. 实现计步算法（加速度峰值检测），**步数每 500ms 更新到 OLED，每 2 分钟随 Telemetry 上报云端**
7. UI 显示心率、血氧、步数，并显示"测量无效"状态；**心率/血氧每 8 分钟刷新，步数每 500ms 刷新**
8. 首次越阈时，触发对应传感器进入实时检测窗口并执行二次确认

**产出物**:
- `components/services/health_monitor/health_monitor.h/.c`
- `components/services/pedometer/pedometer.h/.c`

**验收方法**:
- 手指放上，等待 15 秒测量窗口完成后，OLED 显示心率 60-100 bpm
- 血氧显示 95-99%
- 走动时步数增加，OLED 每 500ms 刷新步数
- 心率/血氧每 8 分钟自动测量一次（15 秒采集后更新 OLED）
- 手动触发测量同样采集 15 秒后输出平均值
- 遮挡/松动导致信号质量低时，不触发报警，仅提示"测量无效"
- 连续阈值触发满足规则后，才发布 ALARM 事件
- 首次越阈后可观察到对应传感器采样频率提升到实时模式

**用户验证清单**:
- [ ] 心率数值合理（15 秒采集后输出平均值）
- [ ] 血氧数值合理（15 秒采集后输出平均值）
- [ ] 自动测量每 8 分钟触发一次，OLED 和云端同步更新
- [ ] 手动测量同样采集 15 秒取平均后输出
- [ ] 步数 OLED 每 500ms 刷新
- [ ] 步数云端每 2 分钟上报
- [ ] 信号质量低时不误触发报警
- [ ] 阈值满足持续条件后才报警
- [ ] 未使用任何区间预警阈值

---

#### 迭代 2.3: 跌倒检测算法

**目标**: 实现跌倒检测，召回率≥85%，误报≤2次/天

**变更范围**:
- 新建 `components/services/fall_detect/`

**步骤**:
1. 实现加速度矢量幅值计算 (SVM)
2. 实现跌倒特征检测（冲击+静止）
3. 添加姿态判断（躺倒检测）
4. 实现疑似跌倒事件发布
5. 测试和参数调优

**量化测试方案**（简化版）:
- 样本数量：模拟跌倒 >=30 次（前跌/后跌/侧跌，各 >=10）
- 对照样本：日常动作 >=50 段（走路、坐下、起身、弯腰）
- 统计口径：
  - 召回率 `Recall = TP / (TP + FN)`
  - 误报率 `FalseAlarmPerDay = FP / 测试总时长(天)`
  - 响应时间 `Tresp = t_detect - t_impact`
- 通过线：
  - Recall >= 85%
  - FalseAlarmPerDay <= 2
  - 中位响应时间 <= 2 秒（P95 <= 3 秒）
- 算法参数可调，便于后续优化

**产出物**:
- `components/services/fall_detect/fall_detect.h/.c`
- `test_reports/fall_detect_metrics.md`（含混淆矩阵、参数版本、原始统计）

**验收方法**:
- 模拟跌倒动作，串口打印 "Fall detected"
- 正常走动不触发误报
- 输出量化测试报告并满足通过线

**用户验证清单**:
- [ ] 模拟跌倒能检测到
- [ ] 正常活动无误报
- [ ] 召回率 >= 85%
- [ ] 误报率 <= 2 次/天
- [ ] 响应时间中位数 <= 2 秒

---

#### 迭代 2.4: BLE GATT 服务 + 数据上报

**目标**: 手机能扫描连接手环，接收 Telemetry 数据

**变更范围**:
- 新建 `components/ble_gatt/`

**步骤**:
1. 初始化 NimBLE 协议栈
2. **修改设备名称为 "CareBand"**（通过 `ble_svc_gap_device_name_set()` 或 menuconfig）
3. 定义 GATT 服务和特征
4. 实现广播和连接回调
5. 实现 Telemetry Notify 发送
6. 用 nRF Connect 测试连接和数据接收

**产出物**:
- `components/ble_gatt/ble_service.h/.c`
- `components/ble_gatt/ble_gatt_defs.h`

**验收方法**:
- nRF Connect 能扫描到 "CareBand"
- 连接后能订阅 Telemetry 特征
- 每 8 分钟收到数据包

**用户验证清单**:
- [ ] 手机能扫描到设备
- [ ] 能成功连接
- [ ] 能接收 Notify 数据

---

#### 迭代 2.5: BLE 安全子任务 + 最小端到端切片

**目标**: 在 Week 2 完成“安全可用”的最小闭环，提前暴露联调风险

**变更范围**:
- 修改 `components/ble_gatt/`（安全参数、命令权限、nonce 校验）
- 新建 `mobile_flutter/` 最小网关骨架（仅 BLE 连接 + Alarm 上云）

**步骤**:
1. 开启 BLE 配对/绑定流程，仅保留 1 台 trusted central
2. Command 特征写入权限改为“仅加密 + 已绑定设备”
3. 增加 `nonce` 重放防护（命令序号回退即拒绝）
4. 建立最小 Flutter 网关页：扫描、连接、订阅 Alarm、转发 MQTT
5. 执行最小切片联调：SW1 报警 -> BLE Alarm Notify -> App 弹窗 -> MQTT `careband/{id}/alarm`

**产出物**:
- `components/ble_gatt/ble_security.h/.c`（或并入 `ble_service.c`）
- `mobile_flutter/lib/minimal_gateway/`（最小闭环代码）

**验收方法**:
- 未配对设备无法写 Command 特征
- 重放旧 nonce 命令被拒绝并记录日志
- 最小闭环在同一测试日可重复成功 >=10 次

**用户验证清单**:
- [ ] 仅 1 台绑定设备可控制手环
- [ ] 命令重放被拦截
- [ ] SW1 报警可在 App 弹窗并成功上云

---

### Week 3: 报警系统 + 语音

#### 迭代 3.1: 报警状态机 + 声光控制

**目标**: 实现完整报警状态机，WS2812 灯光控制

**变更范围**:
- 新建 `components/services/alarm_manager/`
- 新建 `components/drivers/ws2812/`

**步骤**:
1. 定义报警状态枚举和转换规则
2. 实现状态机核心逻辑
3. 实现 PRE_ALARM 倒计时（跌倒专用）
4. 集成健康监测和跌倒检测事件
5. 实现 WS2812 驱动（RMT 外设，GPIO48）

**产出物**:
- `components/services/alarm_manager/alarm_manager.h/.c`

**验收方法**:
- 模拟跌倒 → PRE_ALARM → 15秒后 ALARMING
- 按 SW1 取消 PRE_ALARM
- ALARMING 时 WS2812 红色闪烁

**用户验证清单**:
- [ ] 跌倒触发 PRE_ALARM
- [ ] 倒计时可取消
- [ ] ALARMING 时 WS2812 红色闪烁

---

#### 迭代 3.2: I2S 音频播放

**目标**: 能播放预置 WAV 语音提示（固定语句 + 数字拼接 TTS）

**变更范围**:
- 新建 `components/drivers/audio/`
- 配置 SPIFFS 分区存储音频文件
- 准备音频资源 `spiffs_data/`

**步骤**:
1. **音频资源准备**：
   - 使用 `tools/generate_audio.py` 生成 WAV 文件（微软晓晓 TTS）。
   - 包含：报警语、反馈语、数字拼接片段（0-9, 10, 100, 单位等）。
2. **I2S 驱动开发**：
   - 配置 MAX98357A (BCLK=GPIO17, LRCK=GPIO16, DIN=GPIO18)。
   - 实现 DMA 缓冲区与 `i2s_write` 调用。
3. **音频播放器实现**：
   - 实现 `wav_player`：解析 WAV 头，读取 SPIFFS 文件流。
   - 实现基础 TTS 引擎：支持数字拼接播放（如 "当前心率" + "七" + "十二" + "次每分钟"）。
4. **集成测试**：
   - 播放报警音 "我需要帮助"。
   - 播放测试数值 "心率 75"。

**产出物**:
- `components/drivers/audio/audio_player.h/.c`
- `components/drivers/audio/wav_decoder.h/.c`
- `components/drivers/audio/simple_tts.h/.c`
- `spiffs_data/*.wav`

**验收方法**:
- 喇叭播放清晰的语音提示。
- 数字拼接播放连贯、无明显断音。

**用户验证清单**:
- [ ] 报警语音播放正常。
- [ ] "当前心率 75 次每分钟" 播放正常。

---

#### 迭代 3.3: ESP-SR 语音识别集成

**目标**: 实现本地关键词唤醒（求救、查询心率等）与语音交互

**变更范围**:
- 新建 `components/services/voice_cmd/`
- 配置 I2S 输入（INMP441）
- 引入 `esp-sr` 组件

**步骤**:
1. **麦克风驱动**：
   - 配置 INMP441 (SCK=GPIO17, WS=GPIO16, SD=GPIO15)。
   - 验证录音数据有效性。
2. **ESP-SR 集成**：
   - 配置唤醒词 "Hi 乐鑫" (wnet)。
   - 配置命令词 (multinet)："救命"、"查询心率"、"查询步数"、"呼叫家人"。
3. **语音交互逻辑**：
   - 识别到命令后，触发对应的音频播放反馈。
   - 例：识别 "查询心率" -> 播放 "当前心率 75 次每分钟" (需结合传感器数据)。

**产出物**:
- `components/services/voice_cmd/voice_recognition.h/.c`
- 语音命令模型数据

**验收方法**:
- 距离 50cm 内唤醒成功率 > 80%。
- 命令词识别准确，并能触发语音反馈。

**ESP-SR 配置细节**:
- 唤醒词模型：使用内置 "Hi 乐鑫"（wn9_hilexin）
- 内存预算：约 300-400KB PSRAM
- 命令词列表：
  - "救命" → 触发手动报警
  - "查询心率" → 语音播报当前心率
  - "查询步数" → 语音播报当前步数
  - "呼叫家人" → 通过 BLE/MQTT 发送呼叫通知给家属 App

**资源互斥机制**:
- 语音播放时暂停语音识别
- 播放完成后自动恢复识别
- 使用信号量控制 I2S 资源访问

**步骤**:
1. 配置 I2S 输入（麦克风）
2. 集成 ESP-SR 组件
3. 配置唤醒词模型（wn9_hilexin）
4. 实现语音命令回调
5. 实现 I2S 资源互斥机制
6. 测试 "Hi 乐鑫" 唤醒 + 命令词识别

**产出物**:
- `components/services/voice_cmd/voice_cmd.h/.c`
- ESP-SR 模型配置

**验收方法**:
- 说 "Hi 乐鑫"，串口打印唤醒成功
- 唤醒后说 "求救"，触发报警流程
- 识别率 > 80%

**用户验证清单**:
- [ ] 麦克风能采集声音
- [ ] "Hi 乐鑫" 唤醒词能识别
- [ ] 命令词能正确响应

---

#### 迭代 3.4: 完整报警流程联调

**目标**: 在 Week2 最小闭环基础上，扩展为完整报警流程（含语音与补发）

**变更范围**:
- 修改 `alarm_manager` 集成音频
- 修改 `ble_service` 添加 Alarm Notify

**步骤**:
1. 报警时循环播放语音
2. 实现 BLE Alarm 特征 Notify
3. 按 3.5 策略实现断连缓存和重连补发（RAM N=16，可选最小 NVS）
4. 端到端测试完整流程
5. 修复发现的问题

**验收方法**:
- 跌倒 → 语音提示 → 红灯 → 手机收到报警
- 断开 BLE 后重连，补发报警

**用户验证清单**:
- [ ] 报警时语音播放
- [ ] 手机收到报警通知
- [ ] 断连重连后补发

---

### Week 4: Flutter App + 云端 + 联调

#### 迭代 4.1: Flutter 项目搭建 + BLE 连接

**目标**: 在 Week2 最小骨架基础上，完善 Flutter App 的 BLE 能力

**变更范围**:
- 完善 `mobile_flutter/` 目录结构
- 补全 Flutter 工程配置与状态管理

**步骤**:
1. 基于 Week2 最小网关工程补全依赖与目录分层
2. 实现 BLE 扫描页面
3. 实现设备连接逻辑
4. 实现 GATT 服务发现
5. 订阅 Telemetry 特征

**产出物**:
- `mobile_flutter/lib/ble/ble_manager.dart`
- `mobile_flutter/lib/ui/scan_page.dart`

**验收方法**:
- App 能扫描到 "CareBand"
- 点击连接成功
- 能接收 Telemetry 数据

**用户验证清单**:
- [ ] App 能扫描设备
- [ ] 能连接手环
- [ ] 能接收数据

---

#### 迭代 4.2: 数据展示 UI + 报警 UI

**目标**: App 显示健康数据，报警时弹窗提醒

**变更范围**:
- `mobile_flutter/lib/ui/`
- `mobile_flutter/lib/data/`

**步骤**:
1. 设计数据模型类
2. 实现主页数据展示 UI
3. 实现报警弹窗组件
4. 订阅 Alarm 特征
5. 报警时弹窗 + 震动 + 铃声

**产出物**:
- `mobile_flutter/lib/ui/home_page.dart`
- `mobile_flutter/lib/ui/alarm_dialog.dart`
- `mobile_flutter/lib/data/models.dart`

**验收方法**:
- 主页显示心率、血氧、步数、环境温度
- 手环报警时 App 弹窗

**用户验证清单**:
- [ ] 数据正确显示
- [ ] 报警弹窗正常

---

#### 迭代 4.3: MQTT 网关 + EMQX Cloud 部署

**目标**: App 作为网关转发数据到云端

**变更范围**:
- `mobile_flutter/lib/mqtt/`
- EMQX Cloud 配置

**步骤**:
1. 实现 MQTT 客户端连接
2. 实现 Telemetry/Alarm 消息转发
3. 配置 LWT 遗嘱消息

**产出物**:
- `mobile_flutter/lib/mqtt/mqtt_gateway.dart`
- EMQX Cloud 配置文档

**验收方法**:
- MQTT Explorer 能看到上报的数据
- 设备离线时 LWT 消息发布

**用户验证清单**:
- [ ] MQTT 连接成功
- [ ] 云端能收到数据

---

#### 迭代 4.4: 端到端联调 + 问题修复

**目标**: 完整系统联调，修复所有问题

**变更范围**:
- 全部模块

**步骤**:
1. 完整流程测试（传感器→显示→BLE→App→云端）
2. 报警流程测试（各类报警触发→通知→ACK）
3. 异常场景测试（断连、重连）
4. 性能和稳定性测试
5. 问题修复和优化

**验收方法**:
- 连续运行 2 小时无崩溃
- 所有功能正常工作

**用户验证清单**:
- [ ] 传感器数据正常
- [ ] 报警流程完整
- [ ] BLE 连接稳定
- [ ] 云端数据正常

---

## 六、风险清单与对策

| 风险 | 影响 | 对策 |
|------|------|------|
| **FreeRTOS HZ=1000 功耗增加** | 待机电流增大 | 1. 使用 Light Sleep 模式<br>2. 降低非关键任务频率<br>3. 监控实际功耗 |
| **BLE 连接不稳定** | 数据丢失、报警延迟 | 1. 实现断连重连机制<br>2. 报警缓存队列<br>3. 连接参数优化 |
| **BLE 未授权写入/重放命令** | 误 ACK、误控制、状态错乱 | 1. 配对绑定白名单<br>2. Command 写权限加密+绑定<br>3. nonce 重放防护 |
| **ESP-SR 内存占用大** | 系统内存不足 | 1. 使用 PSRAM<br>2. 精简唤醒词数量<br>3. 动态加载模型 |
| **I2S 麦克风/喇叭共用引脚** | 资源冲突 | 1. 分时复用<br>2. 播放时暂停识别 |
| **MAX30102 测量不准** | 心率血氧误差大 | 1. 信号质量检测<br>2. 提示佩戴调整<br>3. 多次采样平均 |
| **跌倒检测误报** | 用户体验差 | 1. 多特征融合<br>2. PRE_ALARM 确认机制<br>3. 参数可调 |
| **Flutter 后台 BLE 限制** | iOS 后台断连 | 1. 使用后台模式<br>2. 定期唤醒<br>3. 提示用户保持前台 |
| **MQTT 断线重连** | 数据丢失 | 1. 本地缓存<br>2. 自动重连<br>3. QoS 1 保证 |
| **分区预算不足** | ESP-SR/WAV 资源无法落地 | 1. 提前落地 `partitions.csv`<br>2. 每周执行 size 报告<br>3. 超线即调小资源包 |

---

## 七、交付清单

### 7.1 代码结构
```
ESP32S3_Wristband_v3.0/
├── components/
│   ├── drivers/
│   │   ├── common/          # I2C 总线封装
│   │   ├── ds18b20/         # 环境温度传感器
│   │   ├── ds3231/          # RTC时钟模块
│   │   ├── max30102/        # 心率血氧
│   │   ├── mpu6050/         # 6轴IMU
│   │   ├── sh1106/          # OLED显示
│   │   ├── audio/           # I2S音频
│   │   ├── ws2812/          # RGB报警灯
│   │   └── button/          # 按键
│   ├── services/
│   │   ├── event_bus/       # 事件总线
│   │   ├── sensor_service/  # 采样调度
│   │   ├── health_monitor/  # 健康监测
│   │   ├── pedometer/       # 计步
│   │   ├── fall_detect/     # 跌倒检测
│   │   ├── alarm_manager/   # 报警管理
│   │   └── voice_cmd/       # 语音命令
│   ├── ble_gatt/            # BLE服务
│   └── ui_manager/          # UI管理
├── test_reports/
│   └── fall_detect_metrics.md  # 跌倒检测量化报告
├── main/
│   └── main.c               # 入口
├── partitions.csv           # 分区表
└── sdkconfig                # 配置

mobile_flutter/
├── lib/
│   ├── ble/                 # BLE管理
│   ├── data/                # 数据模型
│   ├── mqtt/                # MQTT网关
│   ├── minimal_gateway/     # Week2最小闭环
│   └── ui/                  # 界面
└── pubspec.yaml
```

### 7.2 文档清单
- 接口文档（BLE GATT + MQTT）
- 测试记录模板
- 跌倒检测量化报告（Recall/误报率/响应时间）
- 演示脚本

---

## 八、前置条件与环境检查

### 8.1 开发环境要求
- ESP-IDF v5.2.3 已安装并配置
- Flutter SDK 已安装
- Android Studio / VS Code
- nRF Connect App（用于 BLE 调试）
- MQTT Explorer（用于云端调试）

### 8.2 硬件检查点
- ESP32-S3 开发板正常工作
- 所有传感器已焊接/连接
- I2C 设备地址确认（0x69, 0x57, 0x3C, 0x68）
- 按键和 LED 连接正确

### 8.3 sdkconfig 关键配置确认
```
CONFIG_ESPTOOLPY_FLASHSIZE="16MB"     ✓
CONFIG_ESP32S3_DEFAULT_CPU_FREQ_MHZ=240  ✓
CONFIG_FREERTOS_HZ=1000               ✓
CONFIG_BT_NIMBLE_ENABLED=y            ✓
CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=4096  ✓
CONFIG_ESP_MAIN_TASK_STACK_SIZE=3584   ✓
CONFIG_SPIRAM=y                        ✓
CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16384 ✓
CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=32768 ✓
```

### 8.4 分区/内存预算表（基于当前 sdkconfig 规划，不修改 sdkconfig）

| 资源项 | 当前配置基线（来自 sdkconfig） | 规划预算 | 验收方式 |
|------|----------------------------|--------|--------|
| Flash 总容量 | 16MB (`CONFIG_ESPTOOLPY_FLASHSIZE="16MB"`) | 固定 16MB | `idf.py size` + 烧录验证 |
| 分区表模式 | single app + custom filename | 新建 `partitions.csv`，保证 app + SPIFFS + NVS 足够 | `idf.py partition-table` 校验 |
| APP 分区 | 单 app 模式 | APP 可用空间建议 >= 6MB（含 ESP-SR） | `idf.py size-components` |
| SPIFFS 分区 | 未落地 | 2MB-4MB（WAV 语音资源） | 文件系统写入/读取回归 |
| NVS 分区 | 默认启用 | 24KB（参数）+ 可选 <=512B 告警索引 | NVS 读写压力测试 |

**partitions.csv 具体方案**（迭代 3.2 前创建）：
```csv
# ESP32-S3 CareBand Partition Table
# Name,   Type, SubType, Offset,   Size,     Flags
nvs,      data, nvs,     0x9000,   0x6000,
phy_init, data, phy,     0xf000,   0x1000,
factory,  app,  factory, 0x10000,  0x600000,
storage,  data, spiffs,  0x610000, 0x400000,
```

| 分区 | 大小 | 用途 |
|-----|------|------|
| nvs | 24KB | 系统参数、报警索引 |
| phy_init | 4KB | PHY 校准数据 |
| factory | 6MB | 应用程序（含 ESP-SR） |
| storage | 4MB | SPIFFS（WAV 语音文件） |
| PSRAM | 启用 (`CONFIG_SPIRAM=y`) | ESP-SR 模型和大缓冲优先放 PSRAM，预留 >=4MB 空闲 | 启动后打印 `heap_caps_get_free_size(MALLOC_CAP_SPIRAM)` |
| 内部 RAM 预留 | `MALLOC_ALWAYSINTERNAL=16384`、`RESERVE_INTERNAL=32768` | 内部 RAM 空闲 >= 180KB（系统稳定线） | 启动后 heap 统计 |
| NimBLE 任务栈 | 4096 | 高水位剩余 >= 1024 bytes | `uxTaskGetStackHighWaterMark` |
| Main 任务栈 | 3584 | 高水位剩余 >= 1024 bytes | `uxTaskGetStackHighWaterMark` |
| 业务任务栈 | 未固定 | 各核心任务高水位剩余 >= 768 bytes | 运行 2h 压测后采样 |

**内存验收清单**:
- [ ] 引入 ESP-SR + WAV 播放后，系统可稳定运行 2 小时
- [ ] 所有关键任务栈高水位满足预算红线
- [ ] PSRAM/内部 RAM 不出现持续下降（无明显泄漏）

### 8.5 硬件引脚分配表

| 外设 | 接口 | GPIO | 备注 |
|------|------|------|------|
| DS18B20 | 1-Wire | DQ=GPIO4 | 环境温度传感器 |
| INMP441 | I2S-IN | SD=GPIO15, SCK=GPIO17, WS=GPIO16 | 麦克风 |
| MAX98357A | I2S-OUT | LRC=GPIO16, BCLK=GPIO17, DIN=GPIO18 | 扬声器 |
| MPU6050 | I2C | SDA=GPIO8, SCL=GPIO9, INT=GPIO1 | 6轴IMU |
| MAX30102 | I2C | SDA=GPIO8, SCL=GPIO9, INT=GPIO2 | 心率血氧 |
| SH1106 OLED | I2C | SDA=GPIO8, SCL=GPIO9 | 128x64显示屏 |
| DS3231 RTC | I2C | SDA=GPIO8, SCL=GPIO9 | RTC 时钟模块，I2C地址 0x68 |
| SW1(报警键) | GPIO | GPIO7 | 低电平有效 |
| SW2(交互键) | GPIO | GPIO6 | 低电平有效 |
| WS2812 RGB | GPIO | GPIO48 | 报警指示灯 |

**注意**：I2S 麦克风和扬声器共用 SCK(BCLK) 和 WS(LRC) 引脚，需分时复用。

---

## 九、命名规范

- **变量/函数**: snake_case（如 `sensor_data_t`, `alarm_trigger()`）
- **宏定义**: UPPER_SNAKE_CASE（如 `MAX_ALARM_QUEUE_SIZE`）
- **文件名**: snake_case（如 `health_monitor.c`）
- **组件目录**: snake_case（如 `fall_detect/`）

---

## Phase 2：家属远程监护 App（family_flutter/）

### 背景与目标

Phase 1（迭代 1.1 ~ 4.4）已完成手环端全部功能和 BLE 网关 Flutter App（`mobile_flutter/`），数据已通过 EMQX Cloud 上云。但目前缺少远程端——老人家属无法在 BLE 范围外监护老人健康状况。

**Phase 2 目标**：创建独立的家属远程监护 App（`family_flutter/`），通过 MQTT 订阅云端数据，实现：
- 实时查看老人健康数据（心率、血氧、环境温度、步数）
- 报警即时推送（跌倒、阈值告警、手动求助等）
- 趋势图表分析（24小时/7天）与每日健康摘要
- 报警历史记录与远程 ACK 确认
- 后台常驻保活，确保报警不遗漏

**数据链路**：手环 → 网关 App（BLE）→ EMQX Cloud（MQTT）→ **家属 App（MQTT 订阅）**

### 核心架构决策

| 决策 | 方案 |
|------|------|
| App 架构 | 全新独立 Flutter 项目，不依赖 BLE，纯 MQTT 订阅 |
| MQTT 角色 | 订阅者（接收 telemetry/alarm/status/lwt），可发布 cmd |
| 认证方式 | 固定 MQTT 账号 + 手动输入 device_id 绑定 |
| 多用户 | 每个实例生成唯一 client_id（`family_{device_id}_{random}`） |
| 后台保活 | Android 前台服务 MqttKeepAliveService |
| 本地存储 | SQLite，7 天自动清理 |
| 状态管理 | Provider（ChangeNotifier），复用网关 App 架构模式 |
| UI 语言 | 纯中文，Material 3 |

---

### P2-1 模块边界表

#### lib/mqtt/
| 项目 | 内容 |
|-----|------|
| 职责 | MQTT TLS 连接、主题订阅、消息分发、断线重连、cmd 发布 |
| 不做 | 数据解析/存储（交给 data 层和 providers 层） |
| 对外接口 | `MqttSubscriber` 单例类 |
| 依赖 | mqtt_client |

#### lib/data/
| 项目 | 内容 |
|-----|------|
| 职责 | 数据模型定义、SQLite CRUD、7 天自动清理、Repository 模式封装 |
| 不做 | 网络通信、UI 渲染 |
| 对外接口 | `TelemetryRecord`, `AlarmRecord`, `DeviceStatusRecord`, `DailySummary`, `DatabaseHelper`, `FamilyRepository` |
| 依赖 | sqflite, path |

#### lib/providers/
| 项目 | 内容 |
|-----|------|
| 职责 | 应用状态管理、MQTT 消息消费→状态更新→UI 通知、设备绑定逻辑 |
| 不做 | 直接操作 MQTT 或 SQLite（通过 mqtt/ 和 data/ 层） |
| 对外接口 | `DeviceProvider`, `HealthAnalysisProvider` |
| 依赖 | provider, mqtt/, data/ |

#### lib/services/
| 项目 | 内容 |
|-----|------|
| 职责 | 本地通知推送、报警铃声/振动 |
| 不做 | 数据处理、UI 渲染 |
| 对外接口 | `NotificationService` |
| 依赖 | flutter_local_notifications |

#### lib/ui/
| 项目 | 内容 |
|-----|------|
| 职责 | 页面 UI、报警弹窗、图表展示、设置交互 |
| 不做 | 业务逻辑（通过 providers 层） |
| 对外接口 | 各 Page/Tab/Widget |
| 依赖 | provider, fl_chart |

#### android/kotlin（原生层）
| 项目 | 内容 |
|-----|------|
| 职责 | Android 前台服务（MqttKeepAliveService）、保活通知、MethodChannel 桥接 |
| 不做 | 业务逻辑（仅维持进程存活） |
| 对外接口 | MethodChannel `com.careband.family/keepalive`（start/stop） |
| 依赖 | Android Service API |

---

### P2-2 数据模型定义

#### TelemetryRecord
```dart
class TelemetryRecord {
  final int? id;
  final String deviceId;
  final double temp;        // 环境温度 °C
  final int heartRate;      // 心率 bpm
  final int spo2;           // 血氧 %
  final int steps;          // 步数
  final DateTime timestamp; // 设备时间戳
  final DateTime receivedAt;// 本地接收时间
}
```

#### AlarmRecord
```dart
class AlarmRecord {
  final int? id;
  final String deviceId;
  final int eventId;        // 唯一事件 ID
  final FamilyAlarmType alarmType; // 报警类型
  final double value;       // 触发值
  final DateTime timestamp; // 设备时间戳
  final DateTime receivedAt;// 本地接收时间
  final bool acknowledged;  // 是否已远程 ACK
  final DateTime? ackedAt;  // ACK 时间
}
```

#### FamilyAlarmType 枚举
```dart
enum FamilyAlarmType {
  tempHigh,      // 1 - 环境温度高
  tempLow,       // 2 - 环境温度低
  heartRateHigh, // 3 - 心率高
  heartRateLow,  // 4 - 心率低
  spo2Low,       // 5 - 血氧低
  fall,          // 6 - 跌倒
  manual,        // 7 - 手动报警
  spo2Warning,   // 8 - 血氧预警
  callFamily,    // 9 - 呼叫家人
  unknown;       // 未知类型
}
```

#### DeviceStatusRecord
```dart
class DeviceStatusRecord {
  final String deviceId;
  final bool online;        // 设备是否在线（LWT / status）
  final DateTime lastSeen;  // 最后一次收到消息的时间
}
```

#### DailySummary
```dart
class DailySummary {
  final DateTime date;
  final double avgTemp;
  final double minTemp;
  final double maxTemp;
  final int avgHeartRate;
  final int minHeartRate;
  final int maxHeartRate;
  final int avgSpo2;
  final int minSpo2;
  final int maxSteps;       // 当天最大步数（累积值）
  final int alarmCount;     // 当天报警次数
}
```

---

### P2-3 SQLite Schema

#### telemetry 表
```sql
CREATE TABLE telemetry (
  id          INTEGER PRIMARY KEY AUTOINCREMENT,
  device_id   TEXT    NOT NULL,
  temp        REAL    NOT NULL,
  heart_rate  INTEGER NOT NULL,
  spo2        INTEGER NOT NULL,
  steps       INTEGER NOT NULL,
  timestamp   INTEGER NOT NULL,  -- Unix 秒（设备端）
  received_at INTEGER NOT NULL   -- Unix 毫秒（本地）
);
CREATE INDEX idx_telemetry_device_time ON telemetry(device_id, timestamp);
```

#### alarm 表
```sql
CREATE TABLE alarm (
  id          INTEGER PRIMARY KEY AUTOINCREMENT,
  device_id   TEXT    NOT NULL,
  event_id    INTEGER NOT NULL,
  alarm_type  INTEGER NOT NULL,
  value       REAL    NOT NULL,
  timestamp   INTEGER NOT NULL,
  received_at INTEGER NOT NULL,
  acknowledged INTEGER NOT NULL DEFAULT 0,
  acked_at    INTEGER
);
CREATE INDEX idx_alarm_device_time ON alarm(device_id, timestamp);
```

#### config 表
```sql
CREATE TABLE config (
  key   TEXT PRIMARY KEY,
  value TEXT NOT NULL
);
-- 预置键: 'device_id', 'mqtt_host', 'mqtt_port', 'last_cleanup'
```

#### 7 天自动清理策略
```
触发时机：App 启动时 + 每 24 小时定时触发
清理规则：DELETE FROM telemetry WHERE received_at < (now - 7天)
          DELETE FROM alarm WHERE received_at < (now - 7天) AND acknowledged = 1
保留策略：未 ACK 的报警不清理（即使超过 7 天）
```

---

### P2-4 迭代里程碑总览

```
Phase 2: 家属远程监护 App
├─ 迭代 F-1: 项目搭建 + MQTT TLS 连接
├─ 迭代 F-2: 数据模型 + SQLite 存储 + 7天清理
├─ 迭代 F-3: Provider 状态管理 + 设备绑定 + 前台服务
├─ 迭代 F-4: Dashboard UI + 实时健康展示 + 报警通知
├─ 迭代 F-5: 趋势图表(24h/7天) + 每日健康摘要 + 异常提示
├─ 迭代 F-6: 报警历史 + 统计图表 + 远程 ACK
└─ 迭代 F-7: 设置页 + UI 精修 + 端到端联调
```

| 迭代 | 内容 | 关键产出 |
|------|------|---------|
| F-1 | 项目搭建 + MQTT TLS 连接 | Flutter 项目、MqttSubscriber、CA 证书 |
| F-2 | 数据模型 + SQLite 存储 + 7天清理 | models.dart、database_helper.dart、family_repository.dart |
| F-3 | Provider 状态管理 + 设备绑定 + 前台服务 | DeviceProvider、BindingPage、MqttKeepAliveService |
| F-4 | Dashboard UI + 实时健康展示 + 报警通知 | HomePage、DashboardTab、NotificationService、AlarmDialog |
| F-5 | 趋势图表(24h/7天) + 每日健康摘要 + 异常提示 | TrendTab、TrendChart、SummaryCard、AnomalyTips |
| F-6 | 报警历史 + 统计图表 + 远程 ACK | AlarmTab、AlarmStats（饼图+柱状图）、远程命令 |
| F-7 | 设置页 + UI 精修 + 端到端联调 | SettingsTab、主题优化、集成测试 |

---

### P2-5 逐迭代计划

#### 迭代 F-1: 项目搭建 + MQTT TLS 连接

**目标**: 创建全新 Flutter 项目，实现与 EMQX Cloud 的 TLS 安全连接，验证订阅/接收消息

**变更范围**:
- 新建 `family_flutter/` Flutter 项目
- 新建 `lib/mqtt/mqtt_subscriber.dart`
- 新建 `lib/mqtt/mqtt_config.dart`（gitignored）
- 新建 `assets/CA/emqxsl-ca.crt`
- 配置 `pubspec.yaml`
- 配置 `.gitignore`

**步骤**:
1. 使用 `flutter create` 创建 `family_flutter` 项目，配置 `pubspec.yaml` 基础依赖
2. 将 EMQX Cloud CA 证书放入 `assets/CA/emqxsl-ca.crt`，在 `pubspec.yaml` 注册 assets
3. 创建 `mqtt_config.dart` 存放 MQTT 连接配置（host/port/username/password），并加入 `.gitignore`
4. 实现 `MqttSubscriber` 单例类：TLS 连接、自动重连、主题订阅、消息回调分发
5. 在 `main.dart` 中添加临时测试页面，显示 MQTT 连接状态和接收到的原始消息
6. 使用网关 App 发布测试数据，验证家属 App 能正确接收

**产出物**:
- `family_flutter/` 完整 Flutter 项目骨架
- `lib/mqtt/mqtt_subscriber.dart`
- `lib/mqtt/mqtt_config.dart`（gitignored）
- `assets/CA/emqxsl-ca.crt`

**验收方法**:
- `flutter analyze` 无错误
- App 启动后成功连接 EMQX Cloud（日志显示 `Connected`）
- 订阅 `careband/+/telemetry` 后能接收到网关 App 发布的测试数据
- 断网后自动重连成功

**用户验证清单**:
- [ ] Flutter 项目编译通过
- [ ] MQTT TLS 连接成功
- [ ] 能接收 telemetry 消息
- [ ] 断线自动重连

---

#### 迭代 F-2: 数据模型 + SQLite 存储 + 7天清理

**目标**: 实现数据模型、SQLite 持久化存储和 7 天自动清理策略

**变更范围**:
- 新建 `lib/data/models.dart`
- 新建 `lib/data/database_helper.dart`
- 新建 `lib/data/family_repository.dart`

**步骤**:
1. 实现 `models.dart`：定义 TelemetryRecord、AlarmRecord、DeviceStatusRecord、DailySummary、FamilyAlarmType
2. 实现 `database_helper.dart`：SQLite 初始化、建表（telemetry/alarm/config）、版本迁移
3. 实现 `family_repository.dart`：Repository 模式封装 CRUD 操作
4. 实现 7 天自动清理逻辑：App 启动时触发 + 定时 24 小时触发
5. 实现 DailySummary 聚合查询（AVG/MIN/MAX）
6. 编写单元测试验证 CRUD 和清理逻辑

**产出物**:
- `lib/data/models.dart`
- `lib/data/database_helper.dart`
- `lib/data/family_repository.dart`

**验收方法**:
- `flutter analyze` 无错误
- `flutter test` 通过（CRUD + 清理）
- 插入测试数据后查询返回正确
- 超过 7 天的已确认报警被清理，未确认报警保留
- DailySummary 聚合结果正确

**用户验证清单**:
- [ ] 数据模型定义完整
- [ ] SQLite 建表成功
- [ ] CRUD 操作正确
- [ ] 7 天清理策略生效
- [ ] 未 ACK 报警不被清理

---

#### 迭代 F-3: Provider 状态管理 + 设备绑定 + 前台服务

**目标**: 实现 Provider 状态管理架构、设备绑定流程、Android 前台服务保活

**变更范围**:
- 新建 `lib/providers/device_provider.dart`
- 新建 `lib/providers/health_analysis_provider.dart`
- 新建 `lib/ui/binding_page.dart`
- 新建 `android/.../kotlin/com/careband/family/MqttKeepAliveService.kt`
- 修改 `android/.../kotlin/com/careband/family/MainActivity.kt`
- 修改 `lib/main.dart`

**步骤**:
1. 实现 `DeviceProvider`：管理绑定的 device_id、MQTT 连接状态、设备在线状态、最新遥测数据
2. 实现 `HealthAnalysisProvider`：管理历史数据查询、DailySummary、趋势数据缓存
3. 实现 `BindingPage`：输入 device_id、保存到 SharedPreferences 和 SQLite config 表、触发 MQTT 订阅
4. 实现 Android 前台服务 `MqttKeepAliveService`：Notification Channel、MethodChannel 桥接
5. 在 `MainActivity.kt` 中注册 MethodChannel，实现 start/stop 前台服务
6. 在 `main.dart` 中配置 MultiProvider，串联 MQTT → Provider → UI 数据流

**前台服务设计**:
```
MethodChannel: com.careband.family/keepalive
  ├─ startService()  → 启动前台服务，显示常驻通知 "家属监护运行中"
  └─ stopService()   → 停止前台服务
```

**产出物**:
- `lib/providers/device_provider.dart`
- `lib/providers/health_analysis_provider.dart`
- `lib/ui/binding_page.dart`
- `android/.../MqttKeepAliveService.kt`
- 修改后的 `MainActivity.kt`
- 修改后的 `main.dart`

**验收方法**:
- `flutter analyze` 无错误
- 输入 device_id 后 App 自动订阅对应主题
- 收到 telemetry 消息后 Provider 状态更新，UI 刷新
- App 切到后台 30 分钟后前台服务仍运行（通知栏可见）
- 杀掉 App 后前台服务保持（Android 行为验证）

**用户验证清单**:
- [ ] 设备绑定页面可输入 device_id
- [ ] 绑定后 MQTT 自动订阅
- [ ] Provider 状态正确更新
- [ ] 前台服务常驻通知可见
- [ ] 后台 30 分钟仍可接收消息

---

#### 迭代 F-4: Dashboard UI + 实时健康展示 + 报警通知

**目标**: 实现主页 Dashboard 实时展示健康数据、报警弹窗和本地通知推送

**变更范围**:
- 新建 `lib/ui/home_page.dart`
- 新建 `lib/ui/tabs/dashboard_tab.dart`
- 新建 `lib/ui/widgets/health_card.dart`
- 新建 `lib/ui/widgets/alarm_dialog.dart`
- 新建 `lib/services/notification_service.dart`

**步骤**:
1. 实现 `HomePage`：BottomNavigationBar 四 Tab 架构（仪表盘/趋势/报警/设置）
2. 实现 `DashboardTab`：四宫格 HealthCard 展示心率、血氧、环境温度、步数
3. 实现 `HealthCard` 组件：图标 + 数值 + 单位 + 状态指示（正常/异常/离线）
4. 实现 `NotificationService`：flutter_local_notifications 初始化、报警通知推送（声音+振动）
5. 实现 `AlarmDialog`：报警弹窗（类型、数值、时间、远程 ACK 按钮）
6. 在 `DeviceProvider` 中集成：收到 alarm 消息 → 触发通知 + 弹窗 + 存储

**Dashboard 布局设计**:
```
┌─────────────────────────────┐
│  设备状态栏（在线/离线）      │
├──────────┬──────────────────┤
│  ❤️ 心率   │  🫁 血氧         │
│  72 bpm  │  98 %            │
├──────────┼──────────────────┤
│  🌡️ 温度   │  🚶 步数         │
│  36.5°C  │  1,234 步        │
├──────────┴──────────────────┤
│  最近报警记录（最新 3 条）    │
└─────────────────────────────┘
```

**产出物**:
- `lib/ui/home_page.dart`
- `lib/ui/tabs/dashboard_tab.dart`
- `lib/ui/widgets/health_card.dart`
- `lib/ui/widgets/alarm_dialog.dart`
- `lib/services/notification_service.dart`

**验收方法**:
- `flutter analyze` 无错误
- Dashboard 四宫格正确显示实时数据
- 设备在线/离线状态正确切换
- 收到 alarm 消息时弹窗显示 + 通知栏推送 + 振动
- App 在后台时报警通知可点击跳转到报警详情

**用户验证清单**:
- [ ] Dashboard 四宫格数据正确
- [ ] 设备状态显示正确
- [ ] 报警弹窗正常弹出
- [ ] 通知栏报警推送正常
- [ ] 后台报警通知可跳转

---

#### 迭代 F-5: 趋势图表(24h/7天) + 每日健康摘要 + 异常提示

**目标**: 实现健康趋势折线图、每日摘要卡片和异常数据智能提示

**变更范围**:
- 新建 `lib/ui/tabs/trend_tab.dart`
- 新建 `lib/ui/widgets/trend_chart.dart`
- 新建 `lib/ui/widgets/summary_card.dart`
- 新建 `lib/ui/widgets/anomaly_tips.dart`

**步骤**:
1. 实现 `TrendTab`：时间范围切换（24小时/7天），指标切换（心率/血氧/温度/步数）
2. 实现 `TrendChart`：基于 fl_chart 的折线图，支持 X 轴时间、Y 轴数值、正常/异常区间背景色
3. 实现 `SummaryCard`：每日健康摘要（平均值、最大最小值、步数总计、报警次数）
4. 实现 `AnomalyTips`：智能异常提示（如 "今日 14:30 心率达到 125bpm，高于正常范围"）
5. 在 `HealthAnalysisProvider` 中实现趋势数据查询和 DailySummary 聚合
6. 数据量大时分页加载，避免 UI 卡顿

**趋势图表设计**:
```
┌────────────────────────────────┐
│  [24小时] [7天]   [心率 ▼]     │
├────────────────────────────────┤
│  ┌──────────────────────────┐  │
│  │  120 ─ ─ ─ ─ ─ ─ ─ ─ ─  │  │ ← 高阈值虚线
│  │       ╱╲    ╱╲           │  │
│  │  72 ─╱──╲──╱──╲─────── │  │ ← 数据折线
│  │     ╱    ╲╱              │  │
│  │  45 ─ ─ ─ ─ ─ ─ ─ ─ ─  │  │ ← 低阈值虚线
│  │  00:00  06:00  12:00  18:00│  │
│  └──────────────────────────┘  │
├────────────────────────────────┤
│  📊 今日健康摘要                │
│  平均心率: 75 bpm | 步数: 3456 │
│  血氧范围: 96-99% | 报警: 0 次 │
├────────────────────────────────┤
│  ⚠️ 异常提示                    │
│  • 14:30 心率 125bpm（偏高）   │
│  • 02:15 血氧 89%（低于阈值）  │
└────────────────────────────────┘
```

**产出物**:
- `lib/ui/tabs/trend_tab.dart`
- `lib/ui/widgets/trend_chart.dart`
- `lib/ui/widgets/summary_card.dart`
- `lib/ui/widgets/anomaly_tips.dart`

**验收方法**:
- `flutter analyze` 无错误
- 24 小时趋势图正确渲染，数据点密度适中
- 7 天趋势图以每日聚合展示
- 阈值线正确显示，异常区间有背景色标注
- DailySummary 数据与 SQLite 查询结果一致
- 异常提示正确识别超阈值数据点

**用户验证清单**:
- [ ] 24小时折线图正确显示
- [ ] 7天趋势图正确显示
- [ ] 指标切换正常
- [ ] 每日摘要数据正确
- [ ] 异常提示准确

---

#### 迭代 F-6: 报警历史 + 统计图表 + 远程 ACK

**目标**: 实现报警历史列表、按类型统计图表（饼图+柱状图）和远程 ACK 命令

**变更范围**:
- 新建 `lib/ui/tabs/alarm_tab.dart`
- 新建 `lib/ui/widgets/alarm_card.dart`
- 新建 `lib/ui/widgets/alarm_stats.dart`

**步骤**:
1. 实现 `AlarmTab`：报警历史列表（按时间倒序）+ 顶部统计区域
2. 实现 `AlarmCard`：单条报警卡片（类型图标、描述、时间、ACK 状态、操作按钮）
3. 实现 `AlarmStats`：
   - 饼图：按报警类型分布（fl_chart PieChart）
   - 柱状图：近 7 天每日报警次数（fl_chart BarChart）
4. 实现远程 ACK 功能：点击 ACK 按钮 → 发布 `careband/{id}/cmd` 消息（cmd_type=0x01）→ 更新本地状态
5. 报警列表支持筛选（全部/未确认/按类型）和分页加载
6. 在 `DeviceProvider` 中添加 cmd 发布方法

**远程 ACK 命令格式**:
```json
Topic: careband/{device_id}/cmd
Payload: {
  "cmd_type": 1,
  "event_id": 12345,
  "source": "family_app",
  "timestamp": 1234567890
}
```

**产出物**:
- `lib/ui/tabs/alarm_tab.dart`
- `lib/ui/widgets/alarm_card.dart`
- `lib/ui/widgets/alarm_stats.dart`

**验收方法**:
- `flutter analyze` 无错误
- 报警历史列表正确展示，按时间倒序
- 饼图正确展示各类型报警占比
- 柱状图正确展示近 7 天每日报警次数
- 点击 ACK 按钮后本地状态更新为已确认
- ACK 命令成功发布到 MQTT（MQTT Explorer 可见）
- 筛选和分页功能正常

**用户验证清单**:
- [ ] 报警历史列表正确
- [ ] 饼图统计正确
- [ ] 柱状图统计正确
- [ ] 远程 ACK 功能正常
- [ ] 筛选和分页正常

---

#### 迭代 F-7: 设置页 + UI 精修 + 端到端联调

**目标**: 实现设置页面、整体 UI 精修美化、完整端到端联调验证

**变更范围**:
- 新建 `lib/ui/tabs/settings_tab.dart`
- 修改全局主题配置
- 修改各 Tab/Widget 样式
- 全模块联调

**步骤**:
1. 实现 `SettingsTab`：
   - 设备管理（解绑/重新绑定 device_id）
   - MQTT 连接状态显示
   - 通知开关（报警通知/振动/铃声）
   - 数据存储管理（手动清理/存储空间占用显示）
   - 关于页面（版本号、项目信息）
2. Material 3 主题精修：
   - 统一配色方案（健康绿、报警红、信息蓝）
   - 卡片圆角、阴影、间距统一
   - 深色模式支持（可选）
3. UI 细节优化：
   - 加载状态 Shimmer 效果
   - 空状态占位图
   - 数据刷新下拉动画
   - 过渡动画
4. 端到端联调：
   - 手环 → 网关 App（BLE）→ EMQX Cloud（MQTT）→ 家属 App
   - 验证 telemetry/alarm/status/lwt 全链路
   - 验证多台家属手机同时订阅
   - 验证后台 30 分钟以上仍能收到报警通知
5. 性能验证：
   - APK 大小 < 150MB
   - 冷启动时间 < 3 秒
   - 7 天数据量下列表滚动流畅

**产出物**:
- `lib/ui/tabs/settings_tab.dart`
- 修改后的全局主题和各组件样式
- 端到端联调测试报告

**验收方法**:
- `flutter analyze` 无错误
- `flutter test` 全部通过
- 端到端联调：手环发送报警 → 网关转发 → 家属 App 收到通知 + 弹窗
- 多设备同时订阅不冲突
- 后台保活 30 分钟以上正常
- APK < 150MB
- UI 符合 Material 3 规范，无明显违和感

**用户验证清单**:
- [ ] 设置页功能完整
- [ ] 解绑/重新绑定正常
- [ ] 通知开关生效
- [ ] UI 整体美观统一
- [ ] 端到端联调通过
- [ ] 多设备同时订阅正常
- [ ] 后台 30 分钟保活正常
- [ ] APK < 150MB

---

### P2-6 项目文件结构

```
family_flutter/
├── android/
│   └── app/src/main/kotlin/com/careband/family/
│       ├── MainActivity.kt
│       └── MqttKeepAliveService.kt
├── assets/
│   └── CA/
│       └── emqxsl-ca.crt
├── lib/
│   ├── main.dart
│   ├── mqtt/
│   │   ├── mqtt_subscriber.dart
│   │   └── mqtt_config.dart          (gitignored)
│   ├── data/
│   │   ├── models.dart
│   │   ├── database_helper.dart
│   │   └── family_repository.dart
│   ├── providers/
│   │   ├── device_provider.dart
│   │   └── health_analysis_provider.dart
│   ├── services/
│   │   └── notification_service.dart
│   └── ui/
│       ├── binding_page.dart
│       ├── home_page.dart
│       ├── tabs/
│       │   ├── dashboard_tab.dart
│       │   ├── trend_tab.dart
│       │   ├── alarm_tab.dart
│       │   └── settings_tab.dart
│       └── widgets/
│           ├── health_card.dart
│           ├── trend_chart.dart
│           ├── summary_card.dart
│           ├── anomaly_tips.dart
│           ├── alarm_card.dart
│           └── alarm_stats.dart
├── test/
│   ├── data/
│   │   ├── models_test.dart
│   │   ├── database_helper_test.dart
│   │   └── family_repository_test.dart
│   └── providers/
│       └── device_provider_test.dart
├── pubspec.yaml
└── .gitignore
```

---

### P2-7 依赖清单

| 依赖包 | 用途 | 备注 |
|--------|------|------|
| mqtt_client | MQTT 5.0 客户端，TLS 连接 | 核心依赖 |
| provider | 状态管理（ChangeNotifier） | 复用网关 App 模式 |
| sqflite | SQLite 本地存储 | 7 天数据持久化 |
| fl_chart | 折线图、饼图、柱状图 | 趋势和统计展示 |
| flutter_local_notifications | 本地通知推送 | 报警通知 |
| intl | 日期时间格式化 | 中文日期展示 |
| permission_handler | 运行时权限请求 | 通知权限 |
| shared_preferences | 轻量 KV 存储 | device_id 等配置缓存 |
| path | 文件路径处理 | SQLite 数据库路径 |

**注意**：无 BLE 相关依赖（flutter_blue_plus 等），家属 App 纯 MQTT 通信。

---

### P2-8 风险清单与对策

| 风险 | 影响 | 对策 |
|------|------|------|
| **Android OEM 后台杀进程** | 前台服务被系统杀死，报警遗漏 | 1. 使用前台服务 + 常驻通知<br>2. 引导用户关闭电池优化<br>3. 自动重连机制 |
| **EMQX Cloud 免费层连接数限制** | 多台家属手机可能超出限制 | 1. 使用唯一 client_id 避免冲突<br>2. 控制同时在线设备数<br>3. 监控连接数指标 |
| **Retained 消息过期/缺失** | 家属 App 启动时无法获取最新状态 | 1. 订阅 status retained 消息<br>2. 启动后主动请求状态<br>3. 显示 "等待数据" 占位 |
| **MQTT 消息延迟** | 报警通知不及时 | 1. alarm 使用 QoS 1<br>2. 监控消息延迟<br>3. 界面显示消息时间戳 |
| **SQLite 数据量过大** | App 卡顿、存储空间不足 | 1. 7 天自动清理<br>2. 分页查询<br>3. 显示存储占用 |
| **网络切换导致 MQTT 断连** | WiFi/移动网络切换时断连 | 1. 自动重连策略（指数退避）<br>2. 网络状态监听<br>3. 断连状态 UI 提示 |

---

### P2-9 验证方式

- 每个迭代均包含 `flutter analyze` + `flutter test` 验收
- F-7 迭代执行端到端联调：手环 → 网关 App（BLE）→ EMQX Cloud（MQTT）→ 家属 App
- 验证多台家属手机同时订阅
- 验证后台 30 分钟以上仍能收到报警通知
- 验证 APK < 150MB
