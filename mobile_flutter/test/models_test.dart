import 'package:flutter_test/flutter_test.dart';
import 'package:mobile_flutter/data/models.dart';

void main() {
  group('TelemetryData validity flags', () {
    TelemetryData makeTelemetry({required int dataValid}) {
      return TelemetryData(
        temperature: 36.5,
        heartRate: 72,
        spo2: 98,
        steps: 1000,
        battery: 85,
        dataValid: dataValid,
        timestamp: DateTime.now(),
      );
    }

    test('dataValid=0x01: only temp valid', () {
      final data = makeTelemetry(dataValid: 0x01);
      expect(data.isTempValid, true);
      expect(data.isHrValid, false);
      expect(data.isSpo2Valid, false);
    });

    test('dataValid=0x02: HR and SpO2 valid (shared bit 1), temp invalid', () {
      final data = makeTelemetry(dataValid: 0x02);
      expect(data.isTempValid, false);
      expect(data.isHrValid, true);
      expect(data.isSpo2Valid, true);
    });

    test('dataValid=0x03: temp, HR, SpO2 all valid', () {
      final data = makeTelemetry(dataValid: 0x03);
      expect(data.isTempValid, true);
      expect(data.isHrValid, true);
      expect(data.isSpo2Valid, true);
    });

    test('dataValid=0x07: all valid', () {
      final data = makeTelemetry(dataValid: 0x07);
      expect(data.isTempValid, true);
      expect(data.isHrValid, true);
      expect(data.isSpo2Valid, true);
    });

    test('dataValid=0x00: nothing valid', () {
      final data = makeTelemetry(dataValid: 0x00);
      expect(data.isTempValid, false);
      expect(data.isHrValid, false);
      expect(data.isSpo2Valid, false);
    });

    test('toString contains expected fields', () {
      final data = makeTelemetry(dataValid: 0x03);
      final str = data.toString();
      expect(str, contains('36.5'));
      expect(str, contains('72'));
      expect(str, contains('98'));
      expect(str, contains('1000'));
      expect(str, contains('85'));
    });
  });

  group('AlarmType.fromCode', () {
    test('maps code 0 to none', () {
      expect(AlarmType.fromCode(0), AlarmType.none);
    });

    test('maps code 1 to tempHigh', () {
      expect(AlarmType.fromCode(1), AlarmType.tempHigh);
    });

    test('maps code 2 to tempLow', () {
      expect(AlarmType.fromCode(2), AlarmType.tempLow);
    });

    test('maps code 3 to heartRateHigh', () {
      expect(AlarmType.fromCode(3), AlarmType.heartRateHigh);
    });

    test('maps code 4 to heartRateLow', () {
      expect(AlarmType.fromCode(4), AlarmType.heartRateLow);
    });

    test('maps code 5 to spo2Low', () {
      expect(AlarmType.fromCode(5), AlarmType.spo2Low);
    });

    test('maps code 6 to fall', () {
      expect(AlarmType.fromCode(6), AlarmType.fall);
    });

    test('maps code 7 to manual', () {
      expect(AlarmType.fromCode(7), AlarmType.manual);
    });

    test('maps code 8 to reserved', () {
      expect(AlarmType.fromCode(8), AlarmType.reserved);
    });

    test('maps code 9 to callFamily', () {
      expect(AlarmType.fromCode(9), AlarmType.callFamily);
    });

    test('maps invalid code to none', () {
      expect(AlarmType.fromCode(99), AlarmType.none);
      expect(AlarmType.fromCode(-1), AlarmType.none);
      expect(AlarmType.fromCode(255), AlarmType.none);
    });
  });

  group('AlarmData', () {
    test('isAcked defaults to false', () {
      final alarm = AlarmData(
        eventId: 1,
        alarmType: AlarmType.tempHigh,
        triggerValue: 39.5,
        battery: 80,
        timestamp: DateTime.now(),
      );
      expect(alarm.isAcked, false);
    });

    test('isAcked can be set to true', () {
      final alarm = AlarmData(
        eventId: 1,
        alarmType: AlarmType.tempHigh,
        triggerValue: 39.5,
        battery: 80,
        timestamp: DateTime.now(),
        isAcked: true,
      );
      expect(alarm.isAcked, true);
    });

    test('toString contains expected fields', () {
      final alarm = AlarmData(
        eventId: 42,
        alarmType: AlarmType.fall,
        triggerValue: 0.0,
        battery: 60,
        timestamp: DateTime.now(),
      );
      final str = alarm.toString();
      expect(str, contains('42'));
      expect(str, contains('fall'));
    });
  });

  group('BleConnectionState', () {
    test('has all expected values', () {
      expect(BleConnectionState.values, containsAll([
        BleConnectionState.disconnected,
        BleConnectionState.scanning,
        BleConnectionState.connecting,
        BleConnectionState.connected,
        BleConnectionState.disconnecting,
      ]));
    });

    test('has exactly 5 values', () {
      expect(BleConnectionState.values.length, 5);
    });
  });

  group('DeviceStatus', () {
    test('stores values correctly', () {
      final status = DeviceStatus(
        deviceState: 2,
        bleConnCount: 1,
        alarmState: 0,
      );
      expect(status.deviceState, 2);
      expect(status.bleConnCount, 1);
      expect(status.alarmState, 0);
    });

    test('toString contains expected fields', () {
      final status = DeviceStatus(
        deviceState: 1,
        bleConnCount: 3,
        alarmState: 1,
      );
      final str = status.toString();
      expect(str, contains('1'));
      expect(str, contains('3'));
    });
  });
}
