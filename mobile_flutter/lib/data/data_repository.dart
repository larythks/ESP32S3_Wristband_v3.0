import 'models.dart';

/// 数据仓库抽象接口
/// 当前迭代: 仅定义接口，不实现持久化
/// 后续迭代 4.3/4.4: 实现 CloudDataRepository（MQTT → EMQX Cloud）
abstract class DataRepository {
  /// 保存遥测数据
  Future<void> saveTelemetry(TelemetryData data);

  /// 获取遥测历史
  Future<List<TelemetryData>> getTelemetryHistory({int limit = 30});

  /// 保存报警事件
  Future<void> saveAlarm(AlarmData data);

  /// 获取报警历史
  Future<List<AlarmData>> getAlarmHistory({int limit = 50});

  /// 释放资源
  Future<void> dispose();
}

/// 内存实现（本迭代使用）
/// 作为胶水层，后续可替换为云端实现而不影响 UI 代码
class InMemoryDataRepository implements DataRepository {
  final List<TelemetryData> _telemetryList = [];
  final List<AlarmData> _alarmList = [];

  @override
  Future<void> saveTelemetry(TelemetryData data) async {
    _telemetryList.add(data);
    if (_telemetryList.length > 30) {
      _telemetryList.removeAt(0);
    }
  }

  @override
  Future<List<TelemetryData>> getTelemetryHistory({int limit = 30}) async {
    final start =
        _telemetryList.length > limit ? _telemetryList.length - limit : 0;
    return List.unmodifiable(_telemetryList.sublist(start));
  }

  @override
  Future<void> saveAlarm(AlarmData data) async {
    _alarmList.add(data);
    if (_alarmList.length > 50) {
      _alarmList.removeAt(0);
    }
  }

  @override
  Future<List<AlarmData>> getAlarmHistory({int limit = 50}) async {
    final start = _alarmList.length > limit ? _alarmList.length - limit : 0;
    return List.unmodifiable(_alarmList.sublist(start));
  }

  @override
  Future<void> dispose() async {
    _telemetryList.clear();
    _alarmList.clear();
  }
}
