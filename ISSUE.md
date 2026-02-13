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
