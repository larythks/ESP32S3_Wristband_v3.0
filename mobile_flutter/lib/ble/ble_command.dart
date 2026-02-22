import 'dart:typed_data';

class BleCommand {
  static int _nonce = 0;

  static int get _nextNonce => ++_nonce;

  /// Reset nonce counter (for testing)
  static void resetNonce() => _nonce = 0;

  /// ACK alarm -- 9 bytes: [0x01] + event_id(4, LE) + nonce(4, LE)
  static Uint8List ackAlarm(int eventId) {
    final bd = ByteData(9);
    bd.setUint8(0, 0x01);
    bd.setUint32(1, eventId, Endian.little);
    bd.setUint32(5, _nextNonce, Endian.little);
    return bd.buffer.asUint8List();
  }

  /// Sync time -- 9 bytes: [0x02] + unix_ts(4, LE) + nonce(4, LE)
  static Uint8List syncTime() {
    final bd = ByteData(9);
    bd.setUint8(0, 0x02);
    bd.setUint32(
        1, DateTime.now().millisecondsSinceEpoch ~/ 1000, Endian.little);
    bd.setUint32(5, _nextNonce, Endian.little);
    return bd.buffer.asUint8List();
  }

  /// Request immediate report -- 5 bytes: [0x03] + nonce(4, LE)
  static Uint8List requestReport() {
    final bd = ByteData(5);
    bd.setUint8(0, 0x03);
    bd.setUint32(1, _nextNonce, Endian.little);
    return bd.buffer.asUint8List();
  }

  /// Manual measure control -- 7 bytes: [0x04] + mode(1) + duration(1) + nonce(4, LE)
  static Uint8List manualMeasure({required bool start, int durationSec = 15}) {
    final bd = ByteData(7);
    bd.setUint8(0, 0x04);
    bd.setUint8(1, start ? 1 : 0);
    bd.setUint8(2, durationSec);
    bd.setUint32(3, _nextNonce, Endian.little);
    return bd.buffer.asUint8List();
  }
}
