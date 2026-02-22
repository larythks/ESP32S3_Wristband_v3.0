# ISSUE 日志

---
### ISSUE-001
- **发现日期**: 2026-02-20
- **原因**: 默认生成的 `test/widget_test.dart` 引用了 `MyApp` 类，但项目主入口类已重命名为 `CareBandApp`，导致 `flutter analyze` 报错 `The name 'MyApp' isn't a class`
- **后果**: `flutter analyze` 失败，无法通过静态分析检查
- **解决方案**: 将 `widget_test.dart` 中的 `MyApp` 替换为 `CareBandApp`，并将测试内容更新为验证应用可正常实例化
- **涉及文件**: `test/widget_test.dart`

---
### 代码审查结果 (2026-02-20)

针对以下方面进行了全面审查，未发现其他问题:

1. **BLE UUID**: 5 个特征 UUID (Service FF00, Telemetry FF01, Alarm FF02, Command FF03, Status FF04) 与固件定义完全一致
2. **字节偏移量**:
   - Telemetry (20B): temp@0(int16), hr@2(uint8), spo2@3(uint8), steps@4(uint32), battery@8(uint8), dataValid@9(uint8), timestamp@10(uint32) -- 全部正确
   - Alarm (16B): eventId@0(uint32), alarmType@4(uint8), value@5(int16), battery@7(uint8), timestamp@8(uint32) -- 全部正确
   - Status (3B): deviceState@0, bleConnCount@1, alarmState@2 -- 全部正确
3. **字节序**: 所有多字节字段均使用 `Endian.little`，与固件一致
4. **data_valid bitmap**: 温度检查 bit 0 (0x01)，心率和血氧共用 bit 1 (0x02) -- 正确
5. **命令构造**: ACK=9B, SYNC_TIME=9B, REQUEST_REPORT=5B, MANUAL_MEASURE=7B -- 长度和结构均正确
6. **测试覆盖**: 63 个测试全部通过，覆盖了解析器、命令构造器和数据模型

---
### ISSUE-002
- **发现日期**: 2026-02-21
- **原因**: `scan_page.dart` 的 Consumer builder 在每次 rebuild 时（包括收到遥测数据触发 `notifyListeners()` 时），只要 `connectionState == connected` 就调用 `Navigator.pushNamed('/device')`，导致每次数据更新都向导航栈推入新的 DevicePage
- **后果**: 导航栈累积大量 DevicePage 实例，用户按返回键逐个回退历史页面而非直接回到主界面
- **解决方案**: 在 `_ScanPageState` 中添加 `_navigatedToDevice` 标记，确保连接后仅导航一次；断连时重置标记。DevicePage 断连时使用 `Navigator.popUntil(ModalRoute.withName('/scan'))` 替代 `Navigator.pop()` 一步清除所有 DevicePage
- **涉及文件**: `lib/ui/scan_page.dart`, `lib/ui/device_page.dart`

---
### ISSUE-003
- **发现日期**: 2026-02-21
- **原因**: ScanPage 仅在 Consumer rebuild 且 `connectionState == connected` 时自动导航到 DevicePage，用户在蓝牙仍连接时按返回键回到 ScanPage 后，若没有新的 `notifyListeners()` 触发，Consumer 不会 rebuild，无法再进入设备页面。主界面缺少已连接设备的可视化入口
- **后果**: 用户未断开蓝牙时返回主界面后无法重新进入已连接设备的数据展示页面
- **解决方案**: 在 `BleProvider` 中添加 `connectedDeviceName` getter 暴露已连接设备名称；在 ScanPage 的 body 顶部，当 `ble.isConnected` 为 true 时显示带蓝牙图标的绿色 Card 按钮（显示设备名和「已连接 - 点击查看设备数据」），点击可导航到 DevicePage
- **涉及文件**: `lib/data/ble_provider.dart`, `lib/ui/scan_page.dart`

---
### ISSUE-004
- **发现日期**: 2026-02-21
- **原因**: `connect()` 中 `_connectionSub` 监听器在连接/配对过程开始前就设置，监听到 `disconnected` 事件时立即调用 `_handleDisconnect()`（设置 `_device = null`）。在 `_ensureAndroidBondReady()` 执行 `createBond()` 期间，Android 可能触发瞬态的 `disconnected` 事件，导致 `_device` 被清空。但 `connect()` 后续代码使用局部变量 `device` 继续完成服务发现和特征值订阅，最终 `_updateState(connected)` 将状态改回 `connected`。结果：UI 显示已连接且数据正常流入，但 `_device` 为 null
- **后果**: 用户点击断开连接时，`disconnect()` 检测到 `_device == null` 直接 early return，仅清理 Dart 状态而从未向 ESP32 发送 BLE 断连命令，ESP32 不会重新广播
- **解决方案**: 添加 `_isConnecting` 标志，在 `connect()` 执行期间设为 true，连接监听器中增加 `!_isConnecting` 条件，忽略连接过程中的瞬态断连事件。`_isConnecting` 在 `_updateState(connected)` 之前和 catch 块中重置为 false
- **涉及文件**: `lib/ble/ble_manager.dart`

---
### ISSUE-05
- **发现日期**: 2026-02-21
- **原因**: 迭代 4.2 集成验证时，`main.dart` 使用 `CardTheme(...)` 构造 `ThemeData.cardTheme`，但 Flutter 3.11+ 中该属性类型已改为 `CardThemeData?`，导致 `argument_type_not_assignable` 编译错误
- **后果**: `flutter analyze` 报 1 个 error，项目无法通过静态分析
- **解决方案**: 将 `CardTheme(` 改为 `CardThemeData(`
- **涉及文件**: `lib/main.dart`

---
### ISSUE-06
- **发现日期**: 2026-02-21
- **原因**: `health_card.dart` 中 `displaySubtitle` 变量已通过 `if (displaySubtitle != null)` 条件检查，在 Dart null safety 下已自动提升为非空类型，但代码仍使用 `displaySubtitle!` 非空断言
- **后果**: `flutter analyze` 报 `unnecessary_non_null_assertion` 警告
- **解决方案**: 移除多余的 `!` 操作符
- **涉及文件**: `lib/ui/widgets/health_card.dart`

---
### ISSUE-07
- **发现日期**: 2026-02-21
- **原因**: `BleProvider.startScan()` 中在调用 `_ble.startScan()` 之前就订阅了 `scanResultsStream`，`FlutterBluePlus.scanResults` 立即发送系统缓存的旧扫描结果填充 `_scanResults`。随后 `_ble.startScan()` 因设备仍连接而抛异常，但订阅未取消，旧结果留在列表中。同时 ScanPage 在已连接状态下仍显示扫描按钮，允许用户触发无效扫描
- **后果**: 已连接时点击搜索按钮，同时出现错误消息 "扫描失败: Device still connected" 和旧的扫描设备列表
- **解决方案**: (1) `startScan()` 中将 `scanResultsStream` 订阅移到 `_ble.startScan()` 成功之后，catch 中取消订阅并置 null；(2) ScanPage 的 AppBar actions 中增加 `!ble.isConnected` 条件，已连接时隐藏扫描按钮
- **涉及文件**: `lib/data/ble_provider.dart`, `lib/ui/scan_page.dart`

---
### ISSUE-08
- **发现日期**: 2026-02-21
- **原因**: `BleProvider.ackAlarm()` 仅向 BLE 设备发送 ACK 命令，未更新本地 `_alarmHistory` 中对应 `AlarmData.isAcked` 状态，也未调用 `notifyListeners()` 触发 UI 刷新
- **后果**: 用户在报警记录页面点击"确认"按钮后，命令已发送到设备，但 UI 仍显示"未确认"状态，不会变为"已确认"
- **解决方案**: `ackAlarm()` 成功发送命令后，遍历 `_alarmHistory` 找到匹配 `eventId` 的报警项设置 `isAcked = true`，然后调用 `notifyListeners()` 刷新 UI
- **涉及文件**: `lib/data/ble_provider.dart`

---
### ISSUE-09
- **发现日期**: 2026-02-21
- **原因**: `ScanPage` 使用 `SystemNavigator.pop()` 处理返回键，该方法在 Android 上调用 `Activity.finish()` 销毁 Activity。应用没有前台服务保活，系统在 Activity 销毁后很快回收进程，导致 BLE 连接断开
- **后果**: 用户在主界面按返回键退到后台时，App 进程被杀，蓝牙连接断开
- **解决方案**: (1) 新建 `BleKeepAliveService` 前台服务，BLE 连接时启动通知栏常驻服务防止进程被回收；(2) `MainActivity` 添加 MethodChannel 提供 `moveTaskToBack`/`startBleService`/`stopBleService` 三个原生方法；(3) `scan_page.dart` 返回键改为调用 `moveTaskToBack` 退到后台；(4) `BleProvider` 在连接成功时启动前台服务、断开时停止服务；(5) AndroidManifest 添加 `FOREGROUND_SERVICE`/`FOREGROUND_SERVICE_CONNECTED_DEVICE`/`POST_NOTIFICATIONS` 权限并声明 Service
- **涉及文件**: `android/.../BleKeepAliveService.kt`(新建), `android/.../MainActivity.kt`, `android/.../AndroidManifest.xml`, `lib/ui/scan_page.dart`, `lib/data/ble_provider.dart`

---
### ISSUE-010
- **发现日期**: 2026-02-21
- **原因**: `DevicePage._handleAlarmDialog()` 使用 `_lastShownAlarm` 引用对比来判断是否弹窗。每次重新进入 DevicePage 时 State 重建、`_lastShownAlarm` 重置为 null，导致 `latestAlarm` 无论是否已确认都会再次弹出。且该方法仅处理最新一条报警，无法弹出历史中其他未确认的报警
- **后果**: 每次重新进入设备界面时，已确认的报警仍然弹窗；同时只有最新一条报警会弹窗，之前的未确认报警被忽略
- **解决方案**: 用 `Set<int> _shownAlarmIds` 记录当前会话中已弹过弹窗的 eventId，用 `_isShowingDialog` 防止同时弹出多个对话框。`_showNextUnackedAlarm()` 从 `alarmHistory` 中查找第一条「未确认 且 未弹过」的报警弹窗展示，弹窗关闭后递归调用自身继续弹下一条，直到没有更多未确认报警
- **涉及文件**: `lib/ui/device_page.dart`

---
### ISSUE-011
- **发现日期**: 2026-02-21
- **原因**: `MqttGateway.publishTelemetry()` 和 `publishAlarm()` 中 `jsonEncode()` 位于 try-catch 外部。如果 `TelemetryData` 中包含 `NaN` 或 `Infinity` 值，`jsonEncode()` 抛出 `JsonUnsupportedObjectError`，该异常向上传播到 `BleProvider` 的 `_telemetrySub` / `_alarmSub` Stream 监听器，导致 StreamSubscription 永久取消。此后所有后续的 telemetry/alarm 数据均不再触发监听回调，MQTT 发布永远停止。同时 `_telemetryLogOnce` / `_alarmLogOnce` 标志导致仅首次发布有日志，后续发布无任何确认日志，`_publish()` 返回值未被检查
- **后果**: 连接后仅第一条数据能触发 publish 日志，后续所有 telemetry 和 alarm 数据均不上传且无错误日志；即使首条 publish 执行成功，云端可能因 QoS/网络问题未收到
- **解决方案**: (1) `publishTelemetry()` 和 `publishAlarm()` 整体包裹在 try-catch 中，捕获 jsonEncode 和所有其他异常; (2) `BleProvider` 中 `_mqtt.publishTelemetry()` / `_mqtt.publishAlarm()` 调用也加 try-catch 保护，防止异常杀死 Stream 监听器; (3) 移除 `_logOnce` 标志，每次 publish 均打印 msgId 和关键数据; (4) `_publish()` 返回 `int?`，调用方可检查是否成功
- **涉及文件**: `lib/mqtt/mqtt_gateway.dart`, `lib/data/ble_provider.dart`

---
### ISSUE-012
- **发现日期**: 2026-02-22
- **原因**: 设备连接成功后，BleProvider 仅启动 BLE 前台服务和 MQTT，未主动向设备请求上报最新数据。用户必须手动进入设置页面点击"请求立即上报"按钮才能获取首次数据
- **后果**: 连接后健康数据卡片全部显示 `--`，用户体验差，需要额外手动操作才能看到数据
- **解决方案**: 在 `BleProvider` 的连接状态监听器中，当状态变为 `connected` 时，延迟 500ms 后自动调用 `requestReport()`，确保设备服务发现和时间同步完成后再请求上报
- **涉及文件**: `lib/data/ble_provider.dart`

---
### ISSUE-013
- **发现日期**: 2026-02-22
- **原因**: `DashboardTab._startMeasure()` 中的 15 秒倒计时结束后，仅将 `_isMeasuring` 设为 false，未向设备请求上报测量结果。手动测量的数据需要设备主动上报或 App 主动请求才能收到
- **后果**: 用户点击手动测量并等待 15 秒后，App 不会自动获取到新的测量数据，用户需手动到设置页面点击请求上报
- **解决方案**: 在 `_startMeasure()` 的计时器回调中，当 `_countdown <= 0` 时，在设置 `_isMeasuring = false` 之后立即调用 `ble.requestReport()` 自动请求最新数据
- **涉及文件**: `lib/ui/tabs/dashboard_tab.dart`

---
### ISSUE-014
- **发现日期**: 2026-02-22
- **原因**: `ScanPage` 在 `filteredResults.isEmpty && !isScanning` 时统一显示"点击搜索按钮开始扫描"提示，但当设备已连接时 AppBar 不显示搜索按钮（`!ble.isConnected` 条件隐藏），导致提示文字与实际 UI 不一致
- **后果**: 用户从设备页面返回主界面后，看到"点击搜索按钮开始扫描"但找不到搜索按钮，造成困惑
- **解决方案**: 在空状态提示文字的条件判断中增加 `ble.isConnected` 分支，已连接时显示"设备已连接"替代原有提示
- **涉及文件**: `lib/ui/scan_page.dart`

---
### ISSUE-015
- **发现日期**: 2026-02-22
- **原因**: `DatabaseHelper.cleanup24h()` 使用 `DateTime.now().subtract(Duration(hours: 24))` 作为清理截止时间，保留的是最近 24 小时的数据而非当天的数据。例如当天凌晨 1 点时仍保留前一天 1 点之后的数据，数据边界不清晰
- **后果**: 数据保留范围为滑动的 24 小时窗口，而非自然日（当天 00:00~23:59），与预期的"只保存当天数据"不一致
- **解决方案**: 将 `cleanup24h()` 重命名为 `cleanupBeforeToday()`，cutoff 从 `now - 24h` 改为 `DateTime(now.year, now.month, now.day)` 即当天零点，删除零点之前的所有记录
- **涉及文件**: `lib/data/database_helper.dart`, `lib/data/sqlite_repository.dart`

---
### ISSUE-016
- **发现日期**: 2026-02-22
- **原因**: `TrendChart` 的 Y 轴 `SideTitles` 未设置 `interval`，fl_chart 自动生成刻度标签间距过密；X 轴 `_xLabelInterval` 分级不足，数据点多时标签间距仍不够
- **后果**: 当数据值接近时（如体温 36.5~36.8），Y 轴标签数值重叠无法辨认；X 轴数据量大时时间标签互相遮挡
- **解决方案**: (1) Y 轴 `SideTitles` 添加 `interval: _gridInterval(minY, maxY)` 使标签间距与网格线一致；(2) Y 轴 `getTitlesWidget` 中过滤与边界距离小于 `interval * 0.4` 的标签，防止边界值与刻度挤在一起；(3) X 轴 `_xLabelInterval` 增加更多分级（count≤12→2, ≤20→4, ≤40→8, >40→10）
- **涉及文件**: `lib/ui/widgets/trend_chart.dart`

---
### ISSUE-017
- **发现日期**: 2026-02-22
- **原因**: 通知点击回调中使用 `pushNamedAndRemoveUntil('/device', ...)` 创建新的 DevicePage 实例，旧 DevicePage 在退场动画期间仍处于 mounted 状态。新旧两个 DevicePage 的 `_shownAlarmIds` 独立（新实例为空集），导致双方各自为同一条未确认报警弹出 showDialog，形成两层叠加弹窗。顶层弹窗的 `onAck`/`onDismiss` 回调捕获了旧 DevicePage 的 Consumer `context`（已脱离有效导航树），`Navigator.of(context).pop()` 无法关闭弹窗；底层弹窗的 context 有效，按钮功能正常
- **后果**: 从通知返回 APP 后，警报弹窗的确认/忽略按钮无法关闭弹窗，只能通过 Android 返回键退出，返回键后还有一个弹窗（底层弹窗按钮正常）
- **解决方案**: (1) `main.dart` 中将 `pushNamedAndRemoveUntil` 改为 `popUntil`，回到已有的 DevicePage 而非创建新实例；(2) `device_page.dart` 中将 `_showNextUnackedAlarm` 等副作用从 Consumer builder 移到 `addListener` 监听器中，避免 build 时触发副作用；(3) `showDialog` 的 builder 使用 `dialogContext` 而非外层 `context` 进行 `Navigator.pop`；(4) `pendingTabIndex` 改为 getter/setter 并在赋值时触发 `notifyListeners()`，确保监听器能响应通知点击
- **涉及文件**: `lib/main.dart`, `lib/ui/device_page.dart`, `lib/data/ble_provider.dart`

---
### ISSUE-018
- **发现日期**: 2026-02-22
- **原因**: `BleProvider._initRepository()` 是异步方法，在构造函数中 fire-and-forget 调用。如果 BLE 报警在 `_initRepository` 完成前到达，报警数据被添加到内存 `_alarmHistory` 列表 A 中，但因 `_repoReady=false` 未持久化到 SQLite。当 `_initRepository` 完成后，用 SQLite 加载的数据**直接替换** `_alarmHistory`（列表 B），列表 A 中的报警数据丢失。后续 `ackAlarm` 遍历列表 B 找不到该报警的 `eventId`，`isAcked` 无法设置为 true，UI 始终显示"未确认"。同时 `ackAlarm` 原实现在 BLE 发送失败时才设置 `_errorMessage`，找不到报警记录时无任何反馈
- **后果**: 特定时序下（ESP32 端先按停止报警键再在 APP 端确认），报警记录永远显示"未确认"状态，且后续所有报警记录也可能受影响
- **解决方案**: (1) `_initRepository` 改为合并策略：加载 SQLite 数据后，将初始化前收到的内存数据（按 eventId 去重）追加到列表中，并补写入 SQLite；(2) `ackAlarm` 改为乐观更新：先设置本地 `isAcked=true` 并通知 UI，再异步发送 BLE 命令和更新 SQLite；找不到记录时返回 false 并设置 `_errorMessage`；(3) `alarm_tab.dart` 中 ackAlarm 调用改为 await 并根据返回值显示 SnackBar 错误提示
- **涉及文件**: `lib/data/ble_provider.dart`, `lib/ui/tabs/alarm_tab.dart`
