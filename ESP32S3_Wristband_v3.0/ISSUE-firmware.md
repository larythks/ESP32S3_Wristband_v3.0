# ISSUE.md - 项目问题记录

---

## ISSUE-001：手动测量无法触发 MAX30102 心率/血氧采集

**发现日期**：2026-02-13

**原因**：
`main.c:sw2_callback()` 中，用户长按 SW2 进入手动测量时，仅调用了 `ui_enter_manual_measure()`（UI 层切换页面），但缺少对 `sensor_start_hr_measure()` 的调用。导致传感器层的 15 秒测量窗口从未被启动，MAX30102 始终处于 shutdown 状态。

**后果**：
- 用户按下手动测量按钮后，屏幕显示"测量中"，但实际上 MAX30102 未唤醒、未采集 PPG 数据
- `health_monitor` 中的 `on_sensor_data()` 检测到 `sensor_get_hr_measure_state() == HR_MEASURE_IDLE`，不会处理 PPG 数据
- 心率和血氧值永远不会被计算，手动测量功能完全失效

**解决方案**：
在 `main.c:sw2_callback()` 的手动测量入口处，紧接 `ui_enter_manual_measure()` 之后添加 `sensor_start_hr_measure()` 调用：

```c
ui_enter_manual_measure();
sensor_start_hr_measure();  // 补充：启动 15 秒测量窗口
```

**涉及文件**：`main/main.c`

---

## ISSUE-002：手动测量页面无倒计时、无结果展示、无自动退出

**发现日期**：2026-02-13

**原因**：
`ui_manager.c:draw_manual_measure_page()` 原实现为静态页面，始终显示固定文本"Measuring..."，没有与传感器测量窗口的时间进度关联，也没有测量完成后展示结果和自动退出的逻辑。

**后果**：
- 用户无法知道测量还剩多少时间，体验差
- 测量完成后页面无任何反馈，用户不知道测量已结束
- 必须手动长按 SW2 退出，否则页面一直停留在"Measuring..."

**解决方案**：
为手动测量页引入两阶段状态机：

1. **COUNTDOWN 阶段（15 秒）**：通过 `esp_timer_get_time()` 记录起始时间，在 500ms 定时器回调中计算剩余秒数并刷新显示 `"Time left: Xs"`
2. **RESULT 阶段（5 秒）**：倒计时结束后调用 `health_get_status()` 获取心率/血氧/体温，展示测量结果
3. **自动退出**：结果展示 5 秒后自动调用 `ui_exit_manual_measure()` 返回之前的页面

复用已有的 `step_refresh_timer_callback()`（500ms 周期），扩展其对 `UI_PAGE_MANUAL_MEASURE` 页面的处理。

**涉及文件**：`components/ui_manager/include/ui_manager.h`、`components/ui_manager/ui_manager.c`

---

## ISSUE-003：OLED 局部刷新时旧文本残影未清除

**发现日期**：2026-02-15

**原因**：
`sh1106_draw_char()` 仅在字体位图为 1 的像素位置调用 `sh1106_draw_pixel()` 设置像素，但不处理位图为 0 的像素位置。这导致：
1. 绘制空格字符（位图全为 0）时不会清除任何像素，`sh1106_draw_string(x, y, "   ", 1)` 的"清行"操作完全无效
2. 新文本比旧文本短时（如步数从 "1234" 变为 "99"），尾部旧字符像素残留在帧缓冲中

**后果**：
- 步数每 500ms 局部刷新时，旧数字残影无法被清除，显示出现叠字/鬼影
- 手动测量倒计时等所有局部刷新场景均受影响

**解决方案**：
修改 `sh1106_draw_char()` 为"不透明渲染"模式：字体位图为 1 的位置绘制前景色，为 0 的位置绘制背景色（`!color`）。同时清除字符间 1 像素间隔区域，确保整个 6x7 字符区域被完全覆盖。

```c
// 修改后的关键逻辑
if (line & (1 << j)) {
    sh1106_draw_pixel(x + i, y + j, color);
} else {
    sh1106_draw_pixel(x + i, y + j, !color);  // 新增：清除背景
}
// 新增：清除字符间 1 像素间隔
for (uint8_t j = 0; j < 7; j++) {
    sh1106_draw_pixel(x + 5, y + j, !color);
}
```

**涉及文件**：`components/drivers/sh1106/sh1106.c`

---

## ISSUE-004：手动测量结果页偶现显示上一次测量数据

**发现日期**：2026-02-15

**原因**：
UI 与 health_monitor 之间存在时序竞争。UI 的 500ms 定时器通过自身壁钟时间判断 15 秒倒计时结束后，立即调用 `health_get_status()` 读取结果。但此时 health_monitor 的计算链路（sensor_task → event_publish → event_dispatch → on_sensor_data → calculate_heart_rate/calculate_spo2）可能尚未完成，存在约 0~100ms 的竞争窗口。若 UI timer 恰好在此窗口内触发，读到的是上一次测量的旧数据。

此外，结果页面仅在阶段切换瞬间绘制一次（RESULT 分支不调用 `draw_manual_measure_page()`），一旦首次读到旧数据，整个 5 秒展示期间都不会更新。

**后果**：
- 手动测量结束后，约 20% 概率（100ms/500ms）显示上一次而非本次的心率/血氧结果
- 用户看到错误的测量数据，无法信任测量功能

**解决方案**：
在 COUNTDOWN 和 RESULT 之间新增 `MANUAL_PHASE_WAIT_RESULT` 过渡阶段：

1. 倒计时到达 15 秒后，切换到 WAIT_RESULT 阶段，OLED 显示 "Calculating..."
2. 下一个 timer tick（500ms 后）再切换到 RESULT 阶段，此时读取 `health_get_status()` 展示结果
3. 500ms 的等待远大于 health_monitor 所需的 ~100ms，彻底消除竞争窗口

```c
// 三阶段状态机
typedef enum {
    MANUAL_PHASE_COUNTDOWN = 0,  // 倒计时中（15秒）
    MANUAL_PHASE_WAIT_RESULT,    // 等待计算完毕（500ms）
    MANUAL_PHASE_RESULT          // 显示结果（5秒）
} manual_measure_phase_t;
```

**涉及文件**：`components/ui_manager/ui_manager.c`

---

## ISSUE-005：BLE 广播数据包含 128-bit UUID 存在超限风险

**发现日期**：2026-02-15

**原因**：
`ble_service.c` 的 `ble_start_advertise()` 中，将 128-bit 服务 UUID 放在广播数据 (Advertising Data) 中。BLE 广播数据上限为 31 bytes，各字段占用：
- Flags: 3 bytes
- Complete Local Name "CareBand": 10 bytes (2 header + 8 name)
- 128-bit UUID: 18 bytes (2 header + 16 UUID)
- 合计: 31 bytes

恰好达到上限，未来若增加任何广播字段（如 TX Power Level、Manufacturer Data），将导致 `ble_gap_adv_set_fields()` 返回 `BLE_HS_EMSGSIZE` 错误，广播无法启动。

**后果**：
- 当前可正常工作，但无扩展空间
- 未来增加广播数据字段时会导致广播失败，设备不可被扫描发现

**解决方案**：
将 128-bit UUID 从广播数据移到 Scan Response 数据中。Scan Response 同样有 31 bytes 上限，但独立于广播数据，互不影响。

广播数据仅保留 Flags + 设备名 (13 bytes)，使用 `ble_gap_adv_rsp_set_fields()` 设置 Scan Response 包含 UUID：

```c
/* 广播数据: 仅 Flags + Name */
memset(&fields, 0, sizeof(fields));
fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
fields.name = (uint8_t *)name;
fields.name_len = strlen(name);
fields.name_is_complete = 1;
ble_gap_adv_set_fields(&fields);

/* Scan Response: 服务 UUID */
memset(&rsp_fields, 0, sizeof(rsp_fields));
rsp_fields.uuids128 = &s_svc_uuid;
rsp_fields.num_uuids128 = 1;
rsp_fields.uuids128_is_complete = 1;
ble_gap_adv_rsp_set_fields(&rsp_fields);
```

**涉及文件**：`components/ble_gatt/ble_service.c`

---

## ISSUE-006：Telemetry timestamp 字段为系统启动时间而非 Unix 时间戳

**发现日期**：2026-02-15

**原因**：
Telemetry 数据包中的 `timestamp` 字段来自 `sensor_data_t.timestamp`，该值由 `esp_timer_get_time() / 1000` 产生，代表系统启动以来的毫秒数。除以 1000 后填入数据包的是"系统运行秒数"，而非 development_plan.md 中规定的 Unix 秒时间戳。ESP32 开机时没有 RTC 时钟同步，无法直接生成 Unix 时间。

**后果**：
- 手机端/云端收到的 timestamp 无法直接转为日期时间
- 数据记录无法按真实时间排序
- MVP 阶段可接受（手机端可根据接收时间推算），但正式版本需修复

**解决方案**：
在 telemetry_task 的 timestamp 赋值处添加 TODO 注释，标记后续需要通过 NTP 或手机端下发时间来校正。当前 MVP 阶段保持使用系统启动时间。

**涉及文件**：`components/ble_gatt/ble_service.c`

---

## ISSUE-007：BLE 命令 nonce 校验失败返回不存在的错误码 BLE_ATT_ERR_AUTHORIZATION

**发现日期**：2026-02-15

**原因**：
`ble_service.c` 的 `gatt_chr_access_command()` 中，当 nonce 校验失败时返回 `BLE_ATT_ERR_AUTHORIZATION`，但 NimBLE 的 `ble_att.h` 中不存在该宏定义。正确的宏名称是 `BLE_ATT_ERR_INSUFFICIENT_AUTHOR`（ATT 错误码 0x08，表示 Insufficient Authorization）。

**后果**：
- 编译失败，错误信息：`'BLE_ATT_ERR_AUTHORIZATION' undeclared (first use in this function)`
- 整个项目无法构建

**解决方案**：
将所有 `BLE_ATT_ERR_AUTHORIZATION` 替换为 `BLE_ATT_ERR_INSUFFICIENT_AUTHOR`：

```c
// 修改前
return BLE_ATT_ERR_AUTHORIZATION;

// 修改后
return BLE_ATT_ERR_INSUFFICIENT_AUTHOR;
```

共 4 处替换（ACK_ALARM、SYNC_TIME、REQUEST_REPORT、MANUAL_MEASURE 命令的 nonce 校验失败分支）。

**涉及文件**：`components/ble_gatt/ble_service.c`

---

## ISSUE-008：ALARMING 状态下新报警不发布 EVT_ALARM_STATE 事件

**发现日期**：2026-02-16

**原因**：
`alarm_manager.c` 的 `alarm_trigger()` 函数中，当系统已在 `ALARM_STATE_ALARMING` 状态时收到新报警，仅调用 `send_ble_alarm()` 重新发送 BLE 通知，但未调用 `publish_alarm_state_event()` 发布事件总线事件。

**后果**：
- 如果 UI 或其他模块订阅 `EVT_ALARM_STATE` 来显示当前报警详情（类型/数值），当报警类型变更时（例如先触发心率过高，随后又触发体温过高），订阅者不会收到更新通知
- UI 可能显示过时的报警类型/数值

**解决方案**：
在 `ALARM_STATE_ALARMING` 分支中，`send_ble_alarm()` 之后补充 `publish_alarm_state_event()` 调用：

```c
case ALARM_STATE_ALARMING:
    /* 已在报警中，更新数据并重新发送 BLE + 通知订阅者 */
    send_ble_alarm();
    publish_alarm_state_event();  // 新增：通知订阅者报警类型变更
    break;
```

---

## ISSUE-009：ui_manager 日志 TAG 使用不一致

**发现日期**：2026-02-16

**原因**：
`ui_manager.c` 中有3处 `ESP_LOGI` 调用使用了字符串字面量 `"ui_manager"` 而非已定义的 `TAG` 变量。虽然输出内容相同，但不符合项目编码规范，且如果未来修改 TAG 值，这些日志不会同步更新。

**后果**：
- 代码一致性问题，不符合 ESP-IDF 日志最佳实践
- 如果 TAG 值修改，这3条日志不会同步变更

**解决方案**：
将第235、242、249行的 `"ui_manager"` 替换为 `TAG`：

```c
// 改前: ESP_LOGI("ui_manager", "...");
// 改后: ESP_LOGI(TAG, "...");
```

**涉及文件**：`components/ui_manager/ui_manager.c`

---

## ISSUE-010：心率/血氧 hr_mode 被设置但从未在采样逻辑中使用

**发现日期**：2026-02-17

**原因**：
`sensor_service.c` 中 `sensor_set_mode(SENSOR_HR_SPO2, SAMPLING_MODE_REALTIME)` 正确地将 `s_ctx.hr_mode` 设置为 `SAMPLING_MODE_REALTIME`，但 `HR_MEASURE_IDLE` 状态中的自动触发间隔硬编码为 `SENSOR_HR_AUTO_INTERVAL_MS`（120 秒），完全忽略了 `hr_mode` 的值。此外 `HR_MEASURE_COMPLETE` 状态中 `hr_last_auto_trigger` 仅在 IDLE 触发时设置（测量开始时），导致实时模式下测量完成后立刻再次触发（`now - 15秒前 >= 1秒` 立刻成立）。

**后果**：
- 心率/血氧的实时监测模式形同虚设，即使 `hr_mode == SAMPLING_MODE_REALTIME`，采样间隔仍为 120 秒
- 异常检测后无法加速采样进行持续越阈确认
- 与温度传感器的实时模式行为不一致（温度已正确实现动态间隔）

**解决方案**：
1. 在 `HR_MEASURE_IDLE` 中根据 `s_ctx.hr_mode` 动态选择间隔：实时模式 1 秒，正常模式 120 秒
2. 在 `HR_MEASURE_COMPLETE` 中更新 `hr_last_auto_trigger = now`，确保间隔从测量结束时开始计算

```c
case HR_MEASURE_IDLE: {
    uint32_t hr_interval = (s_ctx.hr_mode == SAMPLING_MODE_REALTIME)
                           ? SENSOR_REALTIME_INTERVAL
                           : SENSOR_HR_AUTO_INTERVAL_MS;
    if ((now - s_ctx.hr_last_auto_trigger) >= hr_interval) { ... }
    break;
}
case HR_MEASURE_COMPLETE:
    ...
    s_ctx.hr_last_auto_trigger = now;  // 从测量结束时开始计算
    break;
```

**涉及文件**：`components/services/sensor_service/sensor_service.c`

---

## ISSUE-011：心率/血氧实时模式在报警确认后才触发，而非首次异常时

**发现日期**：2026-02-17

**原因**：
`health_monitor.c` 中 `publish_health_alert()` 函数内调用 `sensor_set_mode(SENSOR_HR_SPO2, SAMPLING_MODE_REALTIME)`，但 `publish_health_alert()` 仅在报警级别达到 `ALERT_LEVEL_ALARM` 后才被调用。对于心率，这要求持续越阈 30 秒（`HR_ALARM_DURATION_MS`）后才触发实时模式。设计意图是首次异常时立刻进入实时模式进行高频采样确认。

**后果**：
- 首次检测到心率/血氧异常时，仍以 120 秒间隔采样，无法快速进行持续越阈确认
- 在正常模式下等待 30 秒持续越阈本身就需要多个 120 秒周期，逻辑上自相矛盾
- 与温度传感器的行为不一致（温度在 sensor_service 内首次异常即切换实时模式）

**解决方案**：
将告警判定从时间制改为计数制：
- 首次异常立刻进入实时模式 + `alert_count = 1`
- 连续 2 次异常测量后触发报警（`HR_ALARM_COUNT = 2`，`SPO2_ALARM_COUNT = 2`）
- 删除 `HR_ALARM_DURATION_MS` 和 `SPO2_ALARM_DURATION_MS` 宏

**涉及文件**：
- `components/services/health_monitor/include/health_monitor.h`
- `components/services/health_monitor/health_monitor.c`

---

## ISSUE-012：心率/血氧正常化后没有退出实时模式的机制

**发现日期**：2026-02-17

**原因**：
`health_monitor.c` 的 `check_alerts()` 中，当心率/血氧恢复正常时仅将 `hr_in_alert`/`spo2_in_alert` 置 false，但没有调用 `sensor_set_mode(SENSOR_HR_SPO2, SAMPLING_MODE_NORMAL)` 退出实时模式。一旦进入实时模式，即使指标恢复正常，传感器仍以 ~16 秒周期持续高频采样。

**后果**：
- 异常恢复后传感器永远停留在实时采样模式，无法回到正常的 120 秒间隔
- 不必要的高频采样浪费功耗
- 与温度传感器行为不一致（温度恢复正常后自动退出实时模式）

**解决方案**：
在 `check_alerts()` 末尾添加实时模式退出逻辑。因心率和血氧共享 MAX30102 传感器，需两者都恢复正常后才退出：

```c
if (!s_ctx.alert.hr_in_alert && !s_ctx.alert.spo2_in_alert) {
    if (sensor_get_mode(SENSOR_HR_SPO2) == SAMPLING_MODE_REALTIME) {
        sensor_set_mode(SENSOR_HR_SPO2, SAMPLING_MODE_NORMAL);
        ESP_LOGI(TAG, "HR/SpO2 normalized, exit realtime mode");
    }
}
```

**涉及文件**：`components/services/health_monitor/health_monitor.c`

---

## ISSUE-013：心率/血氧告警计数在每次事件回调时递增而非每次测量时递增

**发现日期**：2026-02-17

**原因**：
`health_monitor.c` 的 `check_alerts()` 在每次 `on_sensor_data()` 事件回调中被调用（每 100ms 一次），但心率/血氧值在两次测量窗口之间不会变化。计数制告警的 `hr_alert_count`/`spo2_alert_count` 在每次 `check_alerts()` 调用时都会递增，导致首次异常后 100ms 内计数就从 1 增到 2，立刻触发报警，无法实现"连续 2 次独立测量异常后才报警"的设计意图。

**后果**：
- 首次检测到异常值后约 100ms 即触发报警，与正常模式下的时间制行为无实质区别
- 实时模式的高频复测确认机制完全失效
- 用户单次偶发异常值即会触发报警，误报率高

**解决方案**：
在 `health_monitor_ctx_t` 中新增 `bool ppg_result_fresh` 标志，仅在测量窗口结束、新的 HR/SpO2 计算完成时设置为 `true`。`check_alerts()` 中 HR/SpO2 的计数逻辑仅在 `ppg_result_fresh == true` 时执行，执行后清除标志：

```c
// 测量窗口结束时
s_ctx.ppg_result_fresh = true;

// check_alerts() 中
if (s_ctx.ppg_result_fresh) {
    s_ctx.ppg_result_fresh = false;
    // HR/SpO2 告警计数逻辑...
}
```

**涉及文件**：`components/services/health_monitor/health_monitor.c`

---

## ISSUE-014：OLED 花屏 — 多任务并发操作帧缓冲区/I2C 总线导致显示数据损坏

**发现日期**：2026-02-17

**原因**：

SH1106 OLED 驱动（`sh1106.c`）和 UI 管理器（`ui_manager.c`）的所有函数均无线程安全保护。系统中存在多个并发访问路径：

1. **UI 定时刷新定时器回调**（500ms 周期，运行在 Timer Service 任务）
2. **按键事件回调**（`ui_switch_page()`、`ui_enter_manual_measure()` 等，运行在按键任务或 ISR 上下文）
3. **报警状态变化**（可能触发 `ui_update()`）
4. **2 分钟自动刷新定时器回调**

当两个任务同时操作 `s_buffer[]` 帧缓冲区时，一个任务正在写入新页面数据，另一个任务同时调用 `sh1106_update()` 将缓冲区刷写到 I2C 总线，导致：
- 帧缓冲区数据半新半旧，OLED 显示内容错乱（花屏）
- I2C 总线命令交错，SH1106 接收到错误的页地址/列地址命令，后续页面数据写入错误位置

此外，WS2812 LED 在报警状态下以满亮度（RGB 255）驱动，瞬时电流尖峰可能影响 I2C 总线电平稳定性，加剧花屏现象。

**后果**：
- OLED 屏幕随机出现花屏、乱码、部分区域显示错误内容
- 严重时整屏数据错乱，需要等待下一次全屏刷新才能恢复
- 在报警 LED 闪烁期间花屏概率更高

**解决方案**：

从软件和硬件两个层面同时修复：

**1. SH1106 驱动层 — 互斥锁保护（`sh1106.c`）**

新增 `SemaphoreHandle_t s_mutex` 互斥锁和 `s_initialized` 初始化标志。将绘制函数拆分为内部 `_raw` 版本（无锁）和公开版本（加锁）：

```c
static SemaphoreHandle_t s_mutex = NULL;
static bool s_initialized = false;

// 内部无锁版本
static inline void sh1106_draw_pixel_raw(...) { ... }
static void sh1106_draw_char_raw(...) { ... }
static void sh1106_draw_string_raw(...) { ... }

// 公开加锁版本
void sh1106_draw_string(int16_t x, int16_t y, const char *str, uint8_t color)
{
    if (!s_initialized) return;
    if (!sh1106_lock(pdMS_TO_TICKS(20))) return;
    sh1106_draw_string_raw(x, y, str, color);
    sh1106_unlock();
}
```

所有公开 API（`sh1106_clear`、`sh1106_fill`、`sh1106_update`、`sh1106_draw_pixel`、`sh1106_draw_char`、`sh1106_draw_string`、`sh1106_display_on`、`sh1106_set_contrast`）均加锁保护。`sh1106_update()` 中对每页 I2C 写入增加错误检查，失败时 break 避免写入垃圾数据。

**2. UI 管理器层 — 互斥锁保护（`ui_manager.c`）**

新增 `SemaphoreHandle_t s_ui_mutex` 保护 UI 状态变量（`s_current_page`、`s_manual_measuring` 等）。

定时器回调使用非阻塞获取锁（`ui_lock(0)`），锁被占用时跳过本次刷新：

```c
static void step_refresh_timer_callback(TimerHandle_t timer)
{
    if (!ui_lock(0)) {
        ESP_LOGD(TAG, "Skip step refresh: lock busy");
        return;
    }
    // ... 刷新逻辑 ...
    ui_unlock();
}
```

新增 `ui_update_locked()` 和 `ui_exit_manual_measure_locked()` 内部函数，供已持有锁的上下文调用，避免重复加锁导致死锁。

**3. 报警 LED 亮度降低（`alarm_manager.c`）**

降低 WS2812 LED 驱动亮度，减少电流尖峰对 I2C 总线的干扰：

```c
#define LED_BRIGHTNESS_PREALARM 48   // 原 255
#define LED_BRIGHTNESS_ALARM    64   // 原 255
#define LED_BRIGHTNESS_ACKED    48   // 原 255

// PRE_ALARM: (255,165,0) -> (48,32,0)
// ALARMING:  (255,0,0)   -> (64,0,0)
// ACKED:     (0,255,0)   -> (0,48,0)
```

**涉及文件**：
- `components/drivers/sh1106/sh1106.c`
- `components/ui_manager/ui_manager.c`
- `components/services/alarm_manager/alarm_manager.c`

---

## ISSUE-015：Home 页“星期X”中文显示乱码（字模数据不可辨识）

**发现日期**：2026-02-17

**原因**：
`components/drivers/sh1106/include/font_cn.h` 中的 12x12 中文字模是手写近似值，尤其“星”“期”等复杂字在 OLED 上笔画组合失真，导致实际显示效果接近乱码。

**后果**：
- Home 页顶部“星期X”字段难以辨认
- 日期时间页观感不符合迭代 1.5 验收预期（“中文星期显示正确”）

**解决方案**：
将 `font_cn.h` 的 9 个字符（星/期/一/二/三/四/五/六/日）替换为重新生成的 12x12 位图字模（按当前 `sh1106_draw_chinese()` 的“行优先 + 高位在左”格式组织），保持现有渲染逻辑不变，仅修正字模数据。

**涉及文件**：
- `components/drivers/sh1106/include/font_cn.h`

---

## ISSUE-016：心率/血氧测量期间 OLED 花屏（UI 并发绘制导致帧缓冲竞争）

**发现日期**：2026-02-17

**原因**：
`ui_manager` 中存在两个并发执行上下文会操作 OLED：
1. FreeRTOS 软件定时器回调（`refresh_timer_callback`、`step_refresh_timer_callback`，Timer Service 任务）
2. 按键任务回调触发的 UI 操作（`ui_switch_page`、`ui_enter_manual_measure`、`ui_exit_manual_measure`）

在心率/血氧测量期间，手动测量页面每 500ms 刷新一次，和按键触发的页面切换/退出可能交错执行。由于 `ui_manager` 没有互斥保护，`sh1106` 缓冲区可能在同一时刻被不同路径修改，导致画面出现花屏。

**后果**：
- 手动测量阶段 OLED 偶发花屏、字符错位或残影
- 页面切换与倒计时更新偶发互相覆盖

**解决方案**：
在 `ui_manager.c` 增加 `s_ui_mutex` 并将所有 UI 绘制路径串行化：
1. 定时器回调使用 `ui_lock(0)` 非阻塞获取锁，拿不到锁则跳过本次刷新
2. `ui_switch_page/ui_next_page/ui_enter_manual_measure/ui_exit_manual_measure/ui_update` 统一加锁
3. 增加内部 `_locked` 版本函数，避免锁内再次调用公共接口造成死锁
4. 手动测量超时自动退出改为调用 `ui_exit_manual_measure_locked()`

**涉及文件**：
- `components/ui_manager/ui_manager.c`

---

## ISSUE-017：BLE SYNC_TIME 命令未同步到 DS3231 RTC 硬件时钟

**发现日期**：2026-02-17

**原因**：
迭代 2.5 实现的 BLE `SYNC_TIME` 命令（cmd_type=0x02）仅将 Unix 时间戳存储在软件变量 `s_time_offset` 中，未写入 DS3231 RTC 硬件。迭代 1.5 新增 DS3231 驱动后，Home 页面的日期时间显示依赖 `ds3231_get_time()` 从 RTC 读取，但 RTC 中的时间从未被校准过。

**后果**：
- Home 页面显示的日期和时间不准确（DS3231 出厂默认时间或上次断电时的时间）
- 通过 nRF Connect 发送 SYNC_TIME 命令后，Telemetry 时间戳正确但 OLED 显示时间仍错误

**解决方案**：
在 `ble_service.c` 的 `BLE_CMD_SYNC_TIME` 处理分支中，将 Unix 时间戳转换为年月日时分秒后调用 `ds3231_set_time()` 写入 RTC：
```c
uint32_t ts = cmd->timestamp + 8 * 3600;  /* UTC+8 */
/* Unix 时间戳 -> 年月日时分秒 -> ds3231_set_time() */
```

**涉及文件**：
- `components/ble_gatt/ble_service.c`（新增 ds3231.h 引用和 RTC 写入逻辑）
- `components/ble_gatt/CMakeLists.txt`（REQUIRES 新增 drivers 依赖）

---

## ISSUE-018：SYNC_TIME 写入 DS3231 时缺少 day_of_week 导致校验失败

**发现日期**：2026-02-17

**原因**：
ISSUE-017 修复中新增的 Unix 时间戳转 `ds3231_time_t` 逻辑未设置 `day_of_week` 字段，该字段默认为 0。`ds3231_set_time()` 内部的 `is_time_valid()` 要求 `day_of_week` 在 1-7 范围内，0 不合法，导致返回 `ESP_ERR_INVALID_ARG`。

**后果**：
- BLE SYNC_TIME 命令执行后软件时间偏移正确更新，但 DS3231 硬件 RTC 写入失败
- Home 页面日期时间仍不准确

**解决方案**：
根据 Unix 时间戳计算星期几（1970-01-01 为周四），赋值给 `rtc_time.day_of_week`：
```c
uint32_t total_days = ts / 86400;
uint8_t dow = (uint8_t)((total_days + 3) % 7 + 1);  /* 1=周一 ... 7=周日 */
rtc_time.day_of_week = dow;
```

**涉及文件**：
- `components/ble_gatt/ble_service.c`

---

## ISSUE-019：Timer Service 任务负载过重（报警与灯效定时器）

**发现日期**：2026-02-18

**原因**：
`alarm_manager` 的软件定时器回调运行在 Timer Service 任务上下文内，旧实现使用 `xSemaphoreTake(..., pdMS_TO_TICKS(100))`，在竞争时会阻塞最多 100ms；回调触发状态切换后还会间接执行 BLE 通知和 WS2812 控制。`ws2812` 旧实现同样通过软件定时器回调执行闪烁，而回调内部 `ws2812_send_rgb()` 包含 `rmt_tx_wait_all_done(..., 100)` 阻塞等待。两处叠加导致 Timer Service 任务负担偏重。

**后果**：
- `PRE_ALARM` / `ACKED` 超时状态切换可能出现抖动或延迟。
- 系统忙时 LED 闪烁稳定性下降，定时器命令队列存在背压风险。

**解决方案**：
1. 将 WS2812 闪烁机制从软件定时器回调迁移为独立任务 `ws2812_blink_task`，由任务周期性执行亮灭和 RMT 发送。
2. 新增 WS2812 互斥锁保护闪烁参数；`ws2812_blink_start/stop` 仅更新状态并通过 `xTaskNotifyGive()` 唤醒任务。
3. 将 `alarm_manager` 的 `pre_alarm_timer_cb/acked_timer_cb` 改为非阻塞拿锁（`xSemaphoreTake(..., 0)`）；拿锁失败时使用 `xTimerChangePeriod(..., TIMER_RETRY_MS)` 短延时重试，避免阻塞 Timer Service。
4. 增加 tick 下限保护，避免极小闪烁周期退化为忙等。

**涉及文件**：
- `components/services/alarm_manager/alarm_manager.c`
- `components/drivers/ws2812/ws2812.c`
- `components/drivers/ws2812/include/ws2812.h`

---

## ISSUE-020：alarm_manager 的 get_alarm_wav 缺少 ALERT_TYPE_SPO2_WARNING 映射

**发现日期**：2026-02-18

**原因**：
`alarm_manager.c` 的 `get_alarm_wav()` 函数中 switch-case 缺少 `ALERT_TYPE_SPO2_WARNING` 分支。当血氧预警触发报警进入 ALARMING 状态时，`get_alarm_wav()` 返回 NULL，导致 `alarm_audio_play_async(NULL)` 直接跳过音频播放。

**后果**：
- 血氧预警（SPO2_WARNING）级别的报警无语音播报
- WS2812 灯光和 BLE 通知正常，但喇叭静默

**解决方案**：
在 `get_alarm_wav()` 的 switch 中添加 SPO2_WARNING 分支，复用 spo2_low 的音频文件：
```c
case ALERT_TYPE_SPO2_LOW:     return "alarm_spo2_low";
case ALERT_TYPE_SPO2_WARNING: return "alarm_spo2_low";  // 复用
```

**涉及文件**：`components/services/alarm_manager/alarm_manager.c`

---

## ISSUE-021：VOICE_CMD_CALL_FAMILY 双重音频播放竞争

**发现日期**：2026-02-18

**原因**：
`voice_cmd.c` 的 `handle_command_response(VOICE_CMD_CALL_FAMILY)` 中，`alarm_trigger(ALERT_TYPE_CALL_FAMILY, NULL)` 内部通过 `alarm_audio_play_async()` 启动异步任务播放 "call_family.wav"。紧接着又直接调用 `audio_play_wav("call_family")`，导致两个调用者竞争 I2S TX 互斥锁：异步任务先获取锁播放，直接调用阻塞等待最多 5 秒后再次播放同一文件。

**后果**：
- "呼叫家人" 语音被播放两次（或第二次超时失败）
- I2S 资源被不必要地长时间占用

**解决方案**：
移除冗余的直接 `audio_play_wav("call_family")` 调用，改为 `alarm_trigger()` + `wait_audio_finish()`：
```c
case VOICE_CMD_CALL_FAMILY:
    alarm_trigger(ALERT_TYPE_CALL_FAMILY, NULL);
    wait_audio_finish();  // 等待异步音频完成
    break;
```

**涉及文件**：`components/services/voice_cmd/voice_cmd.c`

---

## ISSUE-022：VOICE_CMD_HELP 触发后 I2S 资源竞争

**发现日期**：2026-02-18

**原因**：
`voice_cmd.c` 的 `handle_command_response(VOICE_CMD_HELP)` 中，`alarm_trigger(ALERT_TYPE_MANUAL, NULL)` 启动异步音频任务播放 "alarm_help.wav"。由于 `alarm_trigger` 是非阻塞的，函数立即返回，随后 `resume_feed_task()` 被调用，feed 任务尝试 `audio_i2s_acquire_rx()` 重新获取 I2S RX。但此时异步音频任务可能仍持有 I2S TX（共享引脚），导致 feed 任务阻塞等待。

**后果**：
- feed 任务阻塞最多 5 秒等待 I2S 资源
- 语音识别在此期间中断（AFE 无新数据输入）
- 系统最终能恢复，但存在功能降级窗口

**解决方案**：
在 `alarm_trigger()` 后添加 `wait_audio_finish()` 等待异步音频播放完成，再执行 `resume_feed_task()`：
```c
case VOICE_CMD_HELP:
    alarm_trigger(ALERT_TYPE_MANUAL, NULL);
    wait_audio_finish();  // 等待异步音频完成后再恢复 feed
    break;
```

**涉及文件**：`components/services/voice_cmd/voice_cmd.c`

---

## ISSUE-023：vc_feed 任务栈溢出（ESP-SR AFE feed 调用链过深）

**发现日期**：2026-02-18

**原因**：
`voice_cmd.c` 中 `vc_feed` 任务的栈大小设置为 4096 字节，但 ESP-SR AFE 的 `s_afe_iface->feed()` 函数内部调用链较深（包含信号处理、噪声抑制等操作），实际栈使用超过 4096 字节。

**后果**：
- 系统运行时触发 `vApplicationStackOverflowHook`，打印 `A stack overflow in task vc_feed has been detected`
- ESP32-S3 进入 panic 并重启，语音识别功能完全不可用

**解决方案**：
将 `vc_feed` 任务栈从 4096 增大到 8192 字节（与 `vc_detect` 任务一致）：
```c
BaseType_t ok = xTaskCreatePinnedToCore(
    feed_task,
    "vc_feed",
    8192,    // 原 4096，ESP-SR AFE feed 调用链需要更大栈空间
    NULL, 5, &s_feed_task, 0);
```

**涉及文件**：`components/services/voice_cmd/voice_cmd.c`

---

## ISSUE-024：上电电流噪声 — RX 模式下 MAX98357A DIN 悬空

**发现日期**：2026-02-18

**原因**：
INMP441 和 MAX98357A 共用 BCLK(GPIO17) 和 WS(GPIO16)。系统启动后 `voice_cmd_start()` 立即获取 I2S RX，I2S 主模式持续输出时钟信号。但 `audio_i2s_acquire_rx()` 将 DOUT(GPIO18) 设为 `I2S_GPIO_UNUSED`，该引脚处于浮空状态。MAX98357A 在有时钟但 DIN 随机的情况下，将随机电平解码为音频数据输出。

**后果**：
- 扬声器一通电就持续输出电流噪声
- 噪声在语音识别运行期间始终存在

**解决方案**：
在 `audio_i2s_acquire_rx()` 末尾和 `audio_i2s_release()`（TX模式释放后）主动将 DOUT(GPIO18) 配置为 GPIO 输出并拉低，使 MAX98357A 接收全零数据（静音）：
```c
gpio_set_direction(AUDIO_DOUT_GPIO, GPIO_MODE_OUTPUT);
gpio_set_level(AUDIO_DOUT_GPIO, 0);
```

**涉及文件**：`components/drivers/audio/audio_player.c`

---

## ISSUE-025：语音播报链路与常驻识别互斥 — alarm_manager 不经过 pause/resume

**发现日期**：2026-02-18

**原因**：
`alarm_manager.c` 的 `alarm_audio_play_async()` 创建异步任务直接调用 `audio_play_wav()`，该函数内部的 `audio_i2s_acquire_tx()` 尝试获取 I2S mutex。但 `voice_cmd.c` 的 feed_task 长期持有 I2S RX（占用 mutex），且 alarm_manager 未调用 `pause_feed_task()` 暂停 feed_task。导致 TX acquire 超时（5秒），报警/TTS 播报失败。

**后果**：
- 有语音识别运行时，报警播报和 TTS 查询播报大概率失败
- I2S 资源无法从 RX 切换到 TX

**解决方案**：
1. 在 `audio_i2s_acquire_tx()` 中添加 pre-hook 调用机制，在尝试获取 mutex 之前自动调用已注册的钩子函数
2. 在 `audio_i2s_release()` 中（TX 模式释放后）添加 post-hook 调用
3. `voice_cmd_init()` 中注册 `pause_feed_task/resume_feed_task` 为钩子
4. `pause_feed_task/resume_feed_task` 添加引用计数，支持嵌套调用（手动 + hook 共存）

**涉及文件**：
- `components/drivers/audio/include/audio_player.h`
- `components/drivers/audio/audio_player.c`
- `components/services/voice_cmd/voice_cmd.c`

---

## ISSUE-026：wait_audio_finish() 竞态窗口 — 异步播放未开始即返回

**发现日期**：2026-02-18

**原因**：
`voice_cmd.c` 的 `handle_command_response()` 中调用 `alarm_trigger()` 后使用 `wait_audio_finish()` 等待异步音频完成。`wait_audio_finish()` 依赖 `audio_is_playing()` 检查 `s_playing` 标志，但 `s_playing` 在 `audio_play_wav()` 内部才被置 true。如果异步任务尚未调度执行，`s_playing` 仍为 false，导致提前返回。

**后果**：
- feed_task 被过早恢复，抢占 I2S RX
- 异步音频任务随后尝试 acquire TX 时可能与 feed_task 竞争

**解决方案**：
1. 新增 `audio_play_wav_async()` API，在创建异步任务前预设 `s_playing = true`
2. 新增 `audio_wait_done()` API，基于 EventGroup 等待完成事件
3. `alarm_manager.c` 改用 `audio_play_wav_async()`
4. `voice_cmd.c` 改用 `audio_wait_done(10000)`

**涉及文件**：
- `components/drivers/audio/include/audio_player.h`
- `components/drivers/audio/audio_player.c`
- `components/services/alarm_manager/alarm_manager.c`
- `components/services/voice_cmd/voice_cmd.c`

---

## ISSUE-027：INMP441 采样参数不匹配 — 16bit slot 导致 BCLK 低于规格

**发现日期**：2026-02-18

**原因**：
`audio_i2s_acquire_rx()` 配置 I2S RX 为 16bit slot、mono 模式，BCLK = 16000 * 16 * 2 = 512 kHz。INMP441 数据手册规定 BCLK 最低频率为 1.024 MHz。低于规格的 BCLK 导致 INMP441 数据输出与 I2S 帧对齐错误，且 24bit 输出被截断。

**后果**：
- 麦克风采集的 PCM 数据存在位对齐错误
- WakeNet/MultiNet 语音识别率降低

**解决方案**：
1. `audio_i2s_acquire_rx()` 的 slot 配置改为 `I2S_DATA_BIT_WIDTH_32BIT`（BCLK = 1.024 MHz）
2. `voice_cmd.c` 的 `feed_task` 读取 32bit 样本后右移 16 位转 16bit 再 feed 给 AFE

**涉及文件**：
- `components/drivers/audio/audio_player.c`
- `components/services/voice_cmd/voice_cmd.c`

---

## ISSUE-028：初始化失败未 fail-fast — 语音模块"看起来在运行但不可用"

**发现日期**：2026-02-18

**原因**：
`main.c` 中 `audio_player_init()` 失败后仅记录日志，仍继续初始化 `voice_cmd`。voice_cmd 依赖 audio_player 的 I2S mutex 和 SPIFFS，无法正常工作。

**后果**：
- 系统启动后语音识别任务运行但功能不可用
- 难以定位问题根源

**解决方案**：
用 `audio_ok` 标志记录结果，`audio_ok == false` 时跳过 voice_cmd 初始化。

**涉及文件**：`main/main.c`

---

## ISSUE-029：PROGRESS.md 3.2/3.3 进度状态偏乐观

**发现日期**：2026-02-18

**原因**：
迭代 3.2/3.3 标记为"已完成"，但验收仅为代码审查，缺乏实机闭环验证。实际存在 ISSUE-024 ~ ISSUE-028 等结构性问题。

**后果**：
- 项目进度跟踪失真

**解决方案**：
将 3.2/3.3 状态改为"代码重构中（待实机验收）"。

**涉及文件**：`PROGRESS.md`

---

## ISSUE-030：WAV 文件头格式不兼容导致语音播报和语音识别完全失效

**发现日期**：2026-02-19

**原因**：
`audio_player.c` 中的 WAV 解析器使用固定 44 字节的 `wav_header_t` 结构体读取文件头，假设 `fmt` 块之后紧跟 `data` 块。但所有 31 个 WAV 文件由 FFmpeg（Lavf62.3.100）生成，在 `fmt` 和 `data` 块之间插入了 34 字节的 `LIST/INFO/ISFT` 元数据块，实际文件头为 78 字节。导致 `data_size` 字段读到的是 LIST 块内容而非真实 PCM 数据长度，PCM 数据起始位置偏移 34 字节，且 34 不是 2 的倍数（16-bit 采样 = 2 字节对齐），所有采样高低字节颠倒。

**后果**：
1. 语音播报：所有 WAV 播放产生噪声/失真，无法正常播放报警语音和 TTS 数字播报
2. 语音识别：唤醒确认音和命令反馈音异常，I2S 资源释放时序被打乱，feed_task 长时间暂停导致 AFE ring buffer 数据过期，语音识别准确率大幅下降
3. 报警系统：alarm_manager 触发的报警音频不可辨识

**解决方案**：
将 `audio_play_wav()` 的 WAV 头解析从固定 44 字节结构体改为 chunk 遍历模式：先读 12 字节 RIFF 容器头，然后循环读取 8 字节 chunk 头（4 字节 ID + 4 字节 size），遇到 `fmt ` 读取格式参数，遇到 `data` 记录数据长度并开始播放，其他 chunk（LIST、fact 等）直接跳过。

**涉及文件**：`components/drivers/audio/audio_player.c`

---

## ISSUE-031：I2S MONO 模式下 L+R 交错数据导致语音识别失效

**发现日期**：2026-02-19

**原因**：
ESP-IDF v5.2 的 I2S 标准模式在 ESP32-S3 上配置 `I2S_SLOT_MODE_MONO` 时，实际仍然捕获左右双声道交错数据（`[L0,R0,L1,R1,...]`）。`voice_cmd.c` 的 feed_task 请求读取 `num_samples * sizeof(int32_t)` = 640 字节（160 个 32-bit 样本），实际得到 80 个 L 样本 + 80 个 R 样本交错排列。由于 INMP441 的 L/R 引脚接 GND（左声道输出），右声道为零值。

**后果**：
1. feed_task 以 5ms/chunk 运行（应为 10ms/chunk），即 2 倍实时速率喂入 AFE ring buffer，导致 ring buffer 溢出
2. 50% 的样本为右声道零值，WakeNet 接收到的音频数据有一半是静音
3. 尽管 VAD（语音活动检测）能检测到语音信号（vad=1），但 WakeNet 始终无法识别唤醒词（wakeup=0）
4. 语音识别功能完全失效

**解决方案**：
将 I2S 读取缓冲区扩大为 2 倍（`num_samples * 2 * sizeof(int32_t)` = 1280 字节），在 32-bit 转 16-bit 的转换循环中以步长 2 遍历，仅提取偶数索引（左声道）的样本：

```c
int read_size = num_samples * 2 * sizeof(int32_t);  // 1280 bytes
// ...
int raw_samples = bytes_read / sizeof(int32_t);
int samples = 0;
for (int i = 0; i < raw_samples && samples < num_samples; i += 2) {
    int32_t s = raw_buf[i] >> 14;  // left channel + 4x gain
    if (s > 32767)  s = 32767;
    if (s < -32768) s = -32768;
    feed_buf[samples++] = (int16_t)s;
}
```

**涉及文件**：`components/services/voice_cmd/voice_cmd.c`

---

## ISSUE-032：I2C 总线无互斥锁保护 — OLED 花屏 + DS3231 读取失败

**发现日期**：2026-02-19

**原因**：
`i2c_bus.c` 中的 `i2c_bus_read()` / `i2c_bus_write()` 没有 FreeRTOS mutex 保护，且 `sh1106.c` 绕过 `i2c_bus` 封装直接调用 `i2c_master_cmd_begin()`。系统中存在多个并发访问 I2C 总线的任务：

1. `sensor_task`（优先级 6，每 20ms）：调用 `mpu6050_read_accel/gyro()`、`max30102_read_fifo()` 通过 `i2c_bus_read/write`
2. Timer Service 任务（优先级 1，500ms 步数定时器）：调用 `ds3231_get_time()` 通过 `i2c_bus_read`，以及 `sh1106_update()` 直接 `i2c_master_cmd_begin`
3. Timer Service 任务（2 分钟刷新定时器）：同上

`sensor_task` 优先级（6）高于 Timer Service（1），可在定时器回调执行 I2C 操作时抢占，导致 I2C 总线状态被破坏。

**后果**：
1. **RTC 读取失败**：`ds3231_get_time()` 被 `sensor_task` 的 I2C 操作中断，返回 `ESP_FAIL`，UI 显示 `--/-- RTC`
2. **OLED 花屏**：`sh1106_update()` 需要 32 次 I2C 事务（8 页 × 4 次），被中途抢占后页地址/列地址命令错乱，数据写入错误位置

**解决方案**：
1. 在 `i2c_bus.c` 中新增 `SemaphoreHandle_t s_i2c_mutex`，`i2c_bus_init()` 时创建
2. `i2c_bus_read()` / `i2c_bus_write()` 前后加 `xSemaphoreTake/Give` 保护
3. 新增 `i2c_bus_lock()` / `i2c_bus_unlock()` 公开接口，供绕过封装的驱动使用
4. `sh1106.c` 中拆出 `sh1106_write_cmd_nolock()` 内部无锁版本
5. `sh1106_write_cmd()` 加锁版本供单条命令使用
6. `sh1106_update()` 用 `i2c_bus_lock(200)` 包裹整个 8 页循环，内部使用无锁版本，确保原子性

**涉及文件**：
- `components/drivers/common/include/i2c_bus.h`
- `components/drivers/common/i2c_bus.c`
- `components/drivers/sh1106/sh1106.c`

---

## ISSUE-033：切换至主界面始终读取不到 RTC 时间，需等待分钟变化才恢复

**发现日期**：2026-02-19

**原因**：
两个问题叠加：

1. **`sh1106_update()` 持锁时间过长（~30ms）**：ISSUE-032 修复中将整个 8 页 OLED 刷新包裹在单个 `i2c_bus_lock()` 中，持锁约 30ms。期间高优先级 `sensor_task`（优先级 6）的 I2C 操作被阻塞，释放后立即抢占。低优先级的 Timer Service / 按键任务中的 `ds3231_get_time()` 在 100ms 内仍可能竞争失败。

2. **Home 页 RTC 读取失败后不会触发重绘**：`draw_home_page()` 中当 `ds3231_get_time()` 失败时，`s_home_last_minute` 不被更新（保留上次访问时的值）。500ms 定时器回调判断 `s_home_last_minute != rtc_time.minute` 时，若分钟未变则跳过重绘，导致 `--/-- RTC` 持续显示直到分钟变化（最多等 60 秒）。

**后果**：
- 每次切换到主界面初始显示 `--/-- RTC`
- 必须等待分钟值变化后才能显示正确时间

**解决方案**：
1. `sh1106_update()` 从整体加锁改为**逐页加锁**：每页的 4 次 I2C 事务保持原子性（防花屏），页间释放锁让其他设备访问 I2C。最大持锁时间从 ~30ms 降至 ~4ms。
2. `draw_home_page()` 的 else 分支添加 `s_home_last_minute = -1`，确保下次 500ms 定时器一定触发重绘。

**涉及文件**：
- `components/drivers/sh1106/sh1106.c`
- `components/ui_manager/ui_manager.c`

---

## ISSUE-034："查询时间"语音命令已注册但缺少响应处理

**发现日期**：2026-02-19

**原因**：
`voice_cmd.c` 中已注册拼音命令 `"cha xun shi jian"`（id=5）并在 `map_command_id()` 中映射为 `VOICE_CMD_QUERY_TIME`，但 `handle_command_response()` 的 switch 语句中无对应 case，导致命中 `default` 分支播放 `cmd_not_recognized.wav`。同时 `simple_tts` 中缺少时间播报函数。

**后果**：
- 用户说 "查询时间" 被正确识别后，喇叭播放 "未识别命令" 而非当前时间
- SPIFFS 中已有 `prefix_time.wav` 和 `time_dot.wav` 但从未被使用

**解决方案**：
1. `simple_tts.h/c` 新增 `tts_speak_time(uint8_t hour, uint8_t minute)`，播放 "当前时间为 XX 点 XX"
2. `voice_cmd.c` 的 `handle_command_response()` 添加 `VOICE_CMD_QUERY_TIME` case，读取 DS3231 后调用 TTS 播报
3. 分钟 < 10 时补播 "零"（如 "十四点零五"），符合中文表达习惯

**涉及文件**：
- `components/drivers/audio/include/simple_tts.h`
- `components/drivers/audio/simple_tts.c`
- `components/services/voice_cmd/voice_cmd.c`

---

## ISSUE-035：ALARMING 状态下报警音频仅播放一次

**发现日期**：2026-02-19

**原因**：
`alarm_manager.c` 中 `enter_state(ALARM_STATE_ALARMING)` 调用 `alarm_audio_play_async()` 播放一次报警 WAV 文件后即结束。没有循环播放机制。

**后果**：
- 按下 SW1 或语音 "救命" 触发报警后，"我需要帮助" 仅播放一次
- 报警状态持续（红灯闪烁、BLE Notify 已发送），但声音提示仅持续数秒
- 在嘈杂环境或无人注意时，单次播放容易被忽略

**解决方案**：
将 ALARMING 状态的音频播放从单次异步播放改为循环播放任务：
1. 新增 `alarm_loop_task()`：`while (s_loop_active)` 循环调用 `audio_play_wav()`，每次播完间隔 500ms
2. `enter_state(ALARM_STATE_ALARMING)` 启动循环任务代替 `alarm_audio_play_async()`
3. 离开 ALARMING 状态时设置 `s_loop_active = false` + `audio_play_stop()` 停止循环

**涉及文件**：
- `components/services/alarm_manager/alarm_manager.c`

---

## ISSUE-036：SPO2_WARNING（血氧预警 90%-92%）功能无实际意义

**发现日期**：2026-02-19

**原因**：
`health_monitor.c` 中 `check_alerts()` 函数对血氧 90%-92% 区间触发 `ALERT_TYPE_SPO2_WARNING`（WARNING 级别），该告警不走连续计数确认机制，立即触发。但此区间值波动频繁，容易产生误报，且 WARNING 级别报警对用户无实际帮助。

**后果**：
- 血氧在 90%-92% 之间频繁触发 WARNING 告警，影响用户体验
- WARNING 级别与 ALARM 级别混用，增加系统复杂度

**解决方案**：
1. `event_bus.h`: `ALERT_TYPE_SPO2_WARNING` 位置改为 `ALERT_TYPE_PRE_ALARM_FALL`（复用枚举位置 6）
2. `health_monitor.h`: 删除 `#define SPO2_WARNING_LOW 92` 宏
3. `health_monitor.c`: 删除 `check_alerts()` 中 `else if (spo2 <= SPO2_WARNING_LOW)` 分支；移除 `publish_health_alert()` 中 `ALERT_TYPE_SPO2_WARNING` 引用
4. `alarm_manager.c`: 移除 `alert_to_ble_alarm_type()` 和 `get_alarm_wav()` 中的 SPO2_WARNING 映射（已在之前完成）
5. `ble_gatt_defs.h`: `BLE_ALARM_TYPE_SPO2_WARNING = 8` → `BLE_ALARM_TYPE_RESERVED_8 = 8`（保留编号避免协议偏移）

**涉及文件**：
- `components/services/event_bus/include/event_bus.h`
- `components/services/health_monitor/include/health_monitor.h`
- `components/services/health_monitor/health_monitor.c`
- `components/services/alarm_manager/alarm_manager.c`
- `components/ble_gatt/include/ble_gatt_defs.h`

---

## ISSUE-037：跌倒 PRE_ALARM 与 ALARMING 播放相同音频，无法区分

**发现日期**：2026-02-19

**原因**：
`alarm_manager.c` 中 `enter_state(ALARM_STATE_PRE_ALARM)` 调用 `alarm_audio_play_async("alarm_fall")`，而 ALARMING 阶段 `get_alarm_wav(ALERT_TYPE_FALL)` 也返回 `"alarm_fall"`。两个阶段播放相同的音频文件，用户无法通过声音区分预报警与正式报警。

**后果**：
- PRE_ALARM 阶段用户听到的是与 ALARMING 相同的紧急语音，不知道可以按键取消
- 15 秒确认窗口形同虚设，用户无法意识到当前处于可取消状态

**解决方案**：
1. `enter_state(ALARM_STATE_PRE_ALARM)`: 将 `alarm_audio_play_async("alarm_fall")` 改为 `alarm_audio_play_async("pre_alarm_fall")`
2. `get_alarm_wav(ALERT_TYPE_FALL)`: 返回值从 `"alarm_fall"` 改为 `"alarm_help"`（ALARMING 阶段播放求救语音）
3. 音频资源 `pre_alarm_fall.wav`（内容："检测到跌倒，如需取消请按报警键"）和 `alarm_help.wav`（内容："我需要帮助"）均已存在于 `spiffs_data/`

**涉及文件**：
- `components/services/alarm_manager/alarm_manager.c`

---

## ISSUE-038：心率测量不准且波动大 — PPG 数据管道丢失 + 无信号滤波 + 固定阈值 + 无运动抑制

**发现日期**：2026-02-20

**原因**：
心率测量链路存在 4 个层面的问题：

1. **PPG 数据大量丢失**（P0）：`sensor_service.c:sample_hr_spo2()` 每次从 MAX30102 FIFO 读出多个样本，但只保留最后一个（`s_ctx.latest_data.ppg_red = red[count - 1]`），导致 60-70% 的原始 PPG 数据被丢弃。

2. **无信号预处理**（P0）：`health_monitor.c:detect_peak()` 直接对原始 PPG 信号做峰值检测，包含直流基线漂移和高频噪声，心率信号频段 0.5-4Hz 未被隔离。

3. **固定峰值检测阈值**（P1）：`PEAK_DETECTION_THRESHOLD = 50` 硬编码，PPG 信号幅度因人因佩戴差异极大，固定阈值不适用。

4. **IMU 数据未用于运动伪影抑制**（P1）：sensor_service 持续采集 50Hz IMU 数据，但 health_monitor 完全未使用，运动伪影被当作心率峰值处理。

**后果**：
- 心率测量结果不准确，与真实心率偏差大
- 心率值在相邻测量窗口间波动剧烈
- 运动状态下心率完全不可信

**解决方案**：

1. **修复 PPG 数据管道**：在 `sensor_service.c` 新增 64 样本 PPG 环形缓冲区 + `sensor_drain_ppg()` API。
2. **添加级联 IIR 带通滤波器**：高通 0.5Hz + 低通 4Hz。
3. **自适应峰值检测**：基于最近 2 秒信号峰峰值 × 0.3 动态阈值 + 中值离群值剔除。
4. **运动伪影抑制**：利用 IMU 加速度检测运动，超阈值时跳过峰值检测。
5. **AC/DC 改为 RMS 计算**：替代全局 min/max，对异常值更鲁棒。

**涉及文件**：
- `components/services/sensor_service/include/sensor_service.h`
- `components/services/sensor_service/sensor_service.c`
- `components/services/health_monitor/health_monitor.c`

---

## ISSUE-039：血氧测量波动大 — AC/DC 计算使用未滤波的原始信号且包含运动样本

**发现日期**：2026-02-20

**原因**：
ISSUE-038 修复了心率检测的信号处理链路，但 SpO2 的 AC/DC 分量计算仍存在两个关键问题：

1. **AC 计算使用未滤波的原始信号**：带通滤波器仅应用于 IR 通道用于峰值检测，AC/DC 计算 (`calculate_ac_dc()`) 仍然遍历 400 元素原始缓冲区计算 RMS，原始信号包含直流漂移和高频噪声，导致 AC 分量不稳定。

2. **RED 通道完全未滤波**：SpO2 = f(AC_red/DC_red, AC_ir/DC_ir)，但 RED 通道没有经过任何滤波处理，其 AC 值受噪声影响更严重。

3. **运动样本污染 AC/DC**：运动样本虽然在峰值检测时被跳过，但仍被存入原始缓冲区参与 AC/DC 计算，运动引起的信号波动被计入 AC 分量。

**后果**：
- SpO2 值在 80%-99% 之间大幅波动
- 正常佩戴时偶尔出现 SpO2 < 80% 的虚假低氧读数
- 轻微运动即可导致 SpO2 读数异常

**解决方案**：

1. **RED 通道添加 IIR 带通滤波**：与 IR 通道相同的高通 0.5Hz + 低通 4Hz 级联滤波。
2. **增量式 AC/DC 累加**：在 `process_ppg_data()` 中逐样本累加滤波后信号的平方和（AC）和原始值（DC），运动样本不参与累加。使用 `double` 精度避免平方和溢出。
3. **移除 400 元素原始缓冲区**：`calculate_ac_dc()` 直接使用累加值计算，不再遍历缓冲区，节省 3200 字节 RAM。

**涉及文件**：
- `components/services/health_monitor/health_monitor.c`


---

## ISSUE-040：血氧计算持续偏低（< 90%）— 线性公式不准确 + 整数截断 + 无信号校验 + 滤波器瞬态偏差

**发现日期**：2026-02-20

**原因**：
SpO2 计算结果持续低于 90%，存在四个叠加问题：

1. **线性经验公式不准确**：使用 `SpO2 = 110 - 25 * R`，该公式在 R=0.5~0.8 的正常范围内系统性偏低 3~5 个百分点。Maxim MAX30102 官方参考使用二次多项式 `SpO2 = -45.060*R² + 30.354*R + 94.845`。

2. **AC 分量整数截断丢失精度**：`red_ac` 和 `ir_ac` 从浮点 RMS 值截断为 `uint32_t`，对于手腕弱信号场景，小数部分丢失导致 R 值产生显著偏差。

3. **缺少信号幅度校验**：`PPG_MIN_AMPLITUDE` 阈值已定义但从未使用。手腕测量信号弱时，噪声主导 AC 分量，使 RED 和 IR 的 AC/DC 比值趋于相同，R → 1.0，SpO2 → 85%。

4. **IIR 滤波器建立期样本参与 AC/DC 累加**：滤波器初始化后前 ~1 秒的输出处于瞬态响应，幅度不稳定，这些样本参与了 RMS 计算导致轻微偏差。

**后果**：
- SpO2 读数持续低于 90%，即使佩戴者血氧正常
- 频繁触发血氧低报警（假阳性）
- 报警系统反复进入实时模式，影响正常使用

**解决方案**：

1. **AC/DC 改用 float 存储**：`red_ac`、`red_dc`、`ir_ac`、`ir_dc` 从 `uint32_t` 改为 `float`，消除整数截断误差。
2. **替换为 Maxim 二次多项式校准公式**：`SpO2 = -45.060*R² + 30.354*R + 94.845`，在 R=0.5~0.8 范围比线性公式高 3~5%。
3. **添加信号幅度校验**：当 `ir_ac` 或 `red_ac` 低于 `PPG_MIN_AMPLITUDE`（100）时返回无效，避免噪声主导的虚假读数。
4. **跳过滤波器建立期**：前 25 个样本（1 秒 @25Hz）不参与 AC/DC 累加，等待 IIR 滤波器输出稳定。
5. **SpO2 结果四舍五入**：从 `(uint8_t)spo2` 改为 `(uint8_t)(spo2 + 0.5f)`，消除系统性向下取整偏差。
6. **增加诊断日志**：输出实际 R 值、AC/DC 分量值，便于后续调优。

**涉及文件**：
- `components/services/health_monitor/health_monitor.c`


---

## ISSUE-041：ds3231_get_time 偶发返回失败导致 OLED 无法显示时间

**发现日期**：2026-02-20

**原因**：
`ds3231_get_time()` 内部仅执行一次 `i2c_bus_read()`，无重试机制。在 I2C 总线繁忙（`sensor_task` 高频读取 MPU6050/MAX30102）或出现瞬态噪声时，单次读取可能因 mutex 超时或硬件 NAK 而失败，函数直接返回错误。

**后果**：
- Home 页面偶发无法获取 RTC 时间，显示不更新
- 语音"查询时间"命令可能播报错误时间（零值）

**解决方案**：
在 `ds3231_get_time()` 中对 `i2c_bus_read()` 添加重试逻辑，最多尝试 3 次，每次间隔 5ms。3 次均失败后才返回错误并输出警告日志。

**涉及文件**：
- `components/drivers/ds3231/ds3231.c`

---

## ISSUE-042：step_refresh_timer 每 500ms 读取 DS3231 导致持续 I2C 失败日志

**发现日期**：2026-02-20

**原因**：
`ui_manager.c` 的 `step_refresh_timer_callback()` 在 Home 页面时，每 500ms 调用一次 `ds3231_get_time()` 来检查分钟是否变化。但时间最快每 60 秒才变化一次，500ms 的 RTC 读取频率完全没有必要。高频 I2C 读取与 `sensor_task`（优先级 6，每 20ms 读取 MPU6050/MAX30102）产生严重总线竞争，导致 DS3231 的 3 次重试（ISSUE-041 添加）全部失败，持续输出 `"I2C read failed after 3 attempts: ESP_FAIL"` 警告日志。

**后果**：
- 串口日志每 500ms 输出一次 DS3231 读取失败警告，刷屏严重
- 不必要的 I2C 总线竞争消耗系统资源
- 实际时间显示不受影响（偶尔成功的读取足以更新分钟显示），但日志噪声掩盖了真正的问题

**解决方案**：
在 `step_refresh_timer_callback()` 中添加静态计数器 `s_rtc_check_counter`，每 20 次回调（10 秒）才读取一次 DS3231，其余回调跳过 RTC 读取。10 秒间隔足以保证分钟变化时及时刷新（最大延迟 10 秒），同时将 I2C 竞争降低 20 倍。

```c
if (page == UI_PAGE_HOME) {
    static uint8_t s_rtc_check_counter = 0;
    if (++s_rtc_check_counter >= 20) {
        s_rtc_check_counter = 0;
        ds3231_time_t rtc_time = {0};
        if (ds3231_get_time(&rtc_time) == ESP_OK) {
            if (s_home_last_minute != (int)rtc_time.minute) {
                draw_home_page();
            }
        }
    }
}
```

**涉及文件**：
- `components/ui_manager/ui_manager.c`

---

## ISSUE-043：UI 定时器回调阻塞 Timer Service 任务（I2C 读取 + OLED 全屏刷新）

**发现日期**：2026-02-20

**原因**：
`ui_manager.c` 中原有两个 FreeRTOS 软件定时器回调（`refresh_timer_callback` 2 分钟周期、`step_refresh_timer_callback` 500ms 周期）运行在 Timer Service 任务上下文中。两个回调均执行重量级操作：
1. I2C 读取 DS3231 RTC（`ds3231_get_time` → `i2c_bus_read`，含 3 次重试）
2. OLED 全屏绘制（`sh1106_clear` + 多次 `sh1106_draw_string` + `sh1106_update` 8 页 I2C 写入）
3. 手动测量状态机管理（倒计时/等待/结果三阶段）

Timer Service 任务是所有软件定时器共享的，回调执行期间会阻塞其他定时器（按键消抖、WS2812 闪烁等）。OLED 全屏刷新耗时约 10-30ms，加上 I2C 操作，单次回调可能占用 Timer Service 40ms 以上。

此外，`step_refresh_timer_callback` 的名称和注释（"仅刷新步数区域"）与实际职责不符——该回调实际承担 HOME 页 RTC 检查、STEPS 页步数刷新、MANUAL_MEASURE 页状态机管理三项工作。

**后果**：
- Timer Service 被 UI 操作阻塞，其他软件定时器响应延迟
- 按键消抖定时器可能因 Timer Service 繁忙而出现抖动
- 代码命名与实际功能不一致，增加维护难度

**解决方案**：
1. 删除两个 FreeRTOS 软件定时器（`s_refresh_timer` 和 `s_step_refresh_timer`）
2. 创建独立 `ui_task` 任务（栈 4096，优先级 2），完全接管所有 UI 定时刷新职责
3. 任务主循环使用 `xTaskNotifyWait` 等待 500ms 超时作为快速刷新周期
4. 内部计数器追踪 2 分钟全量刷新间隔（240 次 × 500ms = 120s）
5. HOME 页 RTC 检查计数器改为任务局部变量（非 static）
6. 宏 `UI_STEP_REFRESH_INTERVAL_MS` → `UI_FAST_REFRESH_INTERVAL_MS`
7. 更新所有注释和日志，反映实际多职责

**涉及文件**：
- `components/ui_manager/include/ui_manager.h`
- `components/ui_manager/ui_manager.c`

---

## ISSUE-045：主页温度显示更新滞后 — 最长 2 分钟才刷新

**发现日期**：2026-02-20

**原因**：
DS18B20 温度传感器每 30 秒采样一次（`SENSOR_TEMP_NORMAL_INTERVAL = 30000`），但 UI 主页仅在以下时机重绘：
1. 2 分钟全量刷新（`UI_REFRESH_INTERVAL_MS = 120000`）
2. RTC 分钟变化时（最长 60 秒）

两个刷新路径均不以温度更新为触发条件，导致新温度数据到达后最长需要等待 2 分钟才在屏幕上显示。

此外，系统启动时温度采样立即触发（`temp_last_sample = now - 30000`），但 DS18B20 需要 750ms 转换时间，而 UI 首次绘制在 `ui_manager_init()` 中同步执行，此时温度数据尚未就绪，导致启动后显示 `--.-C` 直到首次刷新。

**后果**：
- 用户在主页看到的温度值可能滞后 30 秒至 2 分钟
- 系统启动后温度显示为 `--.-C`，需等待较长时间才出现实际值

**解决方案**：

1. **UI 侧（`ui_manager.c`）**：在 `ui_task()` 中新增 30 秒温度刷新计数器（`temp_refresh_counter`，60 次 × 500ms = 30 秒），每 30 秒触发一次 HOME 页重绘。初始值设为 57，使启动约 1.5 秒后首次刷新，配合传感器 1 秒延迟 + 750ms 转换时间。

2. **传感器侧（`sensor_service.c`）**：将首次温度采样从立即触发改为延迟 1 秒（`temp_last_sample = now - 30000 + 1000`），确保系统各模块初始化完成后再开始 I2C 通信。

**涉及文件**：
- `components/services/sensor_service/sensor_service.c`
- `components/ui_manager/ui_manager.c`

**发现日期**：2026-02-20

**原因**：  
在 ISSUE-042 中，为降低 I2C 竞争，Home 页面改为每 10 秒读取一次 DS3231（`20 * 500ms`）。该策略虽然显著降低了 I2C 压力，但 UI 的时间更新完全依赖这次周期读取，导致显示分钟变化时可能滞后。  
同时，页面显示格式为 `HH:MM`（不显示秒），用户对“整分钟跳变时机”更敏感，延迟体感明显。

**后果**：
- Home 页面时间显示与真实时间存在 0~10 秒可见延迟（UI 延迟，不是 RTC 走时误差）
- 分钟跳变可能晚于真实时间，用户观感为“时间不准”
- 为追求更实时显示如果直接恢复高频 RTC 读取，会重新放大 I2C 竞争风险

**解决方案**：  
采用“低频硬件同步 + 高频本地推进”的混合策略：
1. 保留 DS3231 低频读取：每 10 秒读取一次，仅用于校准本地时间缓存（降低 I2C 占用）
2. 新增本地时间推进：每 1 秒在内存中将缓存时间 `+1s`（包含进位、跨日、闰年处理）
3. Home 页面分钟变化检测优先使用本地缓存时间，确保分钟跳变最大延迟 <= 1 秒
4. 当 RTC 读取失败时继续使用缓存时间，避免界面退化为 `--/-- RTC`
5. 离开 Home 页面时重置本地计数器，避免跨页面累计误差

**涉及文件**：
- `components/ui_manager/ui_manager.c`

---

## ISSUE-046：启动后 30 秒内温度显示 0°C — MEASURE_VALID 枚举零值 + UI 刷新间隔过长

**发现日期**：2026-02-20

**原因**：
两个问题叠加导致启动后 30 秒内温度持续显示 0：

1. **`MEASURE_VALID = 0` 与 memset 冲突**：`health_monitor.c:health_monitor_init()` 中 `memset(&s_ctx, 0, sizeof(s_ctx))` 将整个上下文清零，包括 `s_ctx.status.temperature = 0.0f` 和 `s_ctx.status.temp_validity = 0`。由于 `MEASURE_VALID` 的枚举值恰好是 0，清零后 UI 侧 `health_get_status()` 返回的 `temp_validity == MEASURE_VALID`，判定温度有效，显示 `0.0C` 而非预期的 `--.-C`。

2. **UI 温度刷新间隔 30 秒无条件等待**：`ui_manager.c` 中 `temp_refresh_counter` 初始值 56，首次触发在 ~2 秒（计数到 60），但此次可能仍在传感器数据到达前。首次触发后计数器重置为 0，下一次刷新需要再等 30 秒（60 × 500ms），期间即使真实温度已可用也不会更新显示。

**后果**：
- 系统启动后 OLED 主页温度区域显示 `0.0C` 长达 30 秒
- 实际 DS18B20 在启动约 2 秒后已采集到有效温度数据，但 UI 未及时刷新

**解决方案**：

1. **`health_monitor.c`**：`memset` 后显式设置 `s_ctx.status.temp_validity = MEASURE_INVALID_NO_SIGNAL`，使启动时 UI 显示 `--.-C` 而非误导性的 `0.0C`。

2. **`ui_manager.c`**：在温度刷新路径中，`draw_home_page()` 执行后检查温度有效性。若 `temp_validity != MEASURE_VALID`，将 `temp_refresh_counter` 设为 `temp_refresh_cycles - 6`（3 秒后重试），而非等待完整的 30 秒周期。确保真实温度数据一到达就能在数秒内显示。

```c
// health_monitor.c
memset(&s_ctx, 0, sizeof(s_ctx));
s_ctx.status.temp_validity = MEASURE_INVALID_NO_SIGNAL;

// ui_manager.c
if (++temp_refresh_counter >= temp_refresh_cycles) {
    temp_refresh_counter = 0;
    draw_home_page(...);
    if (health_get_status().temp_validity != MEASURE_VALID) {
        temp_refresh_counter = temp_refresh_cycles - 6;  // 3秒后重试
    }
}
```

**涉及文件**：
- `components/services/health_monitor/health_monitor.c`
- `components/ui_manager/ui_manager.c`

---

## ISSUE-047：传感器异常数据未过滤，直接参与事件发布和 BLE 上报

**发现日期**：2026-02-22

**原因**：
传感器采样层（`sensor_service.c`）和健康监测层（`health_monitor.c`）缺少对明显异常数据的丢弃机制。当传感器硬件故障、接触不良或极端环境干扰时，采集到的异常数据会直接写入 `latest_data`、通过事件总线发布、最终经 BLE Telemetry 上报给手机端。

具体缺失的校验：
1. **DS18B20 温度**：无范围校验，异常值（如断线返回 85°C 或 -127°C）直接存入
4. **MAX30102 心率/血氧**：计算结果缺少最终范围校验的防御层

**后果**：
- 手机端收到明显不合理的传感器数据（如体温 85°C、心率 5 bpm）
- 异常加速度数据可能触发误报跌倒检测
- 用户对设备数据可信度产生质疑

**解决方案**：

1. **DS18B20 温度异常丢弃**（`sensor_service.c:temp_read_result()`）：
   读取温度后检查 `temp < -30°C || temp > 50°C`，异常时不更新 `latest_data`，重置 `temp_last_sample = 0` 立即触发重新采样。

```c
if (temp < -30.0f || temp > 50.0f) {
    ESP_LOGW(TAG, "Temp out of range: %.2f°C, discarding", temp);
    s_ctx.temp_state = TEMP_STATE_IDLE;
    s_ctx.temp_last_sample = 0;
    return false;
}
```

2. **心率/血氧异常丢弃**（`health_monitor.c:on_sensor_data()`）：
   计算结果 HR < 20 或 > 200 时标记为 `MEASURE_INVALID_NO_SIGNAL` 不存入；SpO2 < 70% 时同理。

**涉及文件**：
- `components/services/sensor_service/sensor_service.c`
- `components/services/health_monitor/health_monitor.c`

---

## ISSUE-048：BLE Telemetry 固定 120 秒周期上报与心率血氧采集周期不匹配

**发现日期**：2026-02-22

**原因**：
BLE Telemetry 上报间隔固定为 120 秒（`BLE_TELEMETRY_INTERVAL_MS = 120000`），而心率血氧每 8 分钟才采集一次（15 秒窗口）。导致大部分上报时心率血氧数据尚未更新，传输的是旧数据或初始零值。同时 APP 端手动测量结束后立即发送 `REQUEST_REPORT` 命令，但此时 ESP32 可能尚未完成 HR/SpO2 计算，导致上报的仍是旧数据。

**后果**：
- 正常模式下绝大多数 Telemetry 包中的 HR/SpO2 数据不是最新的
- 手动测量后 APP 立即请求上报，拿到的数据可能不包含本次测量结果
- 浪费 BLE 传输带宽（大部分包无新 HR/SpO2 数据）

**解决方案**：

1. **事件驱动上报**：新增 `EVT_HR_RESULT_READY` 事件类型，`health_monitor` 在计算出有效数据（HR、SpO2、温度三者全部 `MEASURE_VALID`）后发布该事件，`ble_service` 订阅后唤醒 telemetry_task 立即上报。

2. **正常模式改为纯事件驱动**：`s_telemetry_interval_ms` 初始值和正常模式下均设为 `portMAX_DELAY`，不再定期发送，仅在收到 `EVT_HR_RESULT_READY` 事件通知时才发送。

3. **Flutter 端去除冗余请求**：删除 `dashboard_tab.dart` 中手动测量结束后的 `ble.requestReport()` 调用，避免重复发送。

**涉及文件**：
- `components/services/event_bus/include/event_bus.h`
- `components/services/health_monitor/health_monitor.c`
- `components/ble_gatt/ble_service.c`

---

## ISSUE-049：开机后 HOME 页需等待数十秒才能显示 DS3231 时间

**发现日期**：2026-02-22

**原因**：
1. `main.c` 开机时成功读取了 DS3231 RTC 时间，但只打印日志，未传递给 UI 缓存（`s_home_cached_time_valid` 仍为 `false`）
2. UI 任务中 `rtc_sync_counter` 初始值为 0，需累计 20 个循环（10 秒）才首次读取 RTC
3. sensor_service 启动后 MPU6050/MAX30102 高频占用 I2C 总线，导致 DS3231 读取重试失败，进一步延长等待

**后果**：用户开机后 HOME 页时间显示为空，需等待 10~30 秒才能看到时间，体验差。

**解决方案**：
- **方案 A**：新增 `ui_manager_set_rtc_cache()` 接口，在 `ui_manager_init()` 之后、`sensor_service_start()` 之前（I2C 总线空闲时），将 `main.c` 中已读取的 RTC 时间注入 UI 缓存
- **方案 B**：将 `rtc_sync_counter` 初始值改为 `UI_HOME_RTC_SYNC_CYCLES - 1`（19），UI 任务首次循环即触发 RTC 读取

```c
// ui_manager.c - 新增接口
void ui_manager_set_rtc_cache(const ds3231_time_t *time)
{
    if (time == NULL) return;
    if (ui_lock(pdMS_TO_TICKS(50))) {
        s_home_cached_time = *time;
        s_home_cached_time_valid = true;
        ui_unlock();
    }
}

// main.c - 注入缓存
if (rtc_valid) {
    ui_manager_set_rtc_cache(&rtc_time);
}

// ui_manager.c - 计数器初始值
uint8_t rtc_sync_counter = UI_HOME_RTC_SYNC_CYCLES - 1;
```

**涉及文件**：
- `components/ui_manager/include/ui_manager.h`
- `components/ui_manager/ui_manager.c`
- `main/main.c`
- `mobile_flutter/lib/ui/tabs/dashboard_tab.dart`
