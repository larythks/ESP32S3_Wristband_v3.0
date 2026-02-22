import 'dart:async';
import 'dart:io' show Platform;
import 'dart:typed_data';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import '../data/models.dart';
import 'ble_parser.dart';
import 'ble_command.dart';

class BleManager {
  static final BleManager instance = BleManager._();
  BleManager._();

  // UUIDs
  static final Guid _serviceUuid = Guid('0000ff00-0000-1000-8000-00805f9b34fb');
  static final Guid _telemetryUuid = Guid(
    '0000ff01-0000-1000-8000-00805f9b34fb',
  );
  static final Guid _alarmUuid = Guid('0000ff02-0000-1000-8000-00805f9b34fb');
  static final Guid _commandUuid = Guid('0000ff03-0000-1000-8000-00805f9b34fb');
  static final Guid _statusUuid = Guid('0000ff04-0000-1000-8000-00805f9b34fb');

  // Internal state
  BluetoothDevice? _device;
  BluetoothCharacteristic? _commandChar;
  BluetoothCharacteristic? _statusChar;
  BluetoothCharacteristic? _telemetryChar;
  BluetoothCharacteristic? _alarmChar;
  StreamSubscription? _connectionSub;
  StreamSubscription? _telemetrySub;
  StreamSubscription? _alarmSub;
  bool _isManualDisconnecting = false;
  bool _isConnecting = false;

  // Stream controllers
  final _connectionStateController =
      StreamController<BleConnectionState>.broadcast();
  final _telemetryController = StreamController<TelemetryData>.broadcast();
  final _alarmController = StreamController<AlarmData>.broadcast();

  BleConnectionState _currentState = BleConnectionState.disconnected;

  // Public API
  Stream<BleConnectionState> get connectionStateStream =>
      _connectionStateController.stream;
  Stream<TelemetryData> get telemetryStream => _telemetryController.stream;
  Stream<AlarmData> get alarmStream => _alarmController.stream;
  BleConnectionState get currentState => _currentState;
  BluetoothDevice? get connectedDevice => _device;
  Stream<List<ScanResult>> get scanResultsStream => FlutterBluePlus.scanResults;

  void _updateState(BleConnectionState state) {
    _currentState = state;
    _connectionStateController.add(state);
  }

  Future<void> startScan({
    Duration timeout = const Duration(seconds: 10),
  }) async {
    if (_device != null &&
        _currentState != BleConnectionState.disconnected &&
        _currentState != BleConnectionState.disconnecting) {
      throw Exception('Device still connected. Please disconnect first.');
    }

    _updateState(BleConnectionState.scanning);
    await FlutterBluePlus.startScan(timeout: timeout);
    Future.delayed(timeout, () {
      if (_currentState == BleConnectionState.scanning) {
        _updateState(BleConnectionState.disconnected);
      }
    });
  }

  Future<void> stopScan() async {
    await FlutterBluePlus.stopScan();
    if (_currentState == BleConnectionState.scanning) {
      _updateState(BleConnectionState.disconnected);
    }
  }

  Future<void> connect(BluetoothDevice device) async {
    _isConnecting = true;
    _updateState(BleConnectionState.connecting);
    try {
      await FlutterBluePlus.stopScan();
      _device = device;

      _connectionSub?.cancel();
      _connectionSub = device.connectionState.listen((state) {
        if (state == BluetoothConnectionState.disconnected &&
            !_isManualDisconnecting &&
            !_isConnecting) {
          _handleDisconnect();
        }
      });

      await device.connect(
        autoConnect: false,
        timeout: const Duration(seconds: 10),
      );

      await _ensureAndroidBondReady(device);

      final services = await device.discoverServices();

      BluetoothService? targetService;
      for (final service in services) {
        if (service.uuid == _serviceUuid) {
          targetService = service;
          break;
        }
      }

      if (targetService == null) {
        throw Exception('CareBand service not found');
      }

      for (final char in targetService.characteristics) {
        if (char.uuid == _telemetryUuid) {
          _telemetryChar = char;
          await char.setNotifyValue(true);
          _telemetrySub?.cancel();
          _telemetrySub = char.onValueReceived.listen((value) {
            final data = BleParser.parseTelemetry(value);
            if (data != null) {
              _telemetryController.add(data);
            }
          });
        } else if (char.uuid == _alarmUuid) {
          _alarmChar = char;
          await char.setNotifyValue(true);
          _alarmSub?.cancel();
          _alarmSub = char.onValueReceived.listen((value) {
            final data = BleParser.parseAlarm(value);
            if (data != null) {
              _alarmController.add(data);
            }
          });
        } else if (char.uuid == _commandUuid) {
          _commandChar = char;
        } else if (char.uuid == _statusUuid) {
          _statusChar = char;
        }
      }

      _isConnecting = false;
      _updateState(BleConnectionState.connected);

      // Keep connection successful even if time sync is not ready yet.
      try {
        await _sendSyncTimeWithRetry();
      } catch (_) {}
    } catch (e) {
      _isConnecting = false;
      final dev = _device;
      _handleDisconnect();
      try {
        await dev?.disconnect(queue: false);
      } catch (_) {}
      rethrow;
    }
  }

  Future<void> _ensureAndroidBondReady(BluetoothDevice device) async {
    if (!Platform.isAndroid) return;

    var state = await device.bondState.first.timeout(
      const Duration(seconds: 5),
    );

    if (state == BluetoothBondState.bonding) {
      state = await device.bondState
          .where((s) => s != BluetoothBondState.bonding)
          .first
          .timeout(const Duration(seconds: 20));
    }

    if (state != BluetoothBondState.bonded) {
      try {
        await device.createBond();
      } catch (e) {
        if (!_isBondingInProgressError(e)) {
          rethrow;
        }
      }

      state = await device.bondState
          .where((s) => s != BluetoothBondState.bonding)
          .first
          .timeout(const Duration(seconds: 20));

      if (state != BluetoothBondState.bonded) {
        throw Exception('Bonding failed: $state');
      }
    }

    // Android often needs extra time before encrypted writes are accepted.
    await Future.delayed(const Duration(milliseconds: 1200));
  }

  bool _isBondingInProgressError(Object e) {
    return e.toString().toLowerCase().contains('bonding already in progress');
  }

  bool _isAuthNotReadyError(Object e) {
    final msg = e.toString().toUpperCase();
    return msg.contains('GATT_INSUFFICIENT_AUTHORIZATION') ||
        msg.contains('ANDROID-CODE: 8') ||
        msg.contains('STATUS: 8');
  }

  Future<void> _sendSyncTimeWithRetry() async {
    Object? lastError;
    for (var i = 0; i < 3; i++) {
      try {
        await sendSyncTime();
        return;
      } catch (e) {
        lastError = e;
        if (!_isAuthNotReadyError(e) || i == 2) {
          rethrow;
        }
        await Future.delayed(Duration(milliseconds: 700 * (i + 1)));
      }
    }
    if (lastError != null) throw lastError;
  }

  void _handleDisconnect() {
    _telemetrySub?.cancel();
    _alarmSub?.cancel();
    _connectionSub?.cancel();
    _telemetrySub = null;
    _alarmSub = null;
    _connectionSub = null;
    _commandChar = null;
    _statusChar = null;
    _telemetryChar = null;
    _alarmChar = null;
    _device = null;
    _updateState(BleConnectionState.disconnected);
  }

  Future<void> disconnect() async {
    final device = _device;
    if (device == null) {
      _handleDisconnect();
      return;
    }

    _isManualDisconnecting = true;
    _updateState(BleConnectionState.disconnecting);

    _telemetrySub?.cancel();
    _alarmSub?.cancel();
    _telemetrySub = null;
    _alarmSub = null;

    try {
      await device.disconnect(queue: false, timeout: 15);
    } catch (_) {}

    _isManualDisconnecting = false;
    _handleDisconnect();
  }

  Future<void> _writeCommand(Uint8List data) async {
    if (_commandChar == null) throw Exception('Not connected');
    await _commandChar!.write(data.toList(), withoutResponse: false);
  }

  Future<void> sendAckAlarm(int eventId) async {
    await _writeCommand(BleCommand.ackAlarm(eventId));
  }

  Future<void> sendSyncTime() async {
    await _writeCommand(BleCommand.syncTime());
  }

  Future<void> sendRequestReport() async {
    await _writeCommand(BleCommand.requestReport());
  }

  Future<void> sendManualMeasure({
    required bool start,
    int durationSec = 15,
  }) async {
    await _writeCommand(
      BleCommand.manualMeasure(start: start, durationSec: durationSec),
    );
  }

  Future<DeviceStatus> readStatus() async {
    if (_statusChar == null) throw Exception('Not connected');
    final value = await _statusChar!.read();
    final status = BleParser.parseStatus(value);
    if (status == null) throw Exception('Invalid status data');
    return status;
  }

  void dispose() {
    _handleDisconnect();
    _connectionStateController.close();
    _telemetryController.close();
    _alarmController.close();
  }
}
