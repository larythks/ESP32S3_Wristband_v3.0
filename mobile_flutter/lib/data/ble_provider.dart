import 'dart:async';
import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'models.dart';
import '../ble/ble_manager.dart';
import '../mqtt/mqtt_gateway.dart';

class BleProvider extends ChangeNotifier {
  final BleManager _ble = BleManager.instance;
  static const _platform = MethodChannel('com.careband.app/platform');

  StreamSubscription<BleConnectionState>? _connectionStateSub;
  StreamSubscription<List<ScanResult>>? _scanResultsSub;
  StreamSubscription<TelemetryData>? _telemetrySub;
  StreamSubscription<AlarmData>? _alarmSub;
  StreamSubscription<bool>? _mqttConnectedSub;

  final MqttGateway _mqtt = MqttGateway.instance;

  BleConnectionState _connectionState = BleConnectionState.disconnected;
  List<ScanResult> _scanResults = [];
  TelemetryData? _latestTelemetry;
  AlarmData? _latestAlarm;
  String? _errorMessage;
  String? _connectedDeviceName;
  bool _mqttConnected = false;

  // --- 历史记录（内存中） ---
  final List<TelemetryData> _telemetryHistory = [];
  final List<AlarmData> _alarmHistory = [];

  static const int maxTelemetryHistory = 30;
  static const int maxAlarmHistory = 50;

  List<TelemetryData> get telemetryHistory =>
      List.unmodifiable(_telemetryHistory);
  List<AlarmData> get alarmHistory =>
      List.unmodifiable(_alarmHistory);

  BleConnectionState get connectionState => _connectionState;
  List<ScanResult> get scanResults => _scanResults;
  TelemetryData? get latestTelemetry => _latestTelemetry;
  AlarmData? get latestAlarm => _latestAlarm;
  bool get isConnected => _connectionState == BleConnectionState.connected;
  String? get errorMessage => _errorMessage;
  String? get connectedDeviceName => _connectedDeviceName;
  bool get mqttConnected => _mqttConnected;

  BleProvider() {
    _connectionStateSub = _ble.connectionStateStream.listen((state) {
      _connectionState = state;
      if (state == BleConnectionState.connected) {
        _startBleService();
        _startMqtt();
      } else if (state == BleConnectionState.disconnected) {
        _stopMqtt();
        _stopBleService();
        _latestTelemetry = null;
        _latestAlarm = null;
        _scanResults = [];
        _connectedDeviceName = null;
        _telemetryHistory.clear();
        _alarmHistory.clear();
      }
      notifyListeners();
    });

    _telemetrySub = _ble.telemetryStream.listen((data) {
      _latestTelemetry = data;
      _telemetryHistory.add(data);
      if (_telemetryHistory.length > maxTelemetryHistory) {
        _telemetryHistory.removeAt(0);
      }
      try {
        _mqtt.publishTelemetry(data);
      } catch (e) {
        debugPrint('[BleProvider] publishTelemetry error: $e');
      }
      notifyListeners();
    });

    _alarmSub = _ble.alarmStream.listen((data) {
      _latestAlarm = data;
      _alarmHistory.add(data);
      if (_alarmHistory.length > maxAlarmHistory) {
        _alarmHistory.removeAt(0);
      }
      try {
        _mqtt.publishAlarm(data);
      } catch (e) {
        debugPrint('[BleProvider] publishAlarm error: $e');
      }
      notifyListeners();
    });
  }

  Future<void> startScan() async {
    _errorMessage = null;
    _scanResults = [];
    notifyListeners();

    try {
      _scanResultsSub?.cancel();
      _scanResultsSub = null;
      await _ble.startScan();
      // 扫描启动成功后再订阅结果流，避免收到系统缓存的旧结果
      _scanResultsSub = _ble.scanResultsStream.listen((results) {
        _scanResults = results;
        notifyListeners();
      });
    } catch (e) {
      _scanResultsSub?.cancel();
      _scanResultsSub = null;
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
    _connectedDeviceName = device.platformName;
    _scanResults = [];
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
      debugPrint('[BleProvider] disconnect requested, state=$_connectionState');
      await _ble.disconnect();
      debugPrint('[BleProvider] disconnect done, state=$_connectionState');
    } catch (e) {
      debugPrint('[BleProvider] disconnect failed: $e');
      _errorMessage = '断开连接失败: $e';
      notifyListeners();
    }
  }

  Future<void> ackAlarm(int eventId) async {
    try {
      await _ble.sendAckAlarm(eventId);
      // 更新本地报警状态为已确认
      for (final alarm in _alarmHistory) {
        if (alarm.eventId == eventId) {
          alarm.isAcked = true;
          break;
        }
      }
      notifyListeners();
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
      await _ble.sendManualMeasure(start: start, durationSec: durationSec);
    } catch (e) {
      _errorMessage = '手动测量失败: $e';
      notifyListeners();
    }
  }

  void _startBleService() {
    _platform.invokeMethod('startBleService').catchError((e) {
      debugPrint('[BleProvider] startBleService failed: $e');
    });
  }

  void _stopBleService() {
    _platform.invokeMethod('stopBleService').catchError((e) {
      debugPrint('[BleProvider] stopBleService failed: $e');
    });
  }

  void _startMqtt() {
    _mqttConnectedSub?.cancel();
    _mqttConnectedSub = _mqtt.connectedStream.listen((connected) {
      _mqttConnected = connected;
      notifyListeners();
    });
    _mqtt.connect().catchError((e) {
      debugPrint('[BleProvider] MQTT connect failed: $e');
    });
  }

  void _stopMqtt() {
    _mqttConnectedSub?.cancel();
    _mqttConnectedSub = null;
    _mqtt.disconnect().catchError((e) {
      debugPrint('[BleProvider] MQTT disconnect failed: $e');
    });
    _mqttConnected = false;
  }

  @override
  void dispose() {
    _connectionStateSub?.cancel();
    _scanResultsSub?.cancel();
    _telemetrySub?.cancel();
    _alarmSub?.cancel();
    _mqttConnectedSub?.cancel();
    _mqtt.dispose();
    _ble.dispose();
    super.dispose();
  }
}
