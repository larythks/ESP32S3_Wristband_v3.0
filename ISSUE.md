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
