import 'dart:typed_data';
import '../data/models.dart';

class BleParser {
  /// Parse Telemetry Notify data (20 bytes)
  /// Offset 0:  int16  temp -> getInt16(0, Endian.little) / 10.0
  /// Offset 2:  uint8  heart_rate
  /// Offset 3:  uint8  spo2
  /// Offset 4:  uint32 steps -> getUint32(4, Endian.little)
  /// Offset 8:  uint8  reserved (原 battery，已移除)
  /// Offset 9:  uint8  data_valid
  /// Offset 10: uint32 timestamp -> getUint32(10, Endian.little)
  static TelemetryData? parseTelemetry(List<int> raw) {
    if (raw.length < 14) return null;
    final bd = ByteData.sublistView(Uint8List.fromList(raw));
    return TelemetryData(
      temperature: bd.getInt16(0, Endian.little) / 10.0,
      heartRate: raw[2],
      spo2: raw[3],
      steps: bd.getUint32(4, Endian.little),
      dataValid: raw[9],
      timestamp: DateTime.fromMillisecondsSinceEpoch(
          bd.getUint32(10, Endian.little) * 1000),
    );
  }

  /// Parse Alarm Notify data (16 bytes)
  /// Offset 0: uint32 event_id
  /// Offset 4: uint8  alarm_type
  /// Offset 5: int16  value / 10.0
  /// Offset 7: uint8  reserved (原 battery，已移除)
  /// Offset 8: uint32 timestamp
  static AlarmData? parseAlarm(List<int> raw) {
    if (raw.length < 12) return null;
    final bd = ByteData.sublistView(Uint8List.fromList(raw));
    return AlarmData(
      eventId: bd.getUint32(0, Endian.little),
      alarmType: AlarmType.fromCode(raw[4]),
      triggerValue: bd.getInt16(5, Endian.little) / 10.0,
      timestamp: DateTime.fromMillisecondsSinceEpoch(
          bd.getUint32(8, Endian.little) * 1000),
    );
  }

  /// Parse Status Read data (3 bytes)
  static DeviceStatus? parseStatus(List<int> raw) {
    if (raw.length < 3) return null;
    return DeviceStatus(
      deviceState: raw[0],
      bleConnCount: raw[1],
      alarmState: raw[2],
    );
  }
}
