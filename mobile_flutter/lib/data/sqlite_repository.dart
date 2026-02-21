import 'models.dart';
import 'data_repository.dart';
import 'database_helper.dart';

class SqliteDataRepository implements DataRepository {
  final DatabaseHelper _dbHelper = DatabaseHelper.instance;

  @override
  Future<void> saveTelemetry(TelemetryData data) =>
      _dbHelper.insertTelemetry(data);

  @override
  Future<List<TelemetryData>> getTelemetryHistory({int limit = 360}) =>
      _dbHelper.queryTelemetry(limit: limit);

  @override
  Future<void> saveAlarm(AlarmData data) => _dbHelper.insertAlarm(data);

  @override
  Future<List<AlarmData>> getAlarmHistory({int limit = 100}) =>
      _dbHelper.queryAlarms(limit: limit);

  @override
  Future<void> updateAlarmAcked(int eventId) =>
      _dbHelper.updateAlarmAcked(eventId, true);

  Future<void> cleanup() => _dbHelper.cleanup24h();

  @override
  Future<void> dispose() => _dbHelper.close();
}
