import 'package:flutter_local_notifications/flutter_local_notifications.dart';
import '../data/models.dart';

/// 通知点击回调类型
typedef NotificationTapCallback = void Function(String? payload);

class NotificationService {
  static NotificationService? _instance;
  static NotificationService get instance {
    _instance ??= NotificationService._();
    return _instance!;
  }

  NotificationService._();

  final FlutterLocalNotificationsPlugin _plugin =
      FlutterLocalNotificationsPlugin();
  bool _initialized = false;
  NotificationTapCallback? _onTap;

  /// 设置通知点击回调
  set onTap(NotificationTapCallback? callback) => _onTap = callback;

  Future<void> init() async {
    if (_initialized) return;

    // Android 通知 channel
    const androidChannel = AndroidNotificationChannel(
      'alarm_channel',
      '报警通知',
      description: '手环报警推送通知',
      importance: Importance.high,
      enableVibration: false,
      playSound: true,
    );

    // 创建 channel
    final androidPlugin =
        _plugin.resolvePlatformSpecificImplementation<
            AndroidFlutterLocalNotificationsPlugin>();
    await androidPlugin?.createNotificationChannel(androidChannel);

    // 请求 Android 13+ 通知权限
    await androidPlugin?.requestNotificationsPermission();

    const androidSettings = AndroidInitializationSettings(
      '@mipmap/ic_launcher',
    );
    const initSettings = InitializationSettings(android: androidSettings);

    await _plugin.initialize(
      initSettings,
      onDidReceiveNotificationResponse: _handleTap,
    );

    _initialized = true;
  }

  void _handleTap(NotificationResponse response) {
    _onTap?.call(response.payload);
  }

  /// 显示报警通知
  Future<void> showAlarmNotification(AlarmData alarm) async {
    if (!_initialized) return;

    final (title, body) = _alarmContent(alarm);

    const androidDetails = AndroidNotificationDetails(
      'alarm_channel',
      '报警通知',
      channelDescription: '手环报警推送通知',
      importance: Importance.high,
      priority: Priority.high,
      enableVibration: false,
      autoCancel: true,
      category: AndroidNotificationCategory.alarm,
    );
    const details = NotificationDetails(android: androidDetails);

    await _plugin.show(
      alarm.eventId % 0x7FFFFFFF,
      title,
      body,
      details,
      payload: 'alarm_tab',
    );
  }

  /// 取消指定通知
  Future<void> cancelNotification(int eventId) async {
    await _plugin.cancel(eventId % 0x7FFFFFFF);
  }

  /// 根据报警类型生成通知标题和正文
  (String title, String body) _alarmContent(AlarmData alarm) {
    switch (alarm.alarmType) {
      case AlarmType.tempHigh:
        return (
          '环境温度过高警报',
          '当前温度 ${alarm.triggerValue.toStringAsFixed(1)}°C，超过安全阈值',
        );
      case AlarmType.tempLow:
        return (
          '环境温度过低警报',
          '当前温度 ${alarm.triggerValue.toStringAsFixed(1)}°C，低于安全阈值',
        );
      case AlarmType.heartRateHigh:
        return (
          '心率过高警报',
          '当前心率 ${alarm.triggerValue.toInt()} bpm，超过安全阈值',
        );
      case AlarmType.heartRateLow:
        return (
          '心率过低警报',
          '当前心率 ${alarm.triggerValue.toInt()} bpm，低于安全阈值',
        );
      case AlarmType.spo2Low:
        return (
          '血氧过低警报',
          '当前血氧 ${alarm.triggerValue.toInt()}%，低于安全阈值',
        );
      case AlarmType.fall:
        return ('跌倒警报', '检测到疑似跌倒，请及时确认');
      case AlarmType.manual:
        return ('手动求助警报', '佩戴者发出手动求助信号');
      case AlarmType.callFamily:
        return ('呼叫家人', '佩戴者请求联系家人');
      case AlarmType.reserved:
        return ('血氧偏低预警', '当前血氧 ${alarm.triggerValue.toInt()}%，请关注');
      case AlarmType.none:
        return ('报警通知', '收到报警事件');
    }
  }
}
