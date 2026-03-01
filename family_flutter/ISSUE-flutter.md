# Family Flutter Issue 记录

本文件记录 family_flutter 项目开发过程中发现和修复的问题。

---

## ISSUE-FAMILY-001

- **发现日期**: 2026-03-01
- **原因**: 全项目 10+ 文件硬编码 Color 值（`Color(0xFF4CAF50)`, `Color(0xFFE63946)`, `Color(0xFF6C757D)` 等），散布于 dashboard_tab、health_card、settings_tab、alarm_tab、alarm_card、alarm_dialog、alarm_stats、anomaly_tips、summary_card、trend_chart
- **后果**: 无法统一修改主题色；深色模式适配不可能；违反 DRY 原则；跌倒和手动报警共用同一红色无法区分
- **解决方案**: 新建 `lib/theme/app_colors.dart` 集中定义语义化颜色常量（AppColors），全部 widget 引用 AppColors 替代硬编码值。跌倒报警改为紫色（#8B5CF6）与手动报警红色区分
- **涉及文件**: `lib/theme/app_colors.dart`(NEW), `lib/data/models.dart`, `lib/ui/tabs/dashboard_tab.dart`, `lib/ui/widgets/health_card.dart`, `lib/ui/tabs/alarm_tab.dart`, `lib/ui/widgets/alarm_card.dart`, `lib/ui/widgets/alarm_dialog.dart`, `lib/ui/widgets/alarm_stats.dart`, `lib/ui/tabs/settings_tab.dart`, `lib/ui/widgets/trend_chart.dart`, `lib/ui/widgets/summary_card.dart`, `lib/ui/widgets/anomaly_tips.dart`

---

## ISSUE-FAMILY-002

- **发现日期**: 2026-03-01
- **原因**: 全项目零 Semantics widget，图标按钮无 tooltip，图表无语义描述
- **后果**: 屏幕阅读器（TalkBack/VoiceOver）无法识别健康数据含义；不满足 WCAG AA 标准；医疗健康类应用可访问性要求更高
- **解决方案**: 在所有关键 UI 组件添加 Semantics widget：HealthCard（指标+数值+状态）、AlarmCard（类型+触发值+时间+确认状态）、AlarmDialog（报警详情）、DeviceStatusBar（设备状态）、TrendChart（excludeSemantics+描述label）、SummaryCard（日期+摘要）、AnomalyTipTile（异常描述）、BindingPage（页面标识）
- **涉及文件**: `lib/ui/widgets/health_card.dart`, `lib/ui/tabs/dashboard_tab.dart`, `lib/ui/widgets/alarm_card.dart`, `lib/ui/widgets/alarm_dialog.dart`, `lib/ui/widgets/alarm_stats.dart`, `lib/ui/widgets/trend_chart.dart`, `lib/ui/widgets/summary_card.dart`, `lib/ui/widgets/anomaly_tips.dart`, `lib/ui/binding_page.dart`

---

## ISSUE-FAMILY-003

- **发现日期**: 2026-03-01
- **原因**: ACK 按钮使用 `tapTargetSize: MaterialTapTargetSize.shrinkWrap` 和 `minimumSize: Size.zero`，FilterChip 未设定触控目标大小
- **后果**: 触控目标小于 48dp 推荐值，老年用户家属操作困难，不满足 WCAG 触控目标标准
- **解决方案**: ACK 按钮改为 `MaterialTapTargetSize.padded`，移除 `minimumSize: Size.zero`；FilterChip 添加 `materialTapTargetSize: MaterialTapTargetSize.padded`
- **涉及文件**: `lib/ui/widgets/alarm_card.dart`, `lib/ui/tabs/alarm_tab.dart`

---

## ISSUE-FAMILY-004

- **发现日期**: 2026-03-01
- **原因**: `_formatTime`/`_pad`/`_formatSteps` 等格式化函数在 dashboard_tab.dart、settings_tab.dart、alarm_card.dart、alarm_dialog.dart、anomaly_tips.dart、summary_card.dart 中重复定义
- **后果**: 代码重复，维护成本高，格式不一致风险
- **解决方案**: 新建 `lib/utils/formatters.dart` 统一提供 6 个格式化函数（formatShortTime/formatDateTime/formatFullDateTime/formatDateTimeMinute/formatDateChinese/formatSteps），删除各文件中的本地重复实现
- **涉及文件**: `lib/utils/formatters.dart`(NEW), `lib/ui/tabs/dashboard_tab.dart`, `lib/ui/tabs/settings_tab.dart`, `lib/ui/widgets/alarm_card.dart`, `lib/ui/widgets/alarm_dialog.dart`, `lib/ui/widgets/anomaly_tips.dart`, `lib/ui/widgets/summary_card.dart`

---

## ISSUE-FAMILY-005

- **发现日期**: 2026-03-01
- **原因**: 主题配置内联在 main.dart 中（约 25 行），无 Google Fonts 集成，使用系统默认字体
- **后果**: 主题修改需直接改 main.dart，数值显示无等宽字体导致对齐不美观，中文渲染依赖系统字体
- **解决方案**: 新建 `lib/theme/app_theme.dart`，提供 `buildAppTheme()` 函数，集成 Google Fonts（JetBrains Mono 数据字体 + Noto Sans SC 中文正文），main.dart 仅调用 `theme: buildAppTheme()`
- **涉及文件**: `lib/theme/app_theme.dart`(NEW), `lib/main.dart`, `pubspec.yaml`

---

## ISSUE-FAMILY-006

- **发现日期**: 2026-03-01
- **原因**: Provider 的 refreshHistory/loadTrendData/loadAlarmStats 无 loading 状态标志，UI 无法区分"加载中"和"无数据"
- **后果**: 用户首次进入页面看到空白或 '--' 值，无法判断是数据加载中还是确实无数据
- **解决方案**: DeviceProvider 新增 `isLoading`，HealthAnalysisProvider 新增 `isLoadingTrend`/`isLoadingStats`，使用 try/catch/finally 模式管理状态
- **涉及文件**: `lib/providers/device_provider.dart`, `lib/providers/health_analysis_provider.dart`

---

## ISSUE-FAMILY-007

- **发现日期**: 2026-03-01
- **原因**: 全项目零动画效果，页面切换无过渡，列表无入场动画，数值更新无视觉反馈
- **后果**: 应用感觉僵硬、缺乏精致感，用户体验偏低
- **解决方案**: 新建 `AnimatedListItem` 交错入场动画组件（fade+slide-up），应用于 HealthCard 网格、报警列表、摘要卡片、设置页各节；HealthCard 数值使用 AnimatedSwitcher 平滑切换；报警统计展开/收起改用 AnimatedCrossFade；主题添加 FadeUpwardsPageTransitionsBuilder
- **涉及文件**: `lib/ui/widgets/animated_list_item.dart`(NEW), `lib/ui/tabs/dashboard_tab.dart`, `lib/ui/widgets/health_card.dart`, `lib/ui/tabs/alarm_tab.dart`, `lib/ui/tabs/trend_tab.dart`, `lib/ui/tabs/settings_tab.dart`, `lib/theme/app_theme.dart`

---

## ISSUE-FAMILY-008

- **发现日期**: 2026-03-01
- **原因**: `lib/utils/formatters.dart` 顶部使用 `///` 文档注释但未关联到 `library` 声明
- **后果**: `flutter analyze` 报 `dangling_library_doc_comments` info 级别 warning
- **解决方案**: 将 `///` 文档注释改为 `//` 普通注释
- **涉及文件**: `lib/utils/formatters.dart`

---

## ISSUE-FAMILY-008

- **发现日期**: 2026-03-01
- **原因**: `trend_chart.dart` 的 `_getMaxY()` 使用 `maxVal * 0.05`（百分比留白）计算 Y 轴上界。当数据最大值（如 126）与阈值线（如 120）接近时，5% 留白仅约 6px，导致阈值虚线与 Y 轴上界数值标签视觉重合
- **后果**: 心率趋势图中 120 bpm 红色警戒线与最高数据点 126 的上界标签拥挤重合，观感差，难以区分阈值与数据边界
- **解决方案**: 改用绝对值留白策略——取数据范围的 15% 与固定最小值 5 中的较大者作为 padding，确保阈值线与 Y 轴边界之间始终有充足间距
- **涉及文件**: `lib/ui/widgets/trend_chart.dart`

---

## ISSUE-FAMILY-009

- **发现日期**: 2026-03-01
- **原因**: `device_provider.dart` 的 `_onAlarm()` 中有 `if (!_isAppInForeground)` 条件判断，仅在 App 处于后台时才发送系统通知
- **后果**: 用户在 App 前台时收到报警不会有系统通知；若用户切到其他 App 或锁屏，理论上可以收到通知，但对于健康报警类应用应始终发送通知以确保不遗漏
- **解决方案**: 移除前后台判断条件，收到报警时始终调用 `NotificationService.instance.showAlarmNotification(record)` 发送系统通知
- **涉及文件**: `lib/providers/device_provider.dart`

---

## ISSUE-FAMILY-010

- **发现日期**: 2026-03-01
- **原因**: family_flutter 家属端缺少手动测量、请求立即上报、同步时间三个远程操作功能，而 mobile_flutter 佩戴端已实现通过 BLE 直接控制设备
- **后果**: 家属无法远程触发设备测量、无法主动要求设备上报最新数据、无法远程同步手环时间，远程监护能力不完整
- **解决方案**: 在 `MqttSubscriber` 新增 `publishSyncTime()`/`publishRequestReport()`/`publishManualMeasure()` 三个 MQTT 命令发布方法；`DeviceProvider` 新增对应封装方法；设置页新增"远程操作"卡片（同步时间 + 请求上报）；仪表盘页新增手动测量按钮（含 15 秒倒计时）。命令通过 MQTT `cmd` topic 发送，mobile_flutter 端接收后转发为 BLE 命令
- **涉及文件**: `lib/mqtt/mqtt_subscriber.dart`, `lib/providers/device_provider.dart`, `lib/ui/tabs/settings_tab.dart`, `lib/ui/tabs/dashboard_tab.dart`

---

## ISSUE-FAMILY-011

- **发现日期**: 2026-03-01
- **原因**: 设置页的通知开关（`notify_enabled`）和振动开关（`vibrate_enabled`）仅保存到 SharedPreferences，但 `DeviceProvider._onAlarm()` 中直接调用 `NotificationService.showAlarmNotification()` 未读取这两个偏好值；`AndroidNotificationDetails` 中 `enableVibration: true` 硬编码
- **后果**: 无论用户是否关闭报警通知，报警后始终弹出系统通知；无论用户是否开启振动提醒，报警后始终无法按偏好控制振动行为
- **解决方案**: `NotificationService.showAlarmNotification()` 新增 `enableVibration` 参数（替代硬编码 `true`）；`DeviceProvider._onAlarm()` 改为调用 `_handleAlarmNotification()` 异步方法，先从 SharedPreferences 读取 `notify_enabled`（为 false 则跳过通知）和 `vibrate_enabled`，再传入 `showAlarmNotification()`
- **涉及文件**: `lib/services/notification_service.dart`, `lib/providers/device_provider.dart`

---

## ISSUE-FAMILY-012

- **发现日期**: 2026-03-01
- **原因**: 1) `AndroidManifest.xml` 缺少 `android.permission.VIBRATE` 权限声明，导致系统无法触发振动；2) Android 8+ 通知渠道一经创建设置不可变，仅靠 `AndroidNotificationDetails.enableVibration` 参数无法在同一渠道内切换振动行为
- **后果**: 即使通知开关和振动开关的偏好读取逻辑正确，振动仍然始终不生效（缺权限）；且单渠道方案无法真正切换振动开/关
- **解决方案**: 1) `AndroidManifest.xml` 添加 `<uses-permission android:name="android.permission.VIBRATE"/>`；2) `NotificationService.init()` 创建两个渠道——`careband_alarm`（带振动）和 `careband_alarm_quiet`（无振动），`showAlarmNotification()` 根据 `enableVibration` 参数选择对应渠道
- **涉及文件**: `android/app/src/main/AndroidManifest.xml`, `lib/services/notification_service.dart`
