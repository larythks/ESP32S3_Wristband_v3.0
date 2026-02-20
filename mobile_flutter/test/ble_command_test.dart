import 'dart:typed_data';
import 'package:flutter_test/flutter_test.dart';
import 'package:mobile_flutter/ble/ble_command.dart';

void main() {
  setUp(() {
    BleCommand.resetNonce();
  });

  group('BleCommand.ackAlarm', () {
    test('has correct length of 9 bytes', () {
      final data = BleCommand.ackAlarm(42);
      expect(data.length, 9);
    });

    test('has command type 0x01 at offset 0', () {
      final data = BleCommand.ackAlarm(42);
      expect(data[0], 0x01);
    });

    test('has event_id in little-endian at offset 1-4', () {
      final data = BleCommand.ackAlarm(0x04030201);
      final bd = ByteData.sublistView(data);
      expect(bd.getUint32(1, Endian.little), 0x04030201);
      // Verify raw bytes are little-endian
      expect(data[1], 0x01);
      expect(data[2], 0x02);
      expect(data[3], 0x03);
      expect(data[4], 0x04);
    });

    test('has nonce at offset 5-8', () {
      final data = BleCommand.ackAlarm(1);
      final bd = ByteData.sublistView(data);
      expect(bd.getUint32(5, Endian.little), 1); // first nonce = 1
    });
  });

  group('BleCommand.syncTime', () {
    test('has correct length of 9 bytes', () {
      final data = BleCommand.syncTime();
      expect(data.length, 9);
    });

    test('has command type 0x02 at offset 0', () {
      final data = BleCommand.syncTime();
      expect(data[0], 0x02);
    });

    test('has reasonable timestamp at offset 1-4', () {
      final before = DateTime.now().millisecondsSinceEpoch ~/ 1000;
      final data = BleCommand.syncTime();
      final after = DateTime.now().millisecondsSinceEpoch ~/ 1000;

      final bd = ByteData.sublistView(data);
      final ts = bd.getUint32(1, Endian.little);

      expect(ts, greaterThanOrEqualTo(before));
      expect(ts, lessThanOrEqualTo(after));
    });

    test('has nonce at offset 5-8', () {
      final data = BleCommand.syncTime();
      final bd = ByteData.sublistView(data);
      expect(bd.getUint32(5, Endian.little), 1); // first nonce = 1
    });
  });

  group('BleCommand.requestReport', () {
    test('has correct length of 5 bytes', () {
      final data = BleCommand.requestReport();
      expect(data.length, 5);
    });

    test('has command type 0x03 at offset 0', () {
      final data = BleCommand.requestReport();
      expect(data[0], 0x03);
    });

    test('has nonce at offset 1-4', () {
      final data = BleCommand.requestReport();
      final bd = ByteData.sublistView(data);
      expect(bd.getUint32(1, Endian.little), 1); // first nonce = 1
    });
  });

  group('BleCommand.manualMeasure', () {
    test('has correct length of 7 bytes', () {
      final data = BleCommand.manualMeasure(start: true);
      expect(data.length, 7);
    });

    test('has command type 0x04 at offset 0', () {
      final data = BleCommand.manualMeasure(start: true);
      expect(data[0], 0x04);
    });

    test('has mode=1 when start=true', () {
      final data = BleCommand.manualMeasure(start: true);
      expect(data[1], 1);
    });

    test('has mode=0 when start=false', () {
      final data = BleCommand.manualMeasure(start: false);
      expect(data[1], 0);
    });

    test('has default duration of 15 at offset 2', () {
      final data = BleCommand.manualMeasure(start: true);
      expect(data[2], 15);
    });

    test('has custom duration at offset 2', () {
      final data = BleCommand.manualMeasure(start: true, durationSec: 30);
      expect(data[2], 30);
    });

    test('has nonce at offset 3-6', () {
      final data = BleCommand.manualMeasure(start: true);
      final bd = ByteData.sublistView(data);
      expect(bd.getUint32(3, Endian.little), 1); // first nonce = 1
    });
  });

  group('Nonce increment', () {
    test('nonce increments across consecutive commands', () {
      final data1 = BleCommand.ackAlarm(1);
      final data2 = BleCommand.requestReport();
      final data3 = BleCommand.manualMeasure(start: true);

      final bd1 = ByteData.sublistView(data1);
      final bd2 = ByteData.sublistView(data2);
      final bd3 = ByteData.sublistView(data3);

      final nonce1 = bd1.getUint32(5, Endian.little);
      final nonce2 = bd2.getUint32(1, Endian.little);
      final nonce3 = bd3.getUint32(3, Endian.little);

      expect(nonce1, 1);
      expect(nonce2, 2);
      expect(nonce3, 3);
    });

    test('resetNonce resets the counter', () {
      BleCommand.ackAlarm(1); // nonce = 1
      BleCommand.ackAlarm(1); // nonce = 2

      BleCommand.resetNonce();

      final data = BleCommand.ackAlarm(1);
      final bd = ByteData.sublistView(data);
      expect(bd.getUint32(5, Endian.little), 1); // back to 1
    });
  });
}
