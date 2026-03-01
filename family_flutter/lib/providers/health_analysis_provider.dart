import 'package:flutter/foundation.dart';

import '../data/family_repository.dart';
import '../data/models.dart';

/// 趋势范围枚举
enum TrendRange { hours24, days7 }

/// 异常提示数据类
class AnomalyTip {
  final DateTime time;
  final String metric;
  final double value;
  final String description;
  final FamilyAlarmType? relatedAlarmType;

  const AnomalyTip({
    required this.time,
    required this.metric,
    required this.value,
    required this.description,
    this.relatedAlarmType,
  });
}

/// 健康分析 Provider（ChangeNotifier）
///
/// 负责：趋势数据加载、每日摘要、报警统计、异常检测
class HealthAnalysisProvider extends ChangeNotifier {
  final FamilyRepository _repo = FamilyRepository.instance;

  // ---- 趋势数据 ----
  List<TelemetryRecord> _trendData = [];
  List<TelemetryRecord> get trendData => List.unmodifiable(_trendData);

  // ---- 每日摘要 ----
  DailySummary? _todaySummary;
  DailySummary? get todaySummary => _todaySummary;

  List<DailySummary> _weekSummaries = [];
  List<DailySummary> get weekSummaries => List.unmodifiable(_weekSummaries);

  // ---- 报警统计 ----
  Map<FamilyAlarmType, int> _alarmTypeStats = {};
  Map<FamilyAlarmType, int> get alarmTypeStats =>
      Map.unmodifiable(_alarmTypeStats);

  Map<DateTime, int> _dailyAlarmCounts = {};
  Map<DateTime, int> get dailyAlarmCounts =>
      Map.unmodifiable(_dailyAlarmCounts);

  // ---- 异常提示 ----
  List<AnomalyTip> _anomalyTips = [];
  List<AnomalyTip> get anomalyTips => List.unmodifiable(_anomalyTips);

  // ---- 加载状态 ----
  bool _isLoadingTrend = false;
  bool get isLoadingTrend => _isLoadingTrend;

  bool _isLoadingStats = false;
  bool get isLoadingStats => _isLoadingStats;

  /// 加载趋势数据
  Future<void> loadTrendData(String deviceId, TrendRange range) async {
    _isLoadingTrend = true;
    notifyListeners();
    try {
      final now = DateTime.now();
      final DateTime start;
      switch (range) {
        case TrendRange.hours24:
          start = now.subtract(const Duration(hours: 24));
        case TrendRange.days7:
          start = now.subtract(const Duration(days: 7));
      }

      _trendData = await _repo.getTelemetryRange(
        deviceId: deviceId,
        start: start,
        end: now,
      );

      // 分析异常
      _analyzeAnomalies();

      debugPrint(
        '[HealthAnalysisProvider] 趋势数据已加载: '
        '${_trendData.length} 条, 范围=$range',
      );
      notifyListeners();
    } catch (e) {
      debugPrint('[HealthAnalysisProvider] loadTrendData 失败: $e');
    } finally {
      _isLoadingTrend = false;
      notifyListeners();
    }
  }

  /// 加载 7 天每日摘要
  Future<void> loadWeekSummaries(String deviceId) async {
    try {
      final today = DateTime.now();
      final startDate = DateTime(today.year, today.month, today.day)
          .subtract(const Duration(days: 6));

      _weekSummaries = await _repo.getDailySummaries(
        deviceId,
        startDate,
        7,
      );

      // 获取今天的摘要
      _todaySummary = await _repo.getDailySummary(
        deviceId,
        DateTime(today.year, today.month, today.day),
      );

      debugPrint(
        '[HealthAnalysisProvider] 周摘要已加载: '
        '${_weekSummaries.length} 天',
      );
      notifyListeners();
    } catch (e) {
      debugPrint('[HealthAnalysisProvider] loadWeekSummaries 失败: $e');
    }
  }

  /// 加载报警统计
  Future<void> loadAlarmStats(String deviceId, {DateTime? since}) async {
    _isLoadingStats = true;
    notifyListeners();
    try {
      _alarmTypeStats = await _repo.getAlarmTypeStats(
        deviceId,
        since: since,
      );

      final startDate = since ??
          DateTime.now().subtract(const Duration(days: 7));
      final normalizedStart = DateTime(
        startDate.year,
        startDate.month,
        startDate.day,
      );
      final days = DateTime.now().difference(normalizedStart).inDays + 1;

      _dailyAlarmCounts = await _repo.getDailyAlarmCounts(
        deviceId,
        normalizedStart,
        days,
      );

      debugPrint(
        '[HealthAnalysisProvider] 报警统计已加载: '
        '类型=${_alarmTypeStats.length}, '
        '天数=${_dailyAlarmCounts.length}',
      );
      notifyListeners();
    } catch (e) {
      debugPrint('[HealthAnalysisProvider] loadAlarmStats 失败: $e');
    } finally {
      _isLoadingStats = false;
      notifyListeners();
    }
  }

  /// 分析异常数据，生成提示
  void _analyzeAnomalies() {
    _anomalyTips = [];

    for (final record in _trendData) {
      // 心率过高
      if (record.heartRate > HealthThresholds.heartRateHigh) {
        _anomalyTips.add(AnomalyTip(
          time: record.timestamp,
          metric: '心率',
          value: record.heartRate.toDouble(),
          description:
              '心率 ${record.heartRate} bpm 超过阈值 ${HealthThresholds.heartRateHigh} bpm',
          relatedAlarmType: FamilyAlarmType.heartRateHigh,
        ));
      }

      // 心率过低
      if (record.heartRate > 0 &&
          record.heartRate < HealthThresholds.heartRateLow) {
        _anomalyTips.add(AnomalyTip(
          time: record.timestamp,
          metric: '心率',
          value: record.heartRate.toDouble(),
          description:
              '心率 ${record.heartRate} bpm 低于阈值 ${HealthThresholds.heartRateLow} bpm',
          relatedAlarmType: FamilyAlarmType.heartRateLow,
        ));
      }

      // 血氧过低
      if (record.spo2 > 0 && record.spo2 < HealthThresholds.spo2Low) {
        _anomalyTips.add(AnomalyTip(
          time: record.timestamp,
          metric: '血氧',
          value: record.spo2.toDouble(),
          description:
              '血氧 ${record.spo2}% 低于阈值 ${HealthThresholds.spo2Low}%',
          relatedAlarmType: FamilyAlarmType.spo2Low,
        ));
      }

      // 温度过高
      if (record.temp > HealthThresholds.tempHigh) {
        _anomalyTips.add(AnomalyTip(
          time: record.timestamp,
          metric: '温度',
          value: record.temp,
          description:
              '温度 ${record.temp.toStringAsFixed(1)}°C 超过阈值 ${HealthThresholds.tempHigh}°C',
          relatedAlarmType: FamilyAlarmType.tempHigh,
        ));
      }

      // 温度过低
      if (record.temp > 0 && record.temp < HealthThresholds.tempLow) {
        _anomalyTips.add(AnomalyTip(
          time: record.timestamp,
          metric: '温度',
          value: record.temp,
          description:
              '温度 ${record.temp.toStringAsFixed(1)}°C 低于阈值 ${HealthThresholds.tempLow}°C',
          relatedAlarmType: FamilyAlarmType.tempLow,
        ));
      }
    }

    debugPrint('[HealthAnalysisProvider] 异常分析完成: ${_anomalyTips.length} 条提示');
  }
}
