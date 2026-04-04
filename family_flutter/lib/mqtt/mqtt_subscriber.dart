import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import 'package:mqtt_client/mqtt_client.dart';
import 'package:mqtt_client/mqtt_server_client.dart';

import '../data/models.dart';
import 'mqtt_config.dart';

/// MQTT 订阅者：家属端接收手环数据
class MqttSubscriber {
  MqttSubscriber._();
  static final MqttSubscriber instance = MqttSubscriber._();

  MqttServerClient? _client;
  StreamSubscription? _msgSub;
  bool _isConnected = false;
  bool _intentionalDisconnect = false;
  bool _isConnecting = false;
  Timer? _reconnectTimer;
  int _reconnectDelay = 2; // 秒，指数退避

  // ==================== 公开流 ====================

  final StreamController<bool> _connectedController =
      StreamController<bool>.broadcast();
  final StreamController<TelemetryRecord> _telemetryController =
      StreamController<TelemetryRecord>.broadcast();
  final StreamController<AlarmRecord> _alarmController =
      StreamController<AlarmRecord>.broadcast();
  final StreamController<DeviceStatusRecord> _statusController =
      StreamController<DeviceStatusRecord>.broadcast();

  /// 连接状态流
  Stream<bool> get connectedStream => _connectedController.stream;

  /// 当前是否已连接
  bool get isConnected => _isConnected;

  /// 遥测数据流
  Stream<TelemetryRecord> get telemetryStream => _telemetryController.stream;

  /// 报警事件流
  Stream<AlarmRecord> get alarmStream => _alarmController.stream;

  /// 设备状态流
  Stream<DeviceStatusRecord> get statusStream => _statusController.stream;

  // ==================== 公开方法 ====================

  /// 连接 MQTT Broker（TLS + 订阅 4 个 topic）
  Future<void> connect(String deviceId) async {
    if (_isConnected || _isConnecting) return;
    _isConnecting = true;
    _intentionalDisconnect = false;

    // 设置设备 ID
    MqttConfig.deviceId = deviceId;

    try {
      debugPrint('[MqttSubscriber] step 1: 加载 CA 证书...');
      final caBytes = await rootBundle.load(MqttConfig.caCertAssetPath);
      final certData = caBytes.buffer.asUint8List(
        caBytes.offsetInBytes,
        caBytes.lengthInBytes,
      );
      debugPrint('[MqttSubscriber] step 1: CA 证书已加载, ${certData.length} bytes');

      // 创建客户端
      final client = MqttServerClient.withPort(
        MqttConfig.broker,
        MqttConfig.clientId,
        MqttConfig.port,
      );

      client.logging(on: false);
      client.keepAlivePeriod = 30;
      client.connectTimeoutPeriod = 10000; // 10 秒
      client.autoReconnect = false; // 手动管理重连
      client.onConnected = _onConnected;
      client.onDisconnected = _onDisconnected;

      // TLS 配置：优先用 SecurityContext，失败则降级 onBadCertificate
      client.secure = true;
      debugPrint('[MqttSubscriber] step 2: 配置 TLS...');
      try {
        final securityContext = SecurityContext(withTrustedRoots: true);
        securityContext.setTrustedCertificatesBytes(certData);
        client.securityContext = securityContext;
        debugPrint('[MqttSubscriber] step 2: SecurityContext 配置成功');
      } catch (e) {
        debugPrint(
          '[MqttSubscriber] step 2: SecurityContext 失败: $e, '
          '降级使用 onBadCertificate',
        );
        client.onBadCertificate = (dynamic certificate) => true;
      }

      // LWT 遗嘱消息（家属端不设置 LWT，仅订阅设备的 LWT）
      final connMessage = MqttConnectMessage()
          .withClientIdentifier(MqttConfig.clientId)
          .authenticateAs(MqttConfig.username, MqttConfig.password)
          .startClean();
      client.connectionMessage = connMessage;

      _client = client;

      debugPrint(
        '[MqttSubscriber] step 3: 连接 '
        '${MqttConfig.broker}:${MqttConfig.port}...',
      );
      final status = await client.connect();
      debugPrint(
        '[MqttSubscriber] step 3: connect() 返回, '
        'state=${status?.state}, returnCode=${status?.returnCode}',
      );

      if (status?.state != MqttConnectionState.connected) {
        debugPrint(
          '[MqttSubscriber] 连接失败: state=${status?.state}, '
          'returnCode=${status?.returnCode}',
        );
        _client = null;
        _isConnecting = false;
        _updateConnected(false);
        _scheduleReconnect();
        return;
      }
    } catch (e, st) {
      debugPrint('[MqttSubscriber] 连接异常: $e');
      debugPrint('[MqttSubscriber] stackTrace: $st');
      _client = null;
      _isConnecting = false;
      _updateConnected(false);
      _scheduleReconnect();
      return;
    }

    _isConnecting = false;
  }

  /// 主动断开 MQTT
  Future<void> disconnect() async {
    _intentionalDisconnect = true;
    _isConnecting = false;
    _reconnectTimer?.cancel();
    _reconnectTimer = null;
    _reconnectDelay = 2;

    if (_client != null && _isConnected) {
      _client!.disconnect();
    }
    _msgSub?.cancel();
    _msgSub = null;
    _client = null;
    _updateConnected(false);
    debugPrint('[MqttSubscriber] 已断开连接');
  }

  /// 发布报警确认到 cmd topic
  Future<void> publishAckAlarm(int eventId) async {
    if (!_isConnected || _client == null) {
      debugPrint(
        '[MqttSubscriber] publishAckAlarm 跳过: '
        'connected=$_isConnected, client=${_client != null}',
      );
      return;
    }

    try {
      final payload = jsonEncode({
        'cmd': 'ack_alarm',
        'event_id': eventId,
        'source': 'family_app',
        'timestamp': DateTime.now().millisecondsSinceEpoch ~/ 1000,
      });

      final msgId = _publish(
        MqttConfig.cmdTopic,
        payload,
        MqttQos.atLeastOnce,
        false,
      );
      debugPrint(
        '[MqttSubscriber] publishAckAlarm: '
        'msgId=$msgId, eventId=$eventId',
      );
    } catch (e) {
      debugPrint('[MqttSubscriber] publishAckAlarm 错误: $e');
    }
  }

  /// 发布同步时间命令
  Future<void> publishSyncTime() async {
    if (!_isConnected || _client == null) {
      debugPrint('[MqttSubscriber] publishSyncTime 跳过: 未连接');
      return;
    }

    try {
      final payload = jsonEncode({
        'cmd': 'sync_time',
        'source': 'family_app',
        'timestamp': DateTime.now().millisecondsSinceEpoch ~/ 1000,
      });

      final msgId = _publish(
        MqttConfig.cmdTopic,
        payload,
        MqttQos.atLeastOnce,
        false,
      );
      debugPrint('[MqttSubscriber] publishSyncTime: msgId=$msgId');
    } catch (e) {
      debugPrint('[MqttSubscriber] publishSyncTime 错误: $e');
    }
  }

  /// 发布请求立即上报命令
  Future<void> publishRequestReport() async {
    if (!_isConnected || _client == null) {
      debugPrint('[MqttSubscriber] publishRequestReport 跳过: 未连接');
      return;
    }

    try {
      final payload = jsonEncode({
        'cmd': 'request_report',
        'source': 'family_app',
        'timestamp': DateTime.now().millisecondsSinceEpoch ~/ 1000,
      });

      final msgId = _publish(
        MqttConfig.cmdTopic,
        payload,
        MqttQos.atLeastOnce,
        false,
      );
      debugPrint('[MqttSubscriber] publishRequestReport: msgId=$msgId');
    } catch (e) {
      debugPrint('[MqttSubscriber] publishRequestReport 错误: $e');
    }
  }

  /// 发布手动测量命令
  Future<void> publishManualMeasure({
    required bool start,
    int durationSec = 15,
  }) async {
    if (!_isConnected || _client == null) {
      debugPrint('[MqttSubscriber] publishManualMeasure 跳过: 未连接');
      return;
    }

    try {
      final payload = jsonEncode({
        'cmd': 'manual_measure',
        'action': start ? 'start' : 'stop',
        'duration': durationSec,
        'source': 'family_app',
        'timestamp': DateTime.now().millisecondsSinceEpoch ~/ 1000,
      });

      final msgId = _publish(
        MqttConfig.cmdTopic,
        payload,
        MqttQos.atLeastOnce,
        false,
      );
      debugPrint(
        '[MqttSubscriber] publishManualMeasure: '
        'msgId=$msgId, start=$start, duration=$durationSec',
      );
    } catch (e) {
      debugPrint('[MqttSubscriber] publishManualMeasure 错误: $e');
    }
  }

  /// 释放资源
  void dispose() {
    _reconnectTimer?.cancel();
    _reconnectTimer = null;
    _msgSub?.cancel();
    _msgSub = null;
    _client?.disconnect();
    _client = null;
    _connectedController.close();
    _telemetryController.close();
    _alarmController.close();
    _statusController.close();
  }

  // ==================== 内部方法 ====================

  void _onConnected() {
    debugPrint('[MqttSubscriber] 已连接');
    _reconnectDelay = 2;
    _updateConnected(true);

    // 订阅 4 个 topic（不订阅 cmd）
    _client?.subscribe(MqttConfig.telemetryTopic, MqttQos.atLeastOnce);
    _client?.subscribe(MqttConfig.alarmTopic, MqttQos.atLeastOnce);
    _client?.subscribe(MqttConfig.statusTopic, MqttQos.atLeastOnce);
    _client?.subscribe(MqttConfig.lwtTopic, MqttQos.atLeastOnce);
    debugPrint(
      '[MqttSubscriber] 已订阅 4 个 topic: '
      'telemetry, alarm, status, lwt',
    );

    // 取消旧监听，避免重连时重复注册
    _msgSub?.cancel();
    _msgSub = _client?.updates?.listen(_onMessage);
  }

  void _onDisconnected() {
    debugPrint(
      '[MqttSubscriber] 断开回调, '
      'intentional=$_intentionalDisconnect, '
      'connecting=$_isConnecting',
    );
    _updateConnected(false);

    // 防重入：如果正在 connect() 流程中，由 connect() 自行处理重连
    if (!_intentionalDisconnect && !_isConnecting) {
      _scheduleReconnect();
    }
  }

  void _onMessage(List<MqttReceivedMessage<MqttMessage>> messages) {
    for (final msg in messages) {
      final topic = msg.topic;
      final pubMsg = msg.payload as MqttPublishMessage;
      final payloadStr = MqttPublishPayload.bytesToStringAsString(
        pubMsg.payload.message,
      );

      debugPrint('[MqttSubscriber] 收到消息: topic=$topic');

      try {
        final json = jsonDecode(payloadStr) as Map<String, dynamic>;
        _dispatchMessage(topic, json);
      } catch (e) {
        debugPrint('[MqttSubscriber] JSON 解析错误: $e');
      }
    }
  }

  /// 根据 topic 分发消息到对应的 StreamController
  void _dispatchMessage(String topic, Map<String, dynamic> json) {
    if (topic == MqttConfig.telemetryTopic) {
      final record = TelemetryRecord.fromMqttJson(json);
      if (!_telemetryController.isClosed) {
        _telemetryController.add(record);
      }
      debugPrint(
        '[MqttSubscriber] 遥测数据: '
        'hr=${record.heartRate}, temp=${record.temp}',
      );
    } else if (topic == MqttConfig.alarmTopic) {
      final record = AlarmRecord.fromMqttJson(json);
      if (!_alarmController.isClosed) {
        _alarmController.add(record);
      }
      debugPrint(
        '[MqttSubscriber] 报警事件: '
        'type=${record.alarmType}, eventId=${record.eventId}',
      );
    } else if (topic == MqttConfig.statusTopic ||
        topic == MqttConfig.lwtTopic) {
      final record = DeviceStatusRecord.fromMqttJson(json);
      if (!_statusController.isClosed) {
        _statusController.add(record);
      }
      debugPrint(
        '[MqttSubscriber] 设备状态: '
        'online=${record.online}, from=$topic',
      );
    } else {
      debugPrint('[MqttSubscriber] 未知 topic: $topic');
    }
  }

  int? _publish(String topic, String payload, MqttQos qos, bool retain) {
    if (_client == null) {
      debugPrint('[MqttSubscriber] _publish: client 为 null');
      return null;
    }

    try {
      final builder = MqttClientPayloadBuilder();
      builder.addString(payload);
      final msgId = _client!.publishMessage(
        topic,
        qos,
        builder.payload!,
        retain: retain,
      );
      return msgId;
    } catch (e) {
      debugPrint('[MqttSubscriber] _publish 到 $topic 失败: $e');
      return null;
    }
  }

  void _scheduleReconnect() {
    if (_intentionalDisconnect) return;

    _reconnectTimer?.cancel();
    debugPrint('[MqttSubscriber] ${_reconnectDelay}s 后重连...');
    _reconnectTimer = Timer(Duration(seconds: _reconnectDelay), () async {
      _reconnectDelay = (_reconnectDelay * 2).clamp(2, 30);
      await connect(MqttConfig.deviceId);
    });
  }

  void _updateConnected(bool connected) {
    if (_isConnected == connected) return;
    _isConnected = connected;
    if (!_connectedController.isClosed) {
      _connectedController.add(connected);
    }
  }
}
