import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import 'package:mqtt_client/mqtt_client.dart';
import 'package:mqtt_client/mqtt_server_client.dart';

import '../ble/ble_manager.dart';
import '../data/models.dart';
import 'mqtt_config.dart';

/// MQTT 网关：BLE ↔ MQTT 桥梁
class MqttGateway {
  MqttGateway._();
  static final MqttGateway instance = MqttGateway._();

  MqttServerClient? _client;
  StreamSubscription? _updatesSub;
  bool _isConnected = false;
  bool _intentionalDisconnect = false;
  bool _isConnecting = false;
  Timer? _reconnectTimer;
  int _reconnectDelay = 2; // 秒，指数退避

  final StreamController<bool> _connectedController =
      StreamController<bool>.broadcast();

  /// 连接状态流
  Stream<bool> get connectedStream => _connectedController.stream;

  /// 当前是否已连接
  bool get isConnected => _isConnected;

  /// alarm_type 枚举 → JSON 字符串映射
  static const Map<AlarmType, String> _alarmTypeStrings = {
    AlarmType.tempHigh: 'temp_high',
    AlarmType.tempLow: 'temp_low',
    AlarmType.heartRateHigh: 'heart_rate_high',
    AlarmType.heartRateLow: 'heart_rate_low',
    AlarmType.spo2Low: 'spo2_low',
    AlarmType.fall: 'fall',
    AlarmType.manual: 'manual',
    AlarmType.callFamily: 'call_family',
  };

  /// 连接 MQTT Broker（TLS + LWT）
  Future<void> connect() async {
    if (_isConnected || _isConnecting) return;
    _isConnecting = true;
    _intentionalDisconnect = false;

    try {
      debugPrint('[MqttGateway] step 1: loading CA cert...');
      final caBytes = await rootBundle.load(MqttConfig.caCertAssetPath);
      final certData = caBytes.buffer.asUint8List(
        caBytes.offsetInBytes,
        caBytes.lengthInBytes,
      );
      debugPrint('[MqttGateway] step 1: CA cert loaded, ${certData.length} bytes');

      // 创建客户端
      final client = MqttServerClient.withPort(
        MqttConfig.broker,
        MqttConfig.clientId,
        MqttConfig.port,
      );

      client.logging(on: false);
      client.keepAlivePeriod = 30;
      client.connectTimeoutPeriod = 10000; // 10 秒，移动网络需要更长
      client.autoReconnect = false;
      client.onConnected = _onConnected;
      client.onDisconnected = _onDisconnected;

      // TLS 配置：优先用 SecurityContext，失败则降级 onBadCertificate
      client.secure = true;
      debugPrint('[MqttGateway] step 2: setting up TLS...');
      try {
        final securityContext = SecurityContext(withTrustedRoots: true);
        securityContext.setTrustedCertificatesBytes(certData);
        client.securityContext = securityContext;
        debugPrint('[MqttGateway] step 2: SecurityContext with CA cert OK');
      } catch (e) {
        debugPrint('[MqttGateway] step 2: SecurityContext failed: $e, using onBadCertificate fallback');
        client.onBadCertificate = (dynamic certificate) => true;
      }

      // LWT 遗嘱消息
      final lwtPayload = jsonEncode({
        'device_id': MqttConfig.deviceId,
        'online': false,
      });
      final connMessage = MqttConnectMessage()
          .withClientIdentifier(MqttConfig.clientId)
          .authenticateAs(MqttConfig.username, MqttConfig.password)
          .withWillTopic(MqttConfig.lwtTopic)
          .withWillMessage(lwtPayload)
          .withWillQos(MqttQos.atLeastOnce)
          .withWillRetain()
          .startClean();
      client.connectionMessage = connMessage;

      _client = client;

      debugPrint('[MqttGateway] step 3: connecting to ${MqttConfig.broker}:${MqttConfig.port}...');
      final status = await client.connect();
      debugPrint('[MqttGateway] step 3: connect() returned, state=${status?.state}, returnCode=${status?.returnCode}');

      if (status?.state != MqttConnectionState.connected) {
        debugPrint('[MqttGateway] connect failed: state=${status?.state}, returnCode=${status?.returnCode}');
        _client = null;
        _isConnecting = false;
        _updateConnected(false);
        _scheduleReconnect();
        return;
      }
    } catch (e, st) {
      debugPrint('[MqttGateway] connect exception: $e');
      debugPrint('[MqttGateway] stackTrace: $st');
      _client = null;
      _isConnecting = false;
      _updateConnected(false);
      _scheduleReconnect();
      return;
    }

    _isConnecting = false;
  }

  /// 主动断开 MQTT（先发 offline status）
  Future<void> disconnect() async {
    _intentionalDisconnect = true;
    _isConnecting = false;
    _reconnectTimer?.cancel();
    _reconnectTimer = null;
    _reconnectDelay = 2;

    if (_client != null && _isConnected) {
      _publishOnlineStatus(false);
      _client!.disconnect();
    }
    _updatesSub?.cancel();
    _updatesSub = null;
    _client = null;
    _updateConnected(false);
    debugPrint('[MqttGateway] disconnected');
  }

  /// 转发 Telemetry 到 MQTT（QoS 0）
  void publishTelemetry(TelemetryData data) {
    if (!_isConnected || _client == null) {
      debugPrint('[MqttGateway] publishTelemetry skipped: connected=$_isConnected, client=${_client != null}');
      return;
    }

    try {
      final payload = jsonEncode({
        'device_id': MqttConfig.deviceId,
        'timestamp': data.timestamp.millisecondsSinceEpoch ~/ 1000,
        'temp': data.temperature,
        'heart_rate': data.heartRate,
        'spo2': data.spo2,
        'steps': data.steps,
        'battery': data.battery,
      });

      final msgId = _publish(MqttConfig.telemetryTopic, payload, MqttQos.atMostOnce, false);
      debugPrint('[MqttGateway] publishTelemetry: msgId=$msgId, hr=${data.heartRate}, temp=${data.temperature}');
    } catch (e) {
      debugPrint('[MqttGateway] publishTelemetry error: $e');
    }
  }

  /// 转发 Alarm 到 MQTT（QoS 1）
  void publishAlarm(AlarmData data) {
    if (!_isConnected || _client == null) {
      debugPrint('[MqttGateway] publishAlarm skipped: connected=$_isConnected, client=${_client != null}');
      return;
    }

    try {
      final typeStr = _alarmTypeStrings[data.alarmType] ?? 'unknown';
      final payload = jsonEncode({
        'device_id': MqttConfig.deviceId,
        'event_id': data.eventId,
        'alarm_type': typeStr,
        'value': data.triggerValue,
        'timestamp': data.timestamp.millisecondsSinceEpoch ~/ 1000,
        'battery': data.battery,
      });

      final msgId = _publish(MqttConfig.alarmTopic, payload, MqttQos.atLeastOnce, false);
      debugPrint('[MqttGateway] publishAlarm: msgId=$msgId, type=$typeStr, eventId=${data.eventId}');
    } catch (e) {
      debugPrint('[MqttGateway] publishAlarm error: $e');
    }
  }

  /// 释放资源
  void dispose() {
    _reconnectTimer?.cancel();
    _reconnectTimer = null;
    _updatesSub?.cancel();
    _updatesSub = null;
    _client?.disconnect();
    _client = null;
    _connectedController.close();
  }

  // ==================== 内部方法 ====================

  void _onConnected() {
    debugPrint('[MqttGateway] connected');
    _reconnectDelay = 2;
    _updateConnected(true);

    // 发布在线状态（retained）
    _publishOnlineStatus(true);

    // 订阅下行命令 topic
    _client?.subscribe(MqttConfig.cmdTopic, MqttQos.atLeastOnce);

    // 取消旧监听，避免重连时重复注册
    _updatesSub?.cancel();
    _updatesSub = _client?.updates?.listen(_onMessage);
  }

  void _onDisconnected() {
    debugPrint('[MqttGateway] disconnected callback, intentional=$_intentionalDisconnect, connecting=$_isConnecting');
    _updateConnected(false);

    // 防重入：如果正在 connect() 流程中，由 connect() 自行处理重连
    if (!_intentionalDisconnect && !_isConnecting) {
      _scheduleReconnect();
    }
  }

  void _onMessage(List<MqttReceivedMessage<MqttMessage>> messages) {
    for (final msg in messages) {
      if (msg.topic != MqttConfig.cmdTopic) continue;

      final pubMsg = msg.payload as MqttPublishMessage;
      final payloadStr = MqttPublishPayload.bytesToStringAsString(
        pubMsg.payload.message,
      );

      debugPrint('[MqttGateway] cmd received: $payloadStr');

      try {
        final json = jsonDecode(payloadStr) as Map<String, dynamic>;
        _handleCommand(json);
      } catch (e) {
        debugPrint('[MqttGateway] cmd parse error: $e');
      }
    }
  }

  void _handleCommand(Map<String, dynamic> json) {
    final cmd = json['cmd'] as String?;
    if (cmd == null) return;

    final ble = BleManager.instance;
    if (ble.currentState != BleConnectionState.connected) {
      debugPrint('[MqttGateway] BLE not connected, ignoring cmd: $cmd');
      return;
    }

    switch (cmd) {
      case 'ack_alarm':
        final eventId = json['event_id'] as int?;
        if (eventId != null) {
          ble.sendAckAlarm(eventId);
        }
        break;
      case 'sync_time':
        ble.sendSyncTime();
        break;
      case 'request_report':
        ble.sendRequestReport();
        break;
      case 'manual_measure':
        final action = json['action'] as String? ?? 'start';
        final duration = json['duration'] as int? ?? 15;
        ble.sendManualMeasure(
          start: action == 'start',
          durationSec: duration,
        );
        break;
      default:
        debugPrint('[MqttGateway] unknown cmd: $cmd');
    }
  }

  void _publishOnlineStatus(bool online) {
    final payload = jsonEncode({
      'device_id': MqttConfig.deviceId,
      'online': online,
    });
    _publish(MqttConfig.statusTopic, payload, MqttQos.atLeastOnce, true);
  }

  int? _publish(String topic, String payload, MqttQos qos, bool retain) {
    if (_client == null) {
      debugPrint('[MqttGateway] _publish: client is null');
      return null;
    }

    try {
      final builder = MqttClientPayloadBuilder();
      builder.addString(payload);
      final msgId = _client!.publishMessage(topic, qos, builder.payload!, retain: retain);
      return msgId;
    } catch (e) {
      debugPrint('[MqttGateway] _publish to $topic failed: $e');
      return null;
    }
  }

  void _scheduleReconnect() {
    if (_intentionalDisconnect) return;

    _reconnectTimer?.cancel();
    debugPrint('[MqttGateway] reconnect in ${_reconnectDelay}s...');
    _reconnectTimer = Timer(Duration(seconds: _reconnectDelay), () async {
      _reconnectDelay = (_reconnectDelay * 2).clamp(2, 30);
      await connect();
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
