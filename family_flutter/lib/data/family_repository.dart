import 'package:flutter/foundation.dart';
import 'package:sqflite/sqflite.dart';

import 'database_helper.dart';
import 'models.dart';

/// 数据仓库（单例）：封装所有 CRUD + 聚合查询
class FamilyRepository {
  FamilyRepository._();
  static final FamilyRepository instance = FamilyRepository._();

  /// 初始化数据库（确保表已创建）
  Future<void> init() async {
    await DatabaseHelper.instance.database;
    debugPrint('[FamilyRepository] 初始化完成');
  }

  // ==================== Telemetry ====================

  /// 保存遥测记录
  Future<void> saveTelemetry(TelemetryRecord record) async {
    try {
      final db = await DatabaseHelper.instance.database;
      await db.insert('telemetry', record.toDbMap());
    } catch (e) {
      debugPrint('[FamilyRepository] saveTelemetry failed: $e');
    }
  }

  /// 查询遥测历史
  Future<List<TelemetryRecord>> getTelemetryHistory({
    required String deviceId,
    int limit = 500,
    DateTime? since,
  }) async {
    try {
      final db = await DatabaseHelper.instance.database;
      final whereParts = <String>['device_id = ?'];
      final whereArgs = <Object>[deviceId];

      if (since != null) {
        whereParts.add('timestamp >= ?');
        whereArgs.add(since.millisecondsSinceEpoch ~/ 1000);
      }

      final rows = await db.query(
        'telemetry',
        where: whereParts.join(' AND '),
        whereArgs: whereArgs,
        orderBy: 'timestamp ASC',
        limit: limit,
      );
      return rows.map(TelemetryRecord.fromDbRow).toList();
    } catch (e) {
      debugPrint('[FamilyRepository] getTelemetryHistory failed: $e');
      return [];
    }
  }

  /// 获取指定日期范围内的遥测数据（用于趋势图）
  Future<List<TelemetryRecord>> getTelemetryRange({
    required String deviceId,
    required DateTime start,
    required DateTime end,
  }) async {
    try {
      final db = await DatabaseHelper.instance.database;
      final startSec = start.millisecondsSinceEpoch ~/ 1000;
      final endSec = end.millisecondsSinceEpoch ~/ 1000;

      final rows = await db.query(
        'telemetry',
        where: 'device_id = ? AND timestamp >= ? AND timestamp <= ?',
        whereArgs: [deviceId, startSec, endSec],
        orderBy: 'timestamp ASC',
      );
      return rows.map(TelemetryRecord.fromDbRow).toList();
    } catch (e) {
      debugPrint('[FamilyRepository] getTelemetryRange failed: $e');
      return [];
    }
  }

  // ==================== Alarm ====================

  /// 保存报警记录（event_id 冲突时忽略）
  Future<void> saveAlarm(AlarmRecord record) async {
    try {
      final db = await DatabaseHelper.instance.database;
      await db.insert(
        'alarm',
        record.toDbMap(),
        conflictAlgorithm: ConflictAlgorithm.ignore,
      );
    } catch (e) {
      debugPrint('[FamilyRepository] saveAlarm failed: $e');
    }
  }

  /// 查询报警历史
  Future<List<AlarmRecord>> getAlarmHistory({
    required String deviceId,
    int limit = 200,
    DateTime? since,
    bool? acknowledged,
  }) async {
    try {
      final db = await DatabaseHelper.instance.database;
      final whereParts = <String>['device_id = ?'];
      final whereArgs = <Object>[deviceId];

      if (since != null) {
        whereParts.add('timestamp >= ?');
        whereArgs.add(since.millisecondsSinceEpoch ~/ 1000);
      }

      if (acknowledged != null) {
        whereParts.add('acknowledged = ?');
        whereArgs.add(acknowledged ? 1 : 0);
      }

      final rows = await db.query(
        'alarm',
        where: whereParts.join(' AND '),
        whereArgs: whereArgs,
        orderBy: 'timestamp DESC',
        limit: limit,
      );
      return rows.map(AlarmRecord.fromDbRow).toList();
    } catch (e) {
      debugPrint('[FamilyRepository] getAlarmHistory failed: $e');
      return [];
    }
  }

  /// 更新报警确认状态
  Future<void> updateAlarmAcked(int eventId, DateTime ackedAt) async {
    try {
      final db = await DatabaseHelper.instance.database;
      await db.update(
        'alarm',
        {
          'acknowledged': 1,
          'acked_at': ackedAt.millisecondsSinceEpoch,
        },
        where: 'event_id = ?',
        whereArgs: [eventId],
      );
    } catch (e) {
      debugPrint('[FamilyRepository] updateAlarmAcked failed: $e');
    }
  }

  /// 获取未确认报警数量
  Future<int> getUnackedAlarmCount(String deviceId) async {
    try {
      final db = await DatabaseHelper.instance.database;
      final result = await db.rawQuery(
        'SELECT COUNT(*) AS cnt FROM alarm '
        'WHERE device_id = ? AND acknowledged = 0',
        [deviceId],
      );
      return Sqflite.firstIntValue(result) ?? 0;
    } catch (e) {
      debugPrint('[FamilyRepository] getUnackedAlarmCount failed: $e');
      return 0;
    }
  }

  // ==================== DailySummary ====================

  /// 获取指定日期的健康摘要
  Future<DailySummary?> getDailySummary(
    String deviceId,
    DateTime date,
  ) async {
    try {
      final db = await DatabaseHelper.instance.database;
      final dayStart = DateTime(date.year, date.month, date.day);
      final dayEnd = dayStart.add(const Duration(days: 1));
      final startSec = dayStart.millisecondsSinceEpoch ~/ 1000;
      final endSec = dayEnd.millisecondsSinceEpoch ~/ 1000;

      // 遥测聚合
      final telRows = await db.rawQuery(
        'SELECT '
        '  AVG(temp) AS avg_temp, '
        '  MIN(temp) AS min_temp, '
        '  MAX(temp) AS max_temp, '
        '  AVG(heart_rate) AS avg_hr, '
        '  MIN(heart_rate) AS min_hr, '
        '  MAX(heart_rate) AS max_hr, '
        '  AVG(spo2) AS avg_spo2, '
        '  MIN(spo2) AS min_spo2, '
        '  MAX(steps) AS max_steps '
        'FROM telemetry '
        'WHERE device_id = ? AND timestamp >= ? AND timestamp < ?',
        [deviceId, startSec, endSec],
      );

      if (telRows.isEmpty || telRows.first['avg_temp'] == null) {
        return null;
      }

      final row = telRows.first;

      // 报警计数
      final alarmRows = await db.rawQuery(
        'SELECT COUNT(*) AS cnt FROM alarm '
        'WHERE device_id = ? AND timestamp >= ? AND timestamp < ?',
        [deviceId, startSec, endSec],
      );
      final alarmCount = Sqflite.firstIntValue(alarmRows) ?? 0;

      return DailySummary(
        date: dayStart,
        avgTemp: (row['avg_temp'] as num).toDouble(),
        minTemp: (row['min_temp'] as num).toDouble(),
        maxTemp: (row['max_temp'] as num).toDouble(),
        avgHeartRate: (row['avg_hr'] as num).round(),
        minHeartRate: (row['min_hr'] as num).toInt(),
        maxHeartRate: (row['max_hr'] as num).toInt(),
        avgSpo2: (row['avg_spo2'] as num).round(),
        minSpo2: (row['min_spo2'] as num).toInt(),
        maxSteps: (row['max_steps'] as num).toInt(),
        alarmCount: alarmCount,
      );
    } catch (e) {
      debugPrint('[FamilyRepository] getDailySummary failed: $e');
      return null;
    }
  }

  /// 获取多天的健康摘要（用于 7 天趋势）
  Future<List<DailySummary>> getDailySummaries(
    String deviceId,
    DateTime startDate,
    int days,
  ) async {
    final summaries = <DailySummary>[];
    for (int i = 0; i < days; i++) {
      final date = startDate.add(Duration(days: i));
      final summary = await getDailySummary(deviceId, date);
      if (summary != null) {
        summaries.add(summary);
      }
    }
    return summaries;
  }

  // ==================== Alarm 统计 ====================

  /// 按类型统计报警数量（用于饼图）
  Future<Map<FamilyAlarmType, int>> getAlarmTypeStats(
    String deviceId, {
    DateTime? since,
  }) async {
    try {
      final db = await DatabaseHelper.instance.database;
      final whereParts = <String>['device_id = ?'];
      final whereArgs = <Object>[deviceId];

      if (since != null) {
        whereParts.add('timestamp >= ?');
        whereArgs.add(since.millisecondsSinceEpoch ~/ 1000);
      }

      final rows = await db.rawQuery(
        'SELECT alarm_type, COUNT(*) AS cnt FROM alarm '
        'WHERE ${whereParts.join(' AND ')} '
        'GROUP BY alarm_type',
        whereArgs,
      );

      final result = <FamilyAlarmType, int>{};
      for (final row in rows) {
        final type = FamilyAlarmType.fromCode(row['alarm_type'] as int);
        result[type] = (row['cnt'] as num).toInt();
      }
      return result;
    } catch (e) {
      debugPrint('[FamilyRepository] getAlarmTypeStats failed: $e');
      return {};
    }
  }

  /// 按天统计报警数量（用于柱状图）
  Future<Map<DateTime, int>> getDailyAlarmCounts(
    String deviceId,
    DateTime startDate,
    int days,
  ) async {
    try {
      final db = await DatabaseHelper.instance.database;
      final result = <DateTime, int>{};

      for (int i = 0; i < days; i++) {
        final date = DateTime(
          startDate.year,
          startDate.month,
          startDate.day,
        ).add(Duration(days: i));
        final nextDate = date.add(const Duration(days: 1));
        final startSec = date.millisecondsSinceEpoch ~/ 1000;
        final endSec = nextDate.millisecondsSinceEpoch ~/ 1000;

        final rows = await db.rawQuery(
          'SELECT COUNT(*) AS cnt FROM alarm '
          'WHERE device_id = ? AND timestamp >= ? AND timestamp < ?',
          [deviceId, startSec, endSec],
        );
        result[date] = Sqflite.firstIntValue(rows) ?? 0;
      }
      return result;
    } catch (e) {
      debugPrint('[FamilyRepository] getDailyAlarmCounts failed: $e');
      return {};
    }
  }

  // ==================== Config ====================

  /// 读取配置
  Future<String?> getConfig(String key) async {
    try {
      final db = await DatabaseHelper.instance.database;
      final rows = await db.query(
        'config',
        where: 'key = ?',
        whereArgs: [key],
      );
      if (rows.isEmpty) return null;
      return rows.first['value'] as String;
    } catch (e) {
      debugPrint('[FamilyRepository] getConfig failed: $e');
      return null;
    }
  }

  /// 写入配置
  Future<void> setConfig(String key, String value) async {
    try {
      final db = await DatabaseHelper.instance.database;
      await db.insert(
        'config',
        {'key': key, 'value': value},
        conflictAlgorithm: ConflictAlgorithm.replace,
      );
    } catch (e) {
      debugPrint('[FamilyRepository] setConfig failed: $e');
    }
  }

  // ==================== Cleanup ====================

  /// 7 天自动清理（已确认报警 + 遥测数据）
  /// 未确认报警不清理（即使超过 7 天）
  Future<void> cleanup() async {
    try {
      final db = await DatabaseHelper.instance.database;
      final cutoffMs = DateTime.now()
          .subtract(const Duration(days: 7))
          .millisecondsSinceEpoch;

      // 清理设备时间超过 7 天的遥测数据
      final telDeleted = await db.delete(
        'telemetry',
        where: 'timestamp < ?',
        whereArgs: [cutoffMs],
      );

      // 清理已确认报警（received_at 超过 7 天 且 已确认）
      final alarmDeleted = await db.delete(
        'alarm',
        where: 'received_at < ? AND acknowledged = 1',
        whereArgs: [cutoffMs],
      );

      // 记录最后清理时间
      await setConfig(
        'last_cleanup',
        DateTime.now().millisecondsSinceEpoch.toString(),
      );

      debugPrint(
        '[FamilyRepository] cleanup 完成: '
        '删除遥测=$telDeleted, 删除已确认报警=$alarmDeleted',
      );
    } catch (e) {
      debugPrint('[FamilyRepository] cleanup failed: $e');
    }
  }

  /// 清除所有数据（包括遥测、报警、配置）
  /// 谨慎使用，会清除所有本地存储数据
  Future<Map<String, int>> clearAllData() async {
    try {
      final db = await DatabaseHelper.instance.database;

      // 清除所有遥测数据
      final telDeleted = await db.delete('telemetry');

      // 清除所有报警数据（包括已确认和未确认）
      final alarmDeleted = await db.delete('alarm');

      // 清除所有配置数据
      final configDeleted = await db.delete('config');

      debugPrint(
        '[FamilyRepository] clearAllData 完成: '
        '删除遥测=$telDeleted, 删除报警=$alarmDeleted, 删除配置=$configDeleted',
      );

      return {
        'telemetry': telDeleted,
        'alarm': alarmDeleted,
        'config': configDeleted,
      };
    } catch (e) {
      debugPrint('[FamilyRepository] clearAllData failed: $e');
      return {'telemetry': 0, 'alarm': 0, 'config': 0};
    }
  }

  // ==================== 释放资源 ====================

  /// 关闭数据库连接
  Future<void> dispose() async {
    await DatabaseHelper.instance.close();
    debugPrint('[FamilyRepository] 已释放资源');
  }
}
