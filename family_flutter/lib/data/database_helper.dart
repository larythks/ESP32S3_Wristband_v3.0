import 'package:flutter/foundation.dart';
import 'package:sqflite/sqflite.dart';
import 'package:path/path.dart' as p;

/// SQLite 数据库助手（单例）
/// 数据库名: family_careband.db, 版本: 1
class DatabaseHelper {
  static DatabaseHelper? _instance;
  Database? _db;

  DatabaseHelper._();

  static DatabaseHelper get instance {
    _instance ??= DatabaseHelper._();
    return _instance!;
  }

  /// 获取数据库实例（懒初始化）
  Future<Database> get database async {
    _db ??= await _open();
    return _db!;
  }

  Future<Database> _open() async {
    final dbPath = await getDatabasesPath();
    final path = p.join(dbPath, 'family_careband.db');
    debugPrint('[DatabaseHelper] 打开数据库: $path');
    return openDatabase(
      path,
      version: 1,
      onCreate: _onCreate,
    );
  }

  Future<void> _onCreate(Database db, int version) async {
    debugPrint('[DatabaseHelper] 创建数据库表 (version=$version)');

    // 遥测数据表
    await db.execute('''
      CREATE TABLE telemetry (
        id          INTEGER PRIMARY KEY AUTOINCREMENT,
        device_id   TEXT    NOT NULL,
        temp        REAL    NOT NULL,
        heart_rate  INTEGER NOT NULL,
        spo2        INTEGER NOT NULL,
        steps       INTEGER NOT NULL,
        timestamp   INTEGER NOT NULL,
        received_at INTEGER NOT NULL
      )
    ''');
    await db.execute(
      'CREATE INDEX idx_telemetry_device_time '
      'ON telemetry(device_id, timestamp)',
    );

    // 报警记录表
    await db.execute('''
      CREATE TABLE alarm (
        id           INTEGER PRIMARY KEY AUTOINCREMENT,
        device_id    TEXT    NOT NULL,
        event_id     INTEGER NOT NULL,
        alarm_type   INTEGER NOT NULL,
        value        REAL    NOT NULL,
        timestamp    INTEGER NOT NULL,
        received_at  INTEGER NOT NULL,
        acknowledged INTEGER NOT NULL DEFAULT 0,
        acked_at     INTEGER,
        UNIQUE(event_id)
      )
    ''');
    await db.execute(
      'CREATE INDEX idx_alarm_device_time '
      'ON alarm(device_id, timestamp)',
    );

    // 配置表
    await db.execute('''
      CREATE TABLE config (
        key   TEXT PRIMARY KEY,
        value TEXT NOT NULL
      )
    ''');

    debugPrint('[DatabaseHelper] 数据库表创建完成');
  }

  /// 关闭数据库
  Future<void> close() async {
    await _db?.close();
    _db = null;
    debugPrint('[DatabaseHelper] 数据库已关闭');
  }
}
