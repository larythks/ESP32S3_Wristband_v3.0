import 'dart:async';
import 'dart:typed_data';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import '../data/models.dart';
import 'ble_parser.dart';
import 'ble_command.dart';

class BleManager {
  static final BleManager instance = BleManager._();
  BleManager._();

  // UUIDs
  static final Guid _serviceUuid =
      Guid('0000ff00-0000-1000-8000-00805f9b34fb');
  static final Guid _telemetryUuid =
      Guid('0000ff01-0000-1000-8000-00805f9b34fb');
  static final Guid _alarmUuid =
      Guid('0000ff02-0000-1000-8000-00805f9b34fb');
  static final Guid _commandUuid =
      Guid('0000ff03-0000-1000-8000-00805f9b34fb');
  static final Guid _statusUuid =
      Guid('0000ff04-0000-1000-8000-00805f9b34fb');

  // Internal state
  BluetoothDevice? _device;
  BluetoothCharacteristic? _commandChar;
  BluetoothCharacteristic? _statusChar;
  StreamSubscription? _connectionSub;
  StreamSubscription? _telemetrySub;
  StreamSubscription? _alarmSub;

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
  Stream<List<ScanResult>> get scanResultsStream =>
      FlutterBluePlus.scanResults;

  void _updateState(BleConnectionState state) {
    _currentState = state;
    _connectionStateController.add(state);
  }

  Future<void> startScan(
      {Duration timeout = const Duration(seconds: 10)}) async {
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
    _updateState(BleConnectionState.connecting);
    try {
      await FlutterBluePlus.stopScan();
      _device = device;

      // Listen to connection state changes
      _connectionSub?.cancel();
      _connectionSub = device.connectionState.listen((state) {
        if (state == BluetoothConnectionState.disconnected) {
          _handleDisconnect();
        }
      });

      // Connect to device
      await device.connect(
          autoConnect: false, timeout: const Duration(seconds: 10));

      // Discover services
      List<BluetoothService> services = await device.discoverServices();

      // Find target service
      BluetoothService? targetService;
      for (var service in services) {
        if (service.uuid == _serviceUuid) {
          targetService = service;
          break;
        }
      }

      if (targetService == null) {
        throw Exception('CareBand service not found');
      }

      // Subscribe to characteristics
      for (var char in targetService.characteristics) {
        if (char.uuid == _telemetryUuid) {
          await char.setNotifyValue(true);
          _telemetrySub?.cancel();
          _telemetrySub = char.onValueReceived.listen((value) {
            final data = BleParser.parseTelemetry(value);
            if (data != null) {
              _telemetryController.add(data);
            }
          });
        } else if (char.uuid == _alarmUuid) {
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

      _updateState(BleConnectionState.connected);

      // Auto sync time after connection
      await sendSyncTime();
    } catch (e) {
      _handleDisconnect();
      rethrow;
    }
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
    _device = null;
    _updateState(BleConnectionState.disconnected);
  }

  Future<void> disconnect() async {
    _updateState(BleConnectionState.disconnecting);
    try {
      await _device?.disconnect();
    } finally {
      _handleDisconnect();
    }
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

  Future<void> sendManualMeasure(
      {required bool start, int durationSec = 15}) async {
    await _writeCommand(
        BleCommand.manualMeasure(start: start, durationSec: durationSec));
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
