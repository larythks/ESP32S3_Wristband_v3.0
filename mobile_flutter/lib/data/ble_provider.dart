import 'dart:async';
import 'package:flutter/services.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'models.dart';
import 'sqlite_repository.dart';
import '../ble/ble_manager.dart';
import '../mqtt/mqtt_gateway.dart';
import '../services/notification_service.dart';

class BleProvider extends ChangeNotifier with WidgetsBindingObserver {
  final BleManager _ble = BleManager.instance;
  static const _platform = MethodChannel('com.careband.app/platform');

  StreamSubscription<BleConnectionState>? _connectionStateSub;
  StreamSubscription<List<ScanResult>>? _scanResultsSub;
  StreamSubscription<TelemetryData>? _telemetrySub;
  StreamSubscription<AlarmData>? _alarmSub;
  StreamSubscription<bool>? _mqttConnectedSub;

  final MqttGateway _mqtt = MqttGateway.instance;
  late final SqliteDataRepository _repo;
  bool _repoReady = false;

  BleConnectionState _connectionState = BleConnectionState.disconnected;
  List<ScanResult> _scanResults = [];
  TelemetryData? _latestTelemetry;
  AlarmData? _latestAlarm;
  String? _errorMessage;
  String? _connectedDeviceName;
  bool _mqttConnected = false;
  bool _isAppInForeground = true;

  /// 通知点击后待跳转的 Tab 索引（null=无待跳转，1=AlarmTab）
  int? _pendingTabIndex;
  int? get pendingTabIndex => _pendingTabIndex;
  set pendingTabIndex(int? value) {
    _pendingTabIndex = value;
    if (value != null) notifyListeners();
  }

  // --- 历史记录（内存缓存 + SQLite 持久化） ---
  List<TelemetryData> _telemetryHistory = [];
  List<AlarmData> _alarmHistory = [];

  static const int _maxTelemetryCache = 360;
  static const int _maxAlarmCache = 100;

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
    WidgetsBinding.instance.addObserver(this);
    _initRepository();

    _connectionStateSub = _ble.connectionStateStream.listen((state) {
      _connectionState = state;
      if (state == BleConnectionState.connected) {
        _startBleService();
        _startMqtt();
        // 连接成功后自动请求上报数据
        Future.delayed(const Duration(milliseconds: 500), () {
          requestReport();
        });
      } else if (state == BleConnectionState.disconnected) {
        _stopMqtt();
        _stopBleService();
        _latestTelemetry = null;
        _latestAlarm = null;
        _scanResults = [];
        _connectedDeviceName = null;
        // 不再清空历史，数据已持久化到 SQLite
      }
      notifyListeners();
    });

    _telemetrySub = _ble.telemetryStream.listen((data) {
      _latestTelemetry = data;
      _telemetryHistory.add(data);
      if (_telemetryHistory.length > _maxTelemetryCache) {
        _telemetryHistory.removeAt(0);
      }
      if (_repoReady) {
        _repo.saveTelemetry(data);
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
      if (_alarmHistory.length > _maxAlarmCache) {
        _alarmHistory.removeAt(0);
      }
      if (_repoReady) {
        _repo.saveAlarm(data);
      }
      try {
        _mqtt.publishAlarm(data);
      } catch (e) {
        debugPrint('[BleProvider] publishAlarm error: $e');
      }
      // App 不在前台时发送系统通知
      if (!_isAppInForeground) {
        NotificationService.instance.showAlarmNotification(data);
      }
      notifyListeners();
    });
  }

  Future<void> _initRepository() async {
    try {
      _repo = SqliteDataRepository();
      await _repo.cleanup();

      // --- 合并遥测数据：保留 repo 初始化前通过 BLE 收到的内存数据 ---
      final dbTelemetry = await _repo.getTelemetryHistory(limit: 360);
      final dbTsSet = dbTelemetry
          .map((t) => t.timestamp.millisecondsSinceEpoch)
          .toSet();
      final unsavedTelemetry = _telemetryHistory
          .where((t) => !dbTsSet.contains(t.timestamp.millisecondsSinceEpoch))
          .toList();
      _telemetryHistory = [...dbTelemetry, ...unsavedTelemetry];

      // --- 合并报警数据：保留 repo 初始化前通过 BLE 收到的内存数据 ---
      final dbAlarms = await _repo.getAlarmHistory(limit: 100);
      final dbEventIds = dbAlarms.map((a) => a.eventId).toSet();
      final unsavedAlarms = _alarmHistory
          .where((a) => !dbEventIds.contains(a.eventId))
          .toList();
      _alarmHistory = [...dbAlarms, ...unsavedAlarms];

      _repoReady = true;

      // 将初始化前收到但未持久化的数据补写入 SQLite
      for (final alarm in unsavedAlarms) {
        await _repo.saveAlarm(alarm);
      }
      for (final t in unsavedTelemetry) {
        await _repo.saveTelemetry(t);
      }

      debugPrint('[BleProvider] repo ready, '
          'alarms=${_alarmHistory.length} (unsaved=${unsavedAlarms.length}), '
          'telemetry=${_telemetryHistory.length} (unsaved=${unsavedTelemetry.length})');
      notifyListeners();
    } catch (e) {
      debugPrint('[BleProvider] SQLite init failed: $e');
      // 降级为纯内存模式，不影响核心 BLE 功能
    }
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

  Future<bool> ackAlarm(int eventId) async {
    // 1. 先更新本地状态（乐观更新），确保 UI 立即反映
    bool found = false;
    for (final alarm in _alarmHistory) {
      if (alarm.eventId == eventId) {
        alarm.isAcked = true;
        found = true;
      }
    }
    if (!found) {
      debugPrint('[BleProvider] ackAlarm: eventId=$eventId not found in '
          '_alarmHistory (${_alarmHistory.length} items)');
      _errorMessage = '确认报警失败: 未找到对应的报警记录';
      notifyListeners();
      return false;
    }
    notifyListeners();

    // 2. 发送 BLE ACK 命令
    try {
      await _ble.sendAckAlarm(eventId);
      debugPrint('[BleProvider] ackAlarm: BLE ACK sent for eventId=$eventId');
    } catch (e) {
      debugPrint('[BleProvider] ackAlarm: BLE send failed: $e');
      // 本地状态已更新，BLE 发送失败不回滚
    }

    // 3. 同步更新 SQLite
    if (_repoReady) {
      try {
        await _repo.updateAlarmAcked(eventId);
      } catch (e) {
        debugPrint('[BleProvider] ackAlarm: SQLite update failed: $e');
      }
    }
    return true;
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
  void didChangeAppLifecycleState(AppLifecycleState state) {
    _isAppInForeground = (state == AppLifecycleState.resumed);
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    _connectionStateSub?.cancel();
    _scanResultsSub?.cancel();
    _telemetrySub?.cancel();
    _alarmSub?.cancel();
    _mqttConnectedSub?.cancel();
    _mqtt.dispose();
    _ble.dispose();
    if (_repoReady) {
      _repo.dispose();
    }
    super.dispose();
  }
}
