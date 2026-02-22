import 'dart:typed_data';
import 'package:flutter_test/flutter_test.dart';
import 'package:mobile_flutter/ble/ble_parser.dart';
import 'package:mobile_flutter/data/models.dart';

void main() {
  group('BleParser.parseTelemetry', () {
    test('parses valid 14-byte telemetry data correctly', () {
      // Build a 14-byte telemetry packet:
      // Offset 0: int16 LE temp = 365 => 36.5 C
      // Offset 2: uint8 heart_rate = 72
      // Offset 3: uint8 spo2 = 98
      // Offset 4: uint32 LE steps = 12345
      // Offset 8: uint8 battery = 85
      // Offset 9: uint8 data_valid = 0x03
      // Offset 10: uint32 LE timestamp = 1700000000
      final bd = ByteData(14);
      bd.setInt16(0, 365, Endian.little);
      bd.setUint8(2, 72);
      bd.setUint8(3, 98);
      bd.setUint32(4, 12345, Endian.little);
      bd.setUint8(8, 85);
      bd.setUint8(9, 0x03);
      bd.setUint32(10, 1700000000, Endian.little);

      final raw = bd.buffer.asUint8List().toList();
      final result = BleParser.parseTelemetry(raw);

      expect(result, isNotNull);
      expect(result!.temperature, closeTo(36.5, 0.01));
      expect(result.heartRate, 72);
      expect(result.spo2, 98);
      expect(result.steps, 12345);
      expect(result.battery, 85);
      expect(result.dataValid, 0x03);
      expect(
        result.timestamp,
        DateTime.fromMillisecondsSinceEpoch(1700000000 * 1000),
      );
    });

    test('parses negative temperature correctly', () {
      final bd = ByteData(14);
      bd.setInt16(0, -50, Endian.little); // -5.0 C
      bd.setUint8(2, 60);
      bd.setUint8(3, 95);
      bd.setUint32(4, 0, Endian.little);
      bd.setUint8(8, 50);
      bd.setUint8(9, 0x01);
      bd.setUint32(10, 1700000000, Endian.little);

      final raw = bd.buffer.asUint8List().toList();
      final result = BleParser.parseTelemetry(raw);

      expect(result, isNotNull);
      expect(result!.temperature, closeTo(-5.0, 0.01));
    });

    test('parses 20-byte packet (extra bytes ignored)', () {
      final bd = ByteData(20);
      bd.setInt16(0, 370, Endian.little);
      bd.setUint8(2, 80);
      bd.setUint8(3, 99);
      bd.setUint32(4, 5000, Endian.little);
      bd.setUint8(8, 100);
      bd.setUint8(9, 0x07);
      bd.setUint32(10, 1700000000, Endian.little);

      final raw = bd.buffer.asUint8List().toList();
      final result = BleParser.parseTelemetry(raw);

      expect(result, isNotNull);
      expect(result!.temperature, closeTo(37.0, 0.01));
      expect(result.heartRate, 80);
      expect(result.spo2, 99);
      expect(result.steps, 5000);
      expect(result.battery, 100);
      expect(result.dataValid, 0x07);
    });

    test('returns null for empty data', () {
      expect(BleParser.parseTelemetry([]), isNull);
    });

    test('returns null for data shorter than 14 bytes', () {
      expect(BleParser.parseTelemetry([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13]), isNull);
    });

    test('verifies little-endian byte order for steps', () {
      // steps = 0x04030201 = 67305985 in little-endian: [01, 02, 03, 04]
      final bd = ByteData(14);
      bd.setInt16(0, 365, Endian.little);
      bd.setUint8(2, 72);
      bd.setUint8(3, 98);
      bd.setUint32(4, 67305985, Endian.little);
      bd.setUint8(8, 85);
      bd.setUint8(9, 0x03);
      bd.setUint32(10, 1700000000, Endian.little);

      final raw = bd.buffer.asUint8List().toList();
      // Verify the raw bytes at offset 4-7 are [01, 02, 03, 04]
      expect(raw[4], 0x01);
      expect(raw[5], 0x02);
      expect(raw[6], 0x03);
      expect(raw[7], 0x04);

      final result = BleParser.parseTelemetry(raw);
      expect(result, isNotNull);
      expect(result!.steps, 67305985);
    });
  });

  group('BleParser.parseAlarm', () {
    test('parses valid 12-byte alarm data correctly', () {
      // Offset 0: uint32 LE event_id = 42
      // Offset 4: uint8 alarm_type = 1 (tempHigh)
      // Offset 5: int16 LE value = 395 => 39.5
      // Offset 7: uint8 battery = 90
      // Offset 8: uint32 LE timestamp = 1700000000
      final bd = ByteData(12);
      bd.setUint32(0, 42, Endian.little);
      bd.setUint8(4, 1);
      bd.setInt16(5, 395, Endian.little);
      bd.setUint8(7, 90);
      bd.setUint32(8, 1700000000, Endian.little);

      final raw = bd.buffer.asUint8List().toList();
      final result = BleParser.parseAlarm(raw);

      expect(result, isNotNull);
      expect(result!.eventId, 42);
      expect(result.alarmType, AlarmType.tempHigh);
      expect(result.triggerValue, closeTo(39.5, 0.01));
      expect(result.battery, 90);
      expect(
        result.timestamp,
        DateTime.fromMillisecondsSinceEpoch(1700000000 * 1000),
      );
      expect(result.isAcked, false);
    });

    test('parses spo2Low alarm type', () {
      final bd = ByteData(12);
      bd.setUint32(0, 100, Endian.little);
      bd.setUint8(4, 5); // spo2Low
      bd.setInt16(5, 880, Endian.little); // 88.0
      bd.setUint8(7, 75);
      bd.setUint32(8, 1700000000, Endian.little);

      final raw = bd.buffer.asUint8List().toList();
      final result = BleParser.parseAlarm(raw);

      expect(result, isNotNull);
      expect(result!.alarmType, AlarmType.spo2Low);
      expect(result.triggerValue, closeTo(88.0, 0.01));
    });

    test('parses fall alarm type', () {
      final bd = ByteData(12);
      bd.setUint32(0, 200, Endian.little);
      bd.setUint8(4, 6); // fall
      bd.setInt16(5, 0, Endian.little);
      bd.setUint8(7, 60);
      bd.setUint32(8, 1700000000, Endian.little);

      final raw = bd.buffer.asUint8List().toList();
      final result = BleParser.parseAlarm(raw);

      expect(result, isNotNull);
      expect(result!.alarmType, AlarmType.fall);
    });

    test('unknown alarm code maps to none', () {
      final bd = ByteData(12);
      bd.setUint32(0, 1, Endian.little);
      bd.setUint8(4, 99); // unknown
      bd.setInt16(5, 0, Endian.little);
      bd.setUint8(7, 50);
      bd.setUint32(8, 1700000000, Endian.little);

      final raw = bd.buffer.asUint8List().toList();
      final result = BleParser.parseAlarm(raw);

      expect(result, isNotNull);
      expect(result!.alarmType, AlarmType.none);
    });

    test('returns null for empty data', () {
      expect(BleParser.parseAlarm([]), isNull);
    });

    test('returns null for data shorter than 12 bytes', () {
      expect(BleParser.parseAlarm([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11]), isNull);
    });

    test('parses 16-byte packet (extra bytes ignored)', () {
      final bd = ByteData(16);
      bd.setUint32(0, 42, Endian.little);
      bd.setUint8(4, 3); // heartRateHigh
      bd.setInt16(5, 1200, Endian.little); // 120.0
      bd.setUint8(7, 80);
      bd.setUint32(8, 1700000000, Endian.little);

      final raw = bd.buffer.asUint8List().toList();
      final result = BleParser.parseAlarm(raw);

      expect(result, isNotNull);
      expect(result!.alarmType, AlarmType.heartRateHigh);
      expect(result.triggerValue, closeTo(120.0, 0.01));
    });
  });

  group('BleParser.parseStatus', () {
    test('parses valid 3-byte status data', () {
      final result = BleParser.parseStatus([2, 1, 0]);

      expect(result, isNotNull);
      expect(result!.deviceState, 2);
      expect(result.bleConnCount, 1);
      expect(result.alarmState, 0);
    });

    test('parses status with alarm active', () {
      final result = BleParser.parseStatus([1, 3, 1]);

      expect(result, isNotNull);
      expect(result!.deviceState, 1);
      expect(result.bleConnCount, 3);
      expect(result.alarmState, 1);
    });

    test('parses longer data (extra bytes ignored)', () {
      final result = BleParser.parseStatus([5, 2, 1, 99, 88]);

      expect(result, isNotNull);
      expect(result!.deviceState, 5);
      expect(result.bleConnCount, 2);
      expect(result.alarmState, 1);
    });

    test('returns null for empty data', () {
      expect(BleParser.parseStatus([]), isNull);
    });

    test('returns null for data shorter than 3 bytes', () {
      expect(BleParser.parseStatus([1, 2]), isNull);
    });
  });
}
