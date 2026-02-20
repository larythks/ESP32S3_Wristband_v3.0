import 'dart:async';
import 'package:flutter/foundation.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'models.dart';
import '../ble/ble_manager.dart';

/// BLE 状态管理 Provider
class BleProvider extends ChangeNotifier {
  final BleManager _ble = BleManager.instance;

  // Stream subscriptions
  StreamSubscription<BleConnectionState>? _connectionStateSub;
  StreamSubscription<List<ScanResult>>? _scanResultsSub;
  StreamSubscription<TelemetryData>? _telemetrySub;
  StreamSubscription<AlarmData>? _alarmSub;

  // 状态
  BleConnectionState _connectionState = BleConnectionState.disconnected;
  List<ScanResult> _scanResults = [];
  TelemetryData? _latestTelemetry;
  AlarmData? _latestAlarm;
  String? _errorMessage;

  // Getters
  BleConnectionState get connectionState => _connectionState;
  List<ScanResult> get scanResults => _scanResults;
  TelemetryData? get latestTelemetry => _latestTelemetry;
  AlarmData? get latestAlarm => _latestAlarm;
  bool get isConnected => _connectionState == BleConnectionState.connected;
  String? get errorMessage => _errorMessage;

  BleProvider() {
    _connectionStateSub = _ble.connectionStateStream.listen((state) {
      _connectionState = state;
      if (state == BleConnectionState.disconnected) {
        _latestTelemetry = null;
        _latestAlarm = null;
        _scanResults = [];
      }
      notifyListeners();
    });

    _telemetrySub = _ble.telemetryStream.listen((data) {
      _latestTelemetry = data;
      notifyListeners();
    });

    _alarmSub = _ble.alarmStream.listen((data) {
      _latestAlarm = data;
      notifyListeners();
    });
  }

  Future<void> startScan() async {
    _errorMessage = null;
    _scanResults = [];
    notifyListeners();

    try {
      _scanResultsSub?.cancel();
      _scanResultsSub = _ble.scanResultsStream.listen((results) {
        _scanResults = results;
        notifyListeners();
      });
      await _ble.startScan();
    } catch (e) {
      _errorMessage = '扫描失败: $e';
      notifyListeners();
    }
  }

  Future<void> stopScan() async {
    try {
      await _ble.stopScan();
      _scanResultsSub?.cancel();
      _scanResultsSub = null;
    } catch (e) {
      _errorMessage = '停止扫描失败: $e';
      notifyListeners();
    }
  }

  Future<void> connectDevice(BluetoothDevice device) async {
    _errorMessage = null;
    notifyListeners();

    try {
      _scanResultsSub?.cancel();
      _scanResultsSub = null;
      await _ble.connect(device);
    } catch (e) {
      _errorMessage = '连接失败: $e';
      notifyListeners();
    }
  }

  Future<void> disconnect() async {
    try {
      await _ble.disconnect();
    } catch (e) {
      _errorMessage = '断开连接失败: $e';
      notifyListeners();
    }
  }

  Future<void> ackAlarm(int eventId) async {
    try {
      await _ble.sendAckAlarm(eventId);
    } catch (e) {
      _errorMessage = '确认报警失败: $e';
      notifyListeners();
    }
  }

  Future<void> syncTime() async {
    try {
      await _ble.sendSyncTime();
    } catch (e) {
      _errorMessage = '时间同步失败: $e';
      notifyListeners();
    }
  }

  Future<void> requestReport() async {
    try {
      await _ble.sendRequestReport();
    } catch (e) {
      _errorMessage = '请求报告失败: $e';
      notifyListeners();
    }
  }

  Future<void> manualMeasure({
    required bool start,
    int durationSec = 15,
  }) async {
    try {
      await _ble.sendManualMeasure(
        start: start,
        durationSec: durationSec,
      );
    } catch (e) {
      _errorMessage = '手动测量失败: $e';
      notifyListeners();
    }
  }

  @override
  void dispose() {
    _connectionStateSub?.cancel();
    _scanResultsSub?.cancel();
    _telemetrySub?.cancel();
    _alarmSub?.cancel();
    _ble.dispose();
    super.dispose();
  }
}
