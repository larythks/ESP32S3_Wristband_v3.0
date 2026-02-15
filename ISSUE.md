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
