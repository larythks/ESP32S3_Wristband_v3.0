import 'package:flutter/foundation.dart';
import 'package:flutter_local_notifications/flutter_local_notifications.dart';

import '../data/models.dart';

/// 通知服务（单例）：管理本地报警通知
class NotificationService {
  NotificationService._();
  static final NotificationService instance = NotificationService._();

  final FlutterLocalNotificationsPlugin _plugin =
      FlutterLocalNotificationsPlugin();
  bool _initialized = false;

  /// 通知点击回调（payload 格式: alarm_{eventId}）
  void Function(String? payload)? onTap;

  /// 初始化通知插件和渠道
  Future<void> init() async {
    if (_initialized) return;

    final androidPlugin = _plugin.resolvePlatformSpecificImplementation<
        AndroidFlutterLocalNotificationsPlugin>();

    // 创建两个 Android 通知渠道（Android 8+ 渠道一经创建设置不可变）
    // 渠道 1：带振动
    const vibrateChannel = AndroidNotificationChannel(
      'careband_alarm',
      '健康报警',
      description: '家属端健康报警推送通知（振动）',
      importance: Importance.high,
      enableVibration: true,
      playSound: true,
    );
    await androidPlugin?.createNotificationChannel(vibrateChannel);

    // 渠道 2：无振动
    const quietChannel = AndroidNotificationChannel(
      'careband_alarm_quiet',
      '健康报警（静音）',
      description: '家属端健康报警推送通知（无振动）',
      importance: Importance.high,
      enableVibration: false,
      playSound: true,
    );
    await androidPlugin?.createNotificationChannel(quietChannel);

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
    debugPrint('[NotificationService] 初始化完成（双渠道）');
  }

  void _handleTap(NotificationResponse response) {
    debugPrint('[NotificationService] 通知点击: payload=${response.payload}');
    onTap?.call(response.payload);
  }

  /// 显示报警通知
  Future<void> showAlarmNotification(
    AlarmRecord alarm, {
    bool enableVibration = true,
  }) async {
    if (!_initialized) {
      debugPrint('[NotificationService] 未初始化，跳过通知');
      return;
    }

    final (title, body) = _alarmContent(alarm);

    // 根据振动开关选择不同渠道（Android 8+ 渠道设置不可变）
    final channelId =
        enableVibration ? 'careband_alarm' : 'careband_alarm_quiet';
    final channelName =
        enableVibration ? '健康报警' : '健康报警（静音）';

    final androidDetails = AndroidNotificationDetails(
      channelId,
      channelName,
      channelDescription: '家属端健康报警推送通知',
      importance: Importance.high,
      priority: Priority.high,
      enableVibration: enableVibration,
      autoCancel: true,
      category: AndroidNotificationCategory.alarm,
    );
    final details = NotificationDetails(android: androidDetails);

    await _plugin.show(
      alarm.eventId % 0x7FFFFFFF,
      title,
      body,
      details,
      payload: 'alarm_${alarm.eventId}',
    );

    debugPrint('[NotificationService] 已发送通知: $title');
  }

  /// 根据报警类型生成通知标题和正文
  (String title, String body) _alarmContent(AlarmRecord alarm) {
    switch (alarm.alarmType) {
      case FamilyAlarmType.tempHigh:
        return (
          '环境温度过高警报',
          '当前温度 ${alarm.value.toStringAsFixed(1)}°C，超过安全阈值',
        );
      case FamilyAlarmType.tempLow:
        return (
          '环境温度过低警报',
          '当前温度 ${alarm.value.toStringAsFixed(1)}°C，低于安全阈值',
        );
      case FamilyAlarmType.heartRateHigh:
        return (
          '心率过高警报',
          '当前心率 ${alarm.value.toInt()} bpm，超过安全阈值',
        );
      case FamilyAlarmType.heartRateLow:
        return (
          '心率过低警报',
          '当前心率 ${alarm.value.toInt()} bpm，低于安全阈值',
        );
      case FamilyAlarmType.spo2Low:
        return (
          '血氧过低警报',
          '当前血氧 ${alarm.value.toInt()}%，低于安全阈值',
        );
      case FamilyAlarmType.fall:
        return ('跌倒警报', '检测到佩戴者疑似跌倒，请及时确认');
      case FamilyAlarmType.manual:
        return ('手动求助警报', '佩戴者发出手动求助信号');
      case FamilyAlarmType.callFamily:
        return ('呼叫家人', '佩戴者请求联系家人');
      case FamilyAlarmType.unknown:
        return ('健康报警', '收到报警事件，请查看详情');
    }
  }
}
