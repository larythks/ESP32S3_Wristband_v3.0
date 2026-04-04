import 'package:sqflite/sqflite.dart';
import 'package:path/path.dart' as p;
import 'models.dart';

class DatabaseHelper {
  static DatabaseHelper? _instance;
  Database? _db;

  DatabaseHelper._();

  static DatabaseHelper get instance {
    _instance ??= DatabaseHelper._();
    return _instance!;
  }

  Future<Database> get database async {
    _db ??= await _open();
    return _db!;
  }

  Future<Database> _open() async {
    final dbPath = await getDatabasesPath();
    final path = p.join(dbPath, 'careband.db');
    return openDatabase(
      path,
      version: 2,
      onCreate: _onCreate,
      onUpgrade: _onUpgrade,
    );
  }

  Future<void> _onCreate(Database db, int version) async {
    await db.execute('''
      CREATE TABLE telemetry (
        id          INTEGER PRIMARY KEY AUTOINCREMENT,
        temperature REAL NOT NULL,
        heart_rate  INTEGER NOT NULL,
        spo2        INTEGER NOT NULL,
        steps       INTEGER NOT NULL,
        data_valid  INTEGER NOT NULL,
        timestamp   INTEGER NOT NULL
      )
    ''');
    await db.execute(
      'CREATE INDEX idx_telemetry_ts ON telemetry(timestamp)',
    );

    await db.execute('''
      CREATE TABLE alarm (
        id            INTEGER PRIMARY KEY AUTOINCREMENT,
        event_id      INTEGER NOT NULL,
        alarm_type    INTEGER NOT NULL,
        trigger_value REAL NOT NULL,
        timestamp     INTEGER NOT NULL,
        is_acked      INTEGER NOT NULL DEFAULT 0,
        UNIQUE(event_id)
      )
    ''');
    await db.execute(
      'CREATE INDEX idx_alarm_ts ON alarm(timestamp)',
    );
  }

  Future<void> _onUpgrade(Database db, int oldVersion, int newVersion) async {
    if (oldVersion < 2) {
      // v1 -> v2: 移除 battery 列（SQLite 不支持 DROP COLUMN，需重建表）
      await db.execute('ALTER TABLE telemetry RENAME TO telemetry_old');
      await db.execute('''
        CREATE TABLE telemetry (
          id          INTEGER PRIMARY KEY AUTOINCREMENT,
          temperature REAL NOT NULL,
          heart_rate  INTEGER NOT NULL,
          spo2        INTEGER NOT NULL,
          steps       INTEGER NOT NULL,
          data_valid  INTEGER NOT NULL,
          timestamp   INTEGER NOT NULL
        )
      ''');
      await db.execute('''
        INSERT INTO telemetry (id, temperature, heart_rate, spo2, steps, data_valid, timestamp)
        SELECT id, temperature, heart_rate, spo2, steps, data_valid, timestamp FROM telemetry_old
      ''');
      await db.execute('DROP TABLE telemetry_old');
      await db.execute(
        'CREATE INDEX IF NOT EXISTS idx_telemetry_ts ON telemetry(timestamp)',
      );

      await db.execute('ALTER TABLE alarm RENAME TO alarm_old');
      await db.execute('''
        CREATE TABLE alarm (
          id            INTEGER PRIMARY KEY AUTOINCREMENT,
          event_id      INTEGER NOT NULL,
          alarm_type    INTEGER NOT NULL,
          trigger_value REAL NOT NULL,
          timestamp     INTEGER NOT NULL,
          is_acked      INTEGER NOT NULL DEFAULT 0,
          UNIQUE(event_id)
        )
      ''');
      await db.execute('''
        INSERT INTO alarm (id, event_id, alarm_type, trigger_value, timestamp, is_acked)
        SELECT id, event_id, alarm_type, trigger_value, timestamp, is_acked FROM alarm_old
      ''');
      await db.execute('DROP TABLE alarm_old');
      await db.execute(
        'CREATE INDEX IF NOT EXISTS idx_alarm_ts ON alarm(timestamp)',
      );
    }
  }

  // ---- Telemetry CRUD ----

  Future<void> insertTelemetry(TelemetryData data) async {
    final db = await database;
    await db.insert('telemetry', {
      'temperature': data.temperature,
      'heart_rate': data.heartRate,
      'spo2': data.spo2,
      'steps': data.steps,
      'data_valid': data.dataValid,
      'timestamp': data.timestamp.millisecondsSinceEpoch,
    });
  }

  Future<List<TelemetryData>> queryTelemetry({
    int limit = 360,
    DateTime? since,
  }) async {
    final db = await database;
    final where = since != null ? 'timestamp >= ?' : null;
    final whereArgs =
        since != null ? [since.millisecondsSinceEpoch] : null;
    final rows = await db.query(
      'telemetry',
      where: where,
      whereArgs: whereArgs,
      orderBy: 'timestamp ASC',
      limit: limit,
    );
    return rows.map(_rowToTelemetry).toList();
  }

  Future<int> deleteTelemetryBefore(DateTime cutoff) async {
    final db = await database;
    return db.delete(
      'telemetry',
      where: 'timestamp < ?',
      whereArgs: [cutoff.millisecondsSinceEpoch],
    );
  }

  // ---- Alarm CRUD ----

  Future<void> insertAlarm(AlarmData data) async {
    final db = await database;
    await db.insert(
      'alarm',
      {
        'event_id': data.eventId,
        'alarm_type': data.alarmType.code,
        'trigger_value': data.triggerValue,
        'timestamp': data.timestamp.millisecondsSinceEpoch,
        'is_acked': data.isAcked ? 1 : 0,
      },
      conflictAlgorithm: ConflictAlgorithm.ignore,
    );
  }

  Future<List<AlarmData>> queryAlarms({
    int limit = 100,
    DateTime? since,
  }) async {
    final db = await database;
    final where = since != null ? 'timestamp >= ?' : null;
    final whereArgs =
        since != null ? [since.millisecondsSinceEpoch] : null;
    final rows = await db.query(
      'alarm',
      where: where,
      whereArgs: whereArgs,
      orderBy: 'timestamp ASC',
      limit: limit,
    );
    return rows.map(_rowToAlarm).toList();
  }

  Future<void> updateAlarmAcked(int eventId, bool acked) async {
    final db = await database;
    await db.update(
      'alarm',
      {'is_acked': acked ? 1 : 0},
      where: 'event_id = ?',
      whereArgs: [eventId],
    );
  }

  Future<int> deleteAlarmsBefore(DateTime cutoff) async {
    final db = await database;
    return db.delete(
      'alarm',
      where: 'timestamp < ?',
      whereArgs: [cutoff.millisecondsSinceEpoch],
    );
  }

  // ---- Cleanup ----

  Future<void> cleanupBeforeToday() async {
    final now = DateTime.now();
    final todayStart = DateTime(now.year, now.month, now.day);
    await deleteTelemetryBefore(todayStart);
    await deleteAlarmsBefore(todayStart);
  }

  /// 清除所有数据（包括遥测、报警）
  /// 谨慎使用，会清除所有本地存储数据
  Future<Map<String, int>> clearAllData() async {
    final db = await database;

    // 清除所有遥测数据
    final telDeleted = await db.delete('telemetry');

    // 清除所有报警数据（包括已确认和未确认）
    final alarmDeleted = await db.delete('alarm');

    return {
      'telemetry': telDeleted,
      'alarm': alarmDeleted,
    };
  }

  Future<void> close() async {
    await _db?.close();
    _db = null;
  }

  // ---- Row mapping helpers ----

  TelemetryData _rowToTelemetry(Map<String, Object?> row) {
    return TelemetryData(
      temperature: (row['temperature'] as num).toDouble(),
      heartRate: row['heart_rate'] as int,
      spo2: row['spo2'] as int,
      steps: row['steps'] as int,
      dataValid: row['data_valid'] as int,
      timestamp: DateTime.fromMillisecondsSinceEpoch(
        row['timestamp'] as int,
      ),
    );
  }

  AlarmData _rowToAlarm(Map<String, Object?> row) {
    return AlarmData(
      eventId: row['event_id'] as int,
      alarmType: AlarmType.fromCode(row['alarm_type'] as int),
      triggerValue: (row['trigger_value'] as num).toDouble(),
      timestamp: DateTime.fromMillisecondsSinceEpoch(
        row['timestamp'] as int,
      ),
      isAcked: (row['is_acked'] as int) == 1,
    );
  }
}
