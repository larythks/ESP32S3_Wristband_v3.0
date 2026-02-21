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
### ISSUE-009
- **发现日期**: 2026-02-21
- **原因**: `connect()` 中 `_connectionSub` 监听器在连接/配对过程开始前就设置，监听到 `disconnected` 事件时立即调用 `_handleDisconnect()`（设置 `_device = null`）。在 `_ensureAndroidBondReady()` 执行 `createBond()` 期间，Android 可能触发瞬态的 `disconnected` 事件，导致 `_device` 被清空。但 `connect()` 后续代码使用局部变量 `device` 继续完成服务发现和特征值订阅，最终 `_updateState(connected)` 将状态改回 `connected`。结果：UI 显示已连接且数据正常流入，但 `_device` 为 null
- **后果**: 用户点击断开连接时，`disconnect()` 检测到 `_device == null` 直接 early return，仅清理 Dart 状态而从未向 ESP32 发送 BLE 断连命令，ESP32 不会重新广播
- **解决方案**: 添加 `_isConnecting` 标志，在 `connect()` 执行期间设为 true，连接监听器中增加 `!_isConnecting` 条件，忽略连接过程中的瞬态断连事件。`_isConnecting` 在 `_updateState(connected)` 之前和 catch 块中重置为 false
- **涉及文件**: `lib/ble/ble_manager.dart`

---
### ISSUE-010
- **发现日期**: 2026-02-21
- **原因**: 迭代 4.2 集成验证时，`main.dart` 使用 `CardTheme(...)` 构造 `ThemeData.cardTheme`，但 Flutter 3.11+ 中该属性类型已改为 `CardThemeData?`，导致 `argument_type_not_assignable` 编译错误
- **后果**: `flutter analyze` 报 1 个 error，项目无法通过静态分析
- **解决方案**: 将 `CardTheme(` 改为 `CardThemeData(`
- **涉及文件**: `lib/main.dart`

---
### ISSUE-011
- **发现日期**: 2026-02-21
- **原因**: `dashboard_tab.dart` 中 `import '../../data/models.dart'` 未被使用（TrendMetric 枚举定义在 trend_chart.dart 内部，TelemetryData 通过 BleProvider 间接访问）
- **后果**: `flutter analyze` 报 `unused_import` 警告
- **解决方案**: 移除未使用的 import 语句
- **涉及文件**: `lib/ui/tabs/dashboard_tab.dart`

---
### ISSUE-012
- **发现日期**: 2026-02-21
- **原因**: `health_card.dart` 中 `displaySubtitle` 变量已通过 `if (displaySubtitle != null)` 条件检查，在 Dart null safety 下已自动提升为非空类型，但代码仍使用 `displaySubtitle!` 非空断言
- **后果**: `flutter analyze` 报 `unnecessary_non_null_assertion` 警告
- **解决方案**: 移除多余的 `!` 操作符
- **涉及文件**: `lib/ui/widgets/health_card.dart`

---
### ISSUE-013
- **发现日期**: 2026-02-21
- **原因**: `BleProvider.connectDevice()` 中取消了扫描订阅 (`_scanResultsSub?.cancel()`)，但未清空 `_scanResults` 列表。连接成功后 ScanPage 被 push 到导航栈底，用户从 DevicePage 按返回键回到 ScanPage 时，`ble.isConnected` 为 true 显示"已连接设备"卡片，同时 `_scanResults` 仍保留旧数据导致 ListView 继续渲染扫描设备列表
- **后果**: 返回 ScanPage 后同时显示已连接设备入口卡片和扫描设备列表，造成"扫描界面与主界面混淆"的观感
- **解决方案**: 在 `connectDevice()` 中设置 `_connectedDeviceName` 后立即执行 `_scanResults = []` 清空扫描结果，再调用 `notifyListeners()`
- **涉及文件**: `lib/data/ble_provider.dart`
