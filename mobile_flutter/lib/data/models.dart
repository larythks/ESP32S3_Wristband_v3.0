import 'package:flutter/material.dart';

/// BLE 连接状态枚举
enum BleConnectionState {
  disconnected,
  scanning,
  connecting,
  connected,
  disconnecting,
}

/// 报警类型枚举（与固件 ble_alarm_type_t 对齐）
enum AlarmType {
  none(0),
  tempHigh(1),
  tempLow(2),
  heartRateHigh(3),
  heartRateLow(4),
  spo2Low(5),
  fall(6),
  manual(7),
  reserved(8),
  callFamily(9);

  final int code;
  const AlarmType(this.code);

  static AlarmType fromCode(int code) =>
      AlarmType.values.firstWhere((e) => e.code == code, orElse: () => AlarmType.none);
}

/// Telemetry 遥测数据（对应 BLE Telemetry 特征 FF01, 20 bytes）
class TelemetryData {
  final double temperature;
  final int heartRate;
  final int spo2;
  final int steps;
  final int battery;
  final int dataValid;
  final DateTime timestamp;

  const TelemetryData({
    required this.temperature,
    required this.heartRate,
    required this.spo2,
    required this.steps,
    required this.battery,
    required this.dataValid,
    required this.timestamp,
  });

  /// 温度数据有效 (bit 0 = SENSOR_TEMP)
  bool get isTempValid => (dataValid & 0x01) != 0;

  /// 心率数据有效 (bit 1 = SENSOR_HR_SPO2, HR和SpO2共用)
  bool get isHrValid => (dataValid & 0x02) != 0;

  /// 血氧数据有效 (bit 1 = SENSOR_HR_SPO2, HR和SpO2共用)
  bool get isSpo2Valid => (dataValid & 0x02) != 0;

  @override
  String toString() =>
      'TelemetryData(temp=$temperature°C, hr=$heartRate, spo2=$spo2%, '
      'steps=$steps, battery=$battery%, valid=0x${dataValid.toRadixString(16)})';
}

/// Alarm 报警事件（对应 BLE Alarm 特征 FF02, 16 bytes）
class AlarmData {
  final int eventId;
  final AlarmType alarmType;
  final double triggerValue;
  final int battery;
  final DateTime timestamp;
  bool isAcked;

  AlarmData({
    required this.eventId,
    required this.alarmType,
    required this.triggerValue,
    required this.battery,
    required this.timestamp,
    this.isAcked = false,
  });

  @override
  String toString() =>
      'AlarmData(id=$eventId, type=$alarmType, value=$triggerValue, acked=$isAcked)';
}

/// AlarmType 扩展方法：显示名称、图标、颜色、格式化值、严重级别
extension AlarmTypeExtension on AlarmType {
  /// 报警类型的中文显示名
  String get displayName {
    switch (this) {
      case AlarmType.none:
        return '无';
      case AlarmType.tempHigh:
        return '环境温度过高';
      case AlarmType.tempLow:
        return '环境温度过低';
      case AlarmType.heartRateHigh:
        return '心率过高';
      case AlarmType.heartRateLow:
        return '心率过低';
      case AlarmType.spo2Low:
        return '血氧过低';
      case AlarmType.fall:
        return '疑似跌倒';
      case AlarmType.manual:
        return '手动报警';
      case AlarmType.reserved:
        return '保留';
      case AlarmType.callFamily:
        return '呼叫家人';
    }
  }

  /// 报警对应的图标
  IconData get icon {
    switch (this) {
      case AlarmType.tempHigh:
      case AlarmType.tempLow:
        return Icons.thermostat;
      case AlarmType.heartRateHigh:
      case AlarmType.heartRateLow:
        return Icons.favorite;
      case AlarmType.spo2Low:
        return Icons.water_drop;
      case AlarmType.fall:
        return Icons.accessibility_new;
      case AlarmType.manual:
        return Icons.warning_amber_rounded;
      case AlarmType.callFamily:
        return Icons.phone_in_talk;
      case AlarmType.none:
      case AlarmType.reserved:
        return Icons.info_outline;
    }
  }

  /// 报警对应的颜色
  Color get color {
    switch (this) {
      case AlarmType.tempHigh:
      case AlarmType.tempLow:
        return const Color(0xFFF4A261);
      case AlarmType.heartRateHigh:
      case AlarmType.heartRateLow:
        return const Color(0xFFE63946);
      case AlarmType.spo2Low:
        return const Color(0xFF457B9D);
      case AlarmType.fall:
      case AlarmType.manual:
        return const Color(0xFFE63946);
      case AlarmType.callFamily:
        return const Color(0xFF2D7DD2);
      case AlarmType.none:
      case AlarmType.reserved:
        return const Color(0xFF6C757D);
    }
  }

  /// 报警触发值的格式化字符串
  String formatValue(double value) {
    switch (this) {
      case AlarmType.tempHigh:
      case AlarmType.tempLow:
        return '${value.toStringAsFixed(1)}°C';
      case AlarmType.heartRateHigh:
      case AlarmType.heartRateLow:
        return '${value.toInt()} bpm';
      case AlarmType.spo2Low:
        return '${value.toInt()}%';
      case AlarmType.fall:
      case AlarmType.manual:
      case AlarmType.callFamily:
      case AlarmType.none:
      case AlarmType.reserved:
        return '--';
    }
  }

  /// 报警严重级别: 0=info, 1=warning, 2=critical
  int get severity {
    switch (this) {
      case AlarmType.none:
      case AlarmType.reserved:
        return 0;
      case AlarmType.tempHigh:
      case AlarmType.tempLow:
      case AlarmType.callFamily:
        return 1;
      case AlarmType.heartRateHigh:
      case AlarmType.heartRateLow:
      case AlarmType.spo2Low:
      case AlarmType.fall:
      case AlarmType.manual:
        return 2;
    }
  }
}

/// 设备状态（对应 BLE Status 特征 FF04, 3 bytes）
class DeviceStatus {
  final int deviceState;
  final int bleConnCount;
  final int alarmState;

  const DeviceStatus({
    required this.deviceState,
    required this.bleConnCount,
    required this.alarmState,
  });

  @override
  String toString() =>
      'DeviceStatus(state=$deviceState, conn=$bleConnCount, alarm=$alarmState)';
}
