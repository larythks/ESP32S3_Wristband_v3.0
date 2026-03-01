import 'dart:async';

import 'package:flutter/services.dart';
import 'package:flutter/widgets.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../data/family_repository.dart';
import '../data/models.dart';
import '../mqtt/mqtt_subscriber.dart';
import '../services/notification_service.dart';

/// 设备状态管理 Provider（ChangeNotifier）
///
/// 负责：设备绑定、MQTT 流消费、数据持久化、前后台通知、报警确认
class DeviceProvider extends ChangeNotifier with WidgetsBindingObserver {
  static const String _prefKeyDeviceId = 'device_id';
  static const _keepAliveChannel =
      MethodChannel('com.careband.family/keepalive');

  final MqttSubscriber _mqtt = MqttSubscriber.instance;
  final FamilyRepository _repo = FamilyRepository.instance;

  // 流订阅
  StreamSubscription<TelemetryRecord>? _telemetrySub;
  StreamSubscription<AlarmRecord>? _alarmSub;
  StreamSubscription<DeviceStatusRecord>? _statusSub;
  StreamSubscription<bool>? _connectedSub;

  // ---- 绑定状态 ----
  String? _deviceId;
  String? get deviceId => _deviceId;
  bool get isBound => _deviceId != null && _deviceId!.isNotEmpty;

  // ---- 连接状态 ----
  bool _mqttConnected = false;
  bool get mqttConnected => _mqttConnected;

  bool _deviceOnline = false;
  bool get deviceOnline => _deviceOnline;

  DateTime? _lastSeen;
  DateTime? get lastSeen => _lastSeen;

  // ---- 最新数据 ----
  TelemetryRecord? _latestTelemetry;
  TelemetryRecord? get latestTelemetry => _latestTelemetry;

  AlarmRecord? _latestAlarm;
  AlarmRecord? get latestAlarm => _latestAlarm;

  // ---- 历史数据 ----
  List<TelemetryRecord> _telemetryHistory = [];
  List<TelemetryRecord> get telemetryHistory =>
      List.unmodifiable(_telemetryHistory);

  List<AlarmRecord> _alarmHistory = [];
  List<AlarmRecord> get alarmHistory => List.unmodifiable(_alarmHistory);

  int _unackedAlarmCount = 0;
  int get unackedAlarmCount => _unackedAlarmCount;

  bool _isLoading = false;
  bool get isLoading => _isLoading;

  // ---- Tab 切换 ----
  int? _pendingTabIndex;
  int? get pendingTabIndex => _pendingTabIndex;
  set pendingTabIndex(int? value) {
    _pendingTabIndex = value;
    if (value != null) notifyListeners();
  }

  // ---- 前后台状态 ----
  bool _isAppInForeground = true;

  DeviceProvider() {
    WidgetsBinding.instance.addObserver(this);
    _initFromPrefs();
  }

  /// 从 SharedPreferences 读取已绑定的 device_id，有则自动连接
  Future<void> _initFromPrefs() async {
    try {
      final prefs = await SharedPreferences.getInstance();
      final savedId = prefs.getString(_prefKeyDeviceId);
      if (savedId != null && savedId.isNotEmpty) {
        debugPrint('[DeviceProvider] 发现已绑定设备: $savedId，自动连接');
        _deviceId = savedId;
        notifyListeners();
        await _startConnection();
        await refreshHistory();
      }
    } catch (e) {
      debugPrint('[DeviceProvider] 初始化读取偏好失败: $e');
    }
  }

  // ==================== 操作 ====================

  /// 绑定设备：保存 device_id 并连接 MQTT
  Future<void> bindDevice(String deviceId) async {
    if (deviceId.isEmpty) return;

    _deviceId = deviceId;
    notifyListeners();

    // 持久化
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(_prefKeyDeviceId, deviceId);
    debugPrint('[DeviceProvider] 设备已绑定: $deviceId');

    // 启动前台服务
    _startKeepAliveService();

    // 连接 MQTT 并订阅流
    await _startConnection();
  }

  /// 解绑设备：清除绑定并断开 MQTT
  Future<void> unbindDevice() async {
    debugPrint('[DeviceProvider] 解绑设备: $_deviceId');

    // 停止前台服务
    _stopKeepAliveService();

    // 断开 MQTT 并取消订阅
    _cancelSubscriptions();
    await _mqtt.disconnect();

    // 清除状态
    _deviceId = null;
    _mqttConnected = false;
    _deviceOnline = false;
    _lastSeen = null;
    _latestTelemetry = null;
    _latestAlarm = null;
    _telemetryHistory = [];
    _alarmHistory = [];
    _unackedAlarmCount = 0;

    // 清除持久化
    final prefs = await SharedPreferences.getInstance();
    await prefs.remove(_prefKeyDeviceId);

    notifyListeners();
  }

  /// 确认报警
  Future<bool> ackAlarm(int eventId) async {
    try {
      // 发布 MQTT ACK
      await _mqtt.publishAckAlarm(eventId);

      // 更新本地数据库
      final now = DateTime.now();
      await _repo.updateAlarmAcked(eventId, now);

      // 更新内存中的报警记录
      for (final alarm in _alarmHistory) {
        if (alarm.eventId == eventId) {
          alarm.acknowledged = true;
          alarm.ackedAt = now;
        }
      }

      // 更新未确认计数
      if (_deviceId != null) {
        _unackedAlarmCount = await _repo.getUnackedAlarmCount(_deviceId!);
      }

      debugPrint('[DeviceProvider] 报警已确认: eventId=$eventId');
      notifyListeners();
      return true;
    } catch (e) {
      debugPrint('[DeviceProvider] 确认报警失败: $e');
      return false;
    }
  }

  /// 远程同步时间
  Future<void> syncTime() async {
    try {
      await _mqtt.publishSyncTime();
      debugPrint('[DeviceProvider] 已发送同步时间命令');
    } catch (e) {
      debugPrint('[DeviceProvider] 同步时间失败: $e');
    }
  }

  /// 远程请求立即上报
  Future<void> requestReport() async {
    try {
      await _mqtt.publishRequestReport();
      debugPrint('[DeviceProvider] 已发送请求上报命令');
    } catch (e) {
      debugPrint('[DeviceProvider] 请求上报失败: $e');
    }
  }

  /// 远程手动测量
  Future<void> manualMeasure({
    required bool start,
    int durationSec = 15,
  }) async {
    try {
      await _mqtt.publishManualMeasure(
        start: start,
        durationSec: durationSec,
      );
      debugPrint(
        '[DeviceProvider] 已发送手动测量命令: '
        'start=$start, duration=$durationSec',
      );
    } catch (e) {
      debugPrint('[DeviceProvider] 手动测量失败: $e');
    }
  }

  /// 刷新历史数据（从数据库加载）
  Future<void> refreshHistory() async {
    if (_deviceId == null) return;

    _isLoading = true;
    notifyListeners();
    try {
      _telemetryHistory = await _repo.getTelemetryHistory(
        deviceId: _deviceId!,
        limit: 500,
      );
      _alarmHistory = await _repo.getAlarmHistory(
        deviceId: _deviceId!,
        limit: 200,
      );
      _unackedAlarmCount = await _repo.getUnackedAlarmCount(_deviceId!);

      // 更新最新数据
      if (_telemetryHistory.isNotEmpty) {
        _latestTelemetry = _telemetryHistory.last;
      }
      if (_alarmHistory.isNotEmpty) {
        _latestAlarm = _alarmHistory.first; // alarmHistory 按时间倒序
      }

      debugPrint(
        '[DeviceProvider] 历史数据已刷新: '
        '遥测=${_telemetryHistory.length}, '
        '报警=${_alarmHistory.length}, '
        '未确认=$_unackedAlarmCount',
      );
    } catch (e) {
      debugPrint('[DeviceProvider] 刷新历史数据失败: $e');
    } finally {
      _isLoading = false;
      notifyListeners();
    }
  }

  // ==================== 内部方法 ====================

  /// 启动 MQTT 连接并订阅流
  Future<void> _startConnection() async {
    if (_deviceId == null) return;

    // 订阅 MQTT 各数据流
    _cancelSubscriptions();

    _connectedSub = _mqtt.connectedStream.listen(_onMqttConnected);
    _telemetrySub = _mqtt.telemetryStream.listen(_onTelemetry);
    _alarmSub = _mqtt.alarmStream.listen(_onAlarm);
    _statusSub = _mqtt.statusStream.listen(_onStatus);

    // 连接 MQTT
    await _mqtt.connect(_deviceId!);

    // 启动前台服务
    _startKeepAliveService();
  }

  void _cancelSubscriptions() {
    _connectedSub?.cancel();
    _connectedSub = null;
    _telemetrySub?.cancel();
    _telemetrySub = null;
    _alarmSub?.cancel();
    _alarmSub = null;
    _statusSub?.cancel();
    _statusSub = null;
  }

  /// MQTT 连接状态变更回调
  void _onMqttConnected(bool connected) {
    _mqttConnected = connected;
    debugPrint('[DeviceProvider] MQTT 连接状态: $connected');
    notifyListeners();

    // MQTT 连接成功后，请求设备立即上报数据以检测在线状态
    if (connected) {
      Future.delayed(const Duration(milliseconds: 500), () {
        requestReport();
      });
    }
  }

  /// 收到遥测数据
  void _onTelemetry(TelemetryRecord record) {
    _latestTelemetry = record;
    _telemetryHistory.add(record);

    // 限制内存缓存大小
    if (_telemetryHistory.length > 500) {
      _telemetryHistory.removeAt(0);
    }

    // 持久化
    _repo.saveTelemetry(record);

    // 更新设备在线状态
    _deviceOnline = true;
    _lastSeen = DateTime.now();

    debugPrint(
      '[DeviceProvider] 遥测数据: '
      'hr=${record.heartRate}, spo2=${record.spo2}, '
      'temp=${record.temp}, steps=${record.steps}',
    );
    notifyListeners();
  }

  /// 收到报警事件
  void _onAlarm(AlarmRecord record) {
    _latestAlarm = record;
    // 插入到列表头部（按时间倒序）
    _alarmHistory.insert(0, record);
    if (_alarmHistory.length > 200) {
      _alarmHistory.removeLast();
    }
    _unackedAlarmCount++;

    // 持久化
    _repo.saveAlarm(record);

    // 根据用户偏好决定是否发送通知及振动
    _handleAlarmNotification(record);

    debugPrint(
      '[DeviceProvider] 报警事件: '
      'type=${record.alarmType}, eventId=${record.eventId}',
    );
    notifyListeners();
  }

  /// 根据通知偏好发送报警通知
  Future<void> _handleAlarmNotification(AlarmRecord record) async {
    try {
      final prefs = await SharedPreferences.getInstance();
      final notifyEnabled = prefs.getBool('notify_enabled') ?? true;
      if (!notifyEnabled) {
        debugPrint('[DeviceProvider] 通知已关闭，跳过系统通知');
        return;
      }
      final vibrateEnabled = prefs.getBool('vibrate_enabled') ?? true;
      await NotificationService.instance.showAlarmNotification(
        record,
        enableVibration: vibrateEnabled,
      );
    } catch (e) {
      debugPrint('[DeviceProvider] 发送报警通知失败: $e');
    }
  }

  /// 收到设备状态更新
  void _onStatus(DeviceStatusRecord record) {
    _deviceOnline = record.online;
    _lastSeen = record.lastSeen;
    debugPrint('[DeviceProvider] 设备状态: online=${record.online}');
    notifyListeners();
  }

  /// 启动前台保活服务
  void _startKeepAliveService() {
    _keepAliveChannel.invokeMethod('startService').catchError((e) {
      debugPrint('[DeviceProvider] 启动前台服务失败: $e');
    });
  }

  /// 停止前台保活服务
  void _stopKeepAliveService() {
    _keepAliveChannel.invokeMethod('stopService').catchError((e) {
      debugPrint('[DeviceProvider] 停止前台服务失败: $e');
    });
  }

  // ==================== 生命周期 ====================

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    _isAppInForeground = (state == AppLifecycleState.resumed);
    debugPrint('[DeviceProvider] App 生命周期: $state, 前台=$_isAppInForeground');
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    _cancelSubscriptions();
    super.dispose();
  }
}
