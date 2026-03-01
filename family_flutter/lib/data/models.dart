/// 家属端数据模型
///
/// 包含所有数据结构、枚举、阈值常量和 UI 扩展
library;

import 'package:flutter/material.dart';
import '../theme/app_colors.dart';

// ==================== 报警类型枚举 ====================

/// 家属 App 报警类型枚举（与 MQTT alarm_type 字段对齐）
enum FamilyAlarmType {
  tempHigh(1, 'temp_high'),
  tempLow(2, 'temp_low'),
  heartRateHigh(3, 'heart_rate_high'),
  heartRateLow(4, 'heart_rate_low'),
  spo2Low(5, 'spo2_low'),
  fall(6, 'fall'),
  manual(7, 'manual'),
  callFamily(9, 'call_family'),
  unknown(0, 'unknown');

  final int code;
  final String mqttString;
  const FamilyAlarmType(this.code, this.mqttString);

  /// 从 MQTT JSON 中的 alarm_type 字符串解析
  static FamilyAlarmType fromMqttString(String? s) {
    if (s == null) return FamilyAlarmType.unknown;
    return FamilyAlarmType.values.firstWhere(
      (e) => e.mqttString == s,
      orElse: () => FamilyAlarmType.unknown,
    );
  }

  /// 从数据库存储的 code 解析
  static FamilyAlarmType fromCode(int code) =>
      FamilyAlarmType.values.firstWhere(
        (e) => e.code == code,
        orElse: () => FamilyAlarmType.unknown,
      );
}

// ==================== 遥测记录 ====================

/// 遥测数据记录（对应 SQLite telemetry 表）
class TelemetryRecord {
  final int? id;
  final String deviceId;
  final double temp;
  final int heartRate;
  final int spo2;
  final int steps;
  final DateTime timestamp;
  final DateTime receivedAt;

  const TelemetryRecord({
    this.id,
    required this.deviceId,
    required this.temp,
    required this.heartRate,
    required this.spo2,
    required this.steps,
    required this.timestamp,
    required this.receivedAt,
  });

  /// 从 MQTT JSON 构建
  factory TelemetryRecord.fromMqttJson(Map<String, dynamic> json) {
    return TelemetryRecord(
      deviceId: json['device_id'] as String? ?? '',
      temp: (json['temp'] as num?)?.toDouble() ?? 0.0,
      heartRate: (json['heart_rate'] as num?)?.toInt() ?? 0,
      spo2: (json['spo2'] as num?)?.toInt() ?? 0,
      steps: (json['steps'] as num?)?.toInt() ?? 0,
      timestamp: json['timestamp'] != null
          ? DateTime.fromMillisecondsSinceEpoch(
              (json['timestamp'] as num).toInt() * 1000,
            )
          : DateTime.now(),
      receivedAt: DateTime.now(),
    );
  }

  /// 从 SQLite 行构建
  factory TelemetryRecord.fromDbRow(Map<String, Object?> row) {
    return TelemetryRecord(
      id: row['id'] as int?,
      deviceId: row['device_id'] as String,
      temp: (row['temp'] as num).toDouble(),
      heartRate: row['heart_rate'] as int,
      spo2: row['spo2'] as int,
      steps: row['steps'] as int,
      timestamp: DateTime.fromMillisecondsSinceEpoch(
        (row['timestamp'] as int) * 1000,
      ),
      receivedAt: DateTime.fromMillisecondsSinceEpoch(
        row['received_at'] as int,
      ),
    );
  }

  /// 转为 SQLite 插入 Map
  Map<String, Object?> toDbMap() {
    return {
      'device_id': deviceId,
      'temp': temp,
      'heart_rate': heartRate,
      'spo2': spo2,
      'steps': steps,
      'timestamp': timestamp.millisecondsSinceEpoch ~/ 1000,
      'received_at': receivedAt.millisecondsSinceEpoch,
    };
  }

  @override
  String toString() =>
      'TelemetryRecord(device=$deviceId, temp=$temp, '
      'hr=$heartRate, spo2=$spo2, steps=$steps)';
}

// ==================== 报警记录 ====================

/// 报警记录（对应 SQLite alarm 表）
class AlarmRecord {
  final int? id;
  final String deviceId;
  final int eventId;
  final FamilyAlarmType alarmType;
  final double value;
  final DateTime timestamp;
  final DateTime receivedAt;
  bool acknowledged;
  DateTime? ackedAt;

  AlarmRecord({
    this.id,
    required this.deviceId,
    required this.eventId,
    required this.alarmType,
    required this.value,
    required this.timestamp,
    required this.receivedAt,
    this.acknowledged = false,
    this.ackedAt,
  });

  /// 从 MQTT JSON 构建
  factory AlarmRecord.fromMqttJson(Map<String, dynamic> json) {
    return AlarmRecord(
      deviceId: json['device_id'] as String? ?? '',
      eventId: (json['event_id'] as num?)?.toInt() ?? 0,
      alarmType: FamilyAlarmType.fromMqttString(
        json['alarm_type'] as String?,
      ),
      value: (json['value'] as num?)?.toDouble() ?? 0.0,
      timestamp: json['timestamp'] != null
          ? DateTime.fromMillisecondsSinceEpoch(
              (json['timestamp'] as num).toInt() * 1000,
            )
          : DateTime.now(),
      receivedAt: DateTime.now(),
    );
  }

  /// 从 SQLite 行构建
  factory AlarmRecord.fromDbRow(Map<String, Object?> row) {
    return AlarmRecord(
      id: row['id'] as int?,
      deviceId: row['device_id'] as String,
      eventId: row['event_id'] as int,
      alarmType: FamilyAlarmType.fromCode(row['alarm_type'] as int),
      value: (row['value'] as num).toDouble(),
      timestamp: DateTime.fromMillisecondsSinceEpoch(
        (row['timestamp'] as int) * 1000,
      ),
      receivedAt: DateTime.fromMillisecondsSinceEpoch(
        row['received_at'] as int,
      ),
      acknowledged: (row['acknowledged'] as int) == 1,
      ackedAt: row['acked_at'] != null
          ? DateTime.fromMillisecondsSinceEpoch(row['acked_at'] as int)
          : null,
    );
  }

  /// 转为 SQLite 插入 Map
  Map<String, Object?> toDbMap() {
    return {
      'device_id': deviceId,
      'event_id': eventId,
      'alarm_type': alarmType.code,
      'value': value,
      'timestamp': timestamp.millisecondsSinceEpoch ~/ 1000,
      'received_at': receivedAt.millisecondsSinceEpoch,
      'acknowledged': acknowledged ? 1 : 0,
      'acked_at': ackedAt?.millisecondsSinceEpoch,
    };
  }

  @override
  String toString() =>
      'AlarmRecord(device=$deviceId, id=$eventId, '
      'type=$alarmType, value=$value, acked=$acknowledged)';
}

// ==================== 设备状态记录 ====================

/// 设备在线状态
class DeviceStatusRecord {
  final String deviceId;
  final bool online;
  final DateTime lastSeen;

  const DeviceStatusRecord({
    required this.deviceId,
    required this.online,
    required this.lastSeen,
  });

  /// 从 MQTT JSON 构建（status / lwt 共用）
  factory DeviceStatusRecord.fromMqttJson(Map<String, dynamic> json) {
    return DeviceStatusRecord(
      deviceId: json['device_id'] as String? ?? '',
      online: json['online'] as bool? ?? false,
      lastSeen: DateTime.now(),
    );
  }

  @override
  String toString() =>
      'DeviceStatusRecord(device=$deviceId, online=$online)';
}

// ==================== 每日摘要 ====================

/// 每日健康摘要（由 FamilyRepository 聚合查询生成）
class DailySummary {
  final DateTime date;
  final double avgTemp;
  final double minTemp;
  final double maxTemp;
  final int avgHeartRate;
  final int minHeartRate;
  final int maxHeartRate;
  final int avgSpo2;
  final int minSpo2;
  final int maxSteps;
  final int alarmCount;

  const DailySummary({
    required this.date,
    required this.avgTemp,
    required this.minTemp,
    required this.maxTemp,
    required this.avgHeartRate,
    required this.minHeartRate,
    required this.maxHeartRate,
    required this.avgSpo2,
    required this.minSpo2,
    required this.maxSteps,
    required this.alarmCount,
  });

  @override
  String toString() =>
      'DailySummary(date=$date, avgHr=$avgHeartRate, '
      'avgSpo2=$avgSpo2, alarms=$alarmCount)';
}

// ==================== 健康阈值常量 ====================

/// 健康阈值常量（与固件端 + 网关 App 保持一致）
class HealthThresholds {
  HealthThresholds._();

  static const double tempHigh = 35.0;
  static const double tempLow = 20.0;
  static const int heartRateHigh = 120;
  static const int heartRateLow = 45;
  static const int spo2Low = 90;
}

// ==================== UI 扩展 ====================

/// AlarmType UI 扩展（显示名称、图标、颜色等）
extension FamilyAlarmTypeUI on FamilyAlarmType {
  /// 报警类型的中文显示名
  String get displayName {
    switch (this) {
      case FamilyAlarmType.tempHigh:
        return '环境温度过高';
      case FamilyAlarmType.tempLow:
        return '环境温度过低';
      case FamilyAlarmType.heartRateHigh:
        return '心率过高';
      case FamilyAlarmType.heartRateLow:
        return '心率过低';
      case FamilyAlarmType.spo2Low:
        return '血氧过低';
      case FamilyAlarmType.fall:
        return '疑似跌倒';
      case FamilyAlarmType.manual:
        return '手动报警';
      case FamilyAlarmType.callFamily:
        return '呼叫家人';
      case FamilyAlarmType.unknown:
        return '未知报警';
    }
  }

  /// 报警对应的图标
  IconData get icon {
    switch (this) {
      case FamilyAlarmType.tempHigh:
      case FamilyAlarmType.tempLow:
        return Icons.thermostat;
      case FamilyAlarmType.heartRateHigh:
      case FamilyAlarmType.heartRateLow:
        return Icons.favorite;
      case FamilyAlarmType.spo2Low:
        return Icons.water_drop;
      case FamilyAlarmType.fall:
        return Icons.accessibility_new;
      case FamilyAlarmType.manual:
        return Icons.warning_amber_rounded;
      case FamilyAlarmType.callFamily:
        return Icons.phone_in_talk;
      case FamilyAlarmType.unknown:
        return Icons.info_outline;
    }
  }

  /// 报警对应的颜色
  Color get color {
    switch (this) {
      case FamilyAlarmType.tempHigh:
      case FamilyAlarmType.tempLow:
        return AppColors.alarmTemp;
      case FamilyAlarmType.heartRateHigh:
      case FamilyAlarmType.heartRateLow:
        return AppColors.alarmHeartRate;
      case FamilyAlarmType.spo2Low:
        return AppColors.alarmSpo2;
      case FamilyAlarmType.fall:
        return AppColors.alarmFall;
      case FamilyAlarmType.manual:
        return AppColors.alarmManual;
      case FamilyAlarmType.callFamily:
        return AppColors.alarmCallFamily;
      case FamilyAlarmType.unknown:
        return AppColors.alarmUnknown;
    }
  }

  /// 报警触发值的格式化字符串
  String formatValue(double v) {
    switch (this) {
      case FamilyAlarmType.tempHigh:
      case FamilyAlarmType.tempLow:
        return '${v.toStringAsFixed(1)} C';
      case FamilyAlarmType.heartRateHigh:
      case FamilyAlarmType.heartRateLow:
        return '${v.toInt()} bpm';
      case FamilyAlarmType.spo2Low:
        return '${v.toInt()}%';
      case FamilyAlarmType.fall:
      case FamilyAlarmType.manual:
      case FamilyAlarmType.callFamily:
      case FamilyAlarmType.unknown:
        return '--';
    }
  }

  /// 报警严重级别: 0=info, 1=warning, 2=critical
  int get severity {
    switch (this) {
      case FamilyAlarmType.unknown:
        return 0;
      case FamilyAlarmType.tempHigh:
      case FamilyAlarmType.tempLow:
      case FamilyAlarmType.callFamily:
        return 1;
      case FamilyAlarmType.heartRateHigh:
      case FamilyAlarmType.heartRateLow:
      case FamilyAlarmType.spo2Low:
      case FamilyAlarmType.fall:
      case FamilyAlarmType.manual:
        return 2;
    }
  }
}
