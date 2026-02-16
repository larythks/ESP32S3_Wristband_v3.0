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

## ISSUE-008：BLE 配对始终失败（首次配对无法成功）

**发现日期**：2026-02-15

**原因**：
`sdkconfig` 中 `CONFIG_BT_NIMBLE_SM_SC_LVL` 被配置为 `1`。在 NimBLE 中该级别对应“无安全级别”，会在配对流程中返回 `BLE_SM_ERR_CMD_NOT_SUPP`，导致手机发起配对时被协议栈拒绝。  
同时，代码中广播固定使用 `BLE_OWN_ADDR_PUBLIC`，未使用 `ble_hs_id_infer_auto()` 推断出的地址类型；在部分设备组合下会增加连接/安全流程不稳定风险。

**后果**：
- 手机端始终提示“配对失败”，历史上从未成功建立 bonded 关系
- `Command(FF03)` 的加密写入链路无法按预期建立，安全功能不可用
- 后续若 bond 存储达到上限（`MAX_BONDS=1`），重配对稳定性进一步下降

**解决方案**：
1. 将 `CONFIG_BT_NIMBLE_SM_SC_LVL` 从 `1` 调整为 `2`，允许 NoInputNoOutput（Just Works）场景下完成未认证加密配对。
2. 广播启动时改为使用 `ble_hs_id_infer_auto()` 推断得到的 `own_addr_type`，并在 `on_sync` 中先调用 `ble_hs_util_ensure_addr(0)` 确保地址可用。
3. 增加 `ble_hs_cfg.store_status_cb = ble_store_util_status_rr`，在 bond 满额时可自动回收旧记录，避免后续重复配对失败。

```c
// 关键修复
CONFIG_BT_NIMBLE_SM_SC_LVL=2

ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
ble_gap_adv_start(s_own_addr_type, ...);
```

**涉及文件**：`sdkconfig`、`components/ble_gatt/ble_service.c`

---

## ISSUE-009：Timer Service 和事件分发任务栈大小不足，可能导致栈溢出

**发现日期**：2026-02-16

**原因**：
迭代 3.1 引入 alarm_manager 后，Timer Service 任务的回调调用链显著加深：`pre_alarm_timer_cb` → `enter_state(ALARMING)` → `ws2812_blink_start`（RMT 传输）+ `send_ble_alarm`（`ble_notify_alarm` → NimBLE 内部）+ `publish_alarm_state_event`（`event_publish` → `xQueueSend`）。同样，事件分发任务中 `on_health_alert` → `alarm_trigger` → `enter_state` 也有相同深度的调用链。原有的 4096 字节栈在减去 FreeRTOS TCB 开销和局部变量（`ble_alarm_t` 16B + `event_data_t` 48B 等）后，安全余量不足。

此外，SW1 按键回调运行在 Timer Service 任务上下文中（通过 button.c 的 FreeRTOS 定时器消抖），`sw1_callback` → `alarm_trigger` → `enter_state` 的调用链与上述问题叠加。

**后果**：
- 极端情况下 Timer Service 任务或事件分发任务可能发生栈溢出，导致系统不可预测的崩溃或重启
- 问题难以调试（栈溢出通常不产生明确错误信息）

**解决方案**：
将两个任务的栈大小从 4096 增大到 6144 字节，提供充足的安全余量：

```c
// sdkconfig
CONFIG_FREERTOS_TIMER_TASK_STACK_DEPTH=6144  // 原 4096

// event_bus.c
#define EVENT_DISPATCH_TASK_STACK   6144  // 原 4096
```

额外 RAM 开销：2KB × 2 = 4KB，ESP32-S3 有 512KB SRAM，影响可忽略。

**涉及文件**：`sdkconfig`、`components/services/event_bus/event_bus.c`

---

## ISSUE-010：ALARMING 状态下新报警不发布 EVT_ALARM_STATE 事件

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

**涉及文件**：`components/services/alarm_manager/alarm_manager.c`
