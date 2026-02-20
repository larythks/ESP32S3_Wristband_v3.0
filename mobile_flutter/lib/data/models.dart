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
