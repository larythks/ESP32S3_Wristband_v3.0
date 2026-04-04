import 'package:fl_chart/fl_chart.dart';
import 'package:flutter/material.dart';

import '../../data/models.dart';
import '../../providers/health_analysis_provider.dart';
import '../../theme/app_colors.dart';

/// 趋势指标枚举
enum TrendMetric {
  heartRate('心率', 'bpm', Icons.favorite),
  spo2('血氧', '%', Icons.water_drop),
  temp('温度', '°C', Icons.thermostat),
  steps('步数', '步', Icons.directions_walk);

  final String label;
  final String unit;
  final IconData icon;
  const TrendMetric(this.label, this.unit, this.icon);
}

/// 趋势折线图组件
///
/// 基于 fl_chart 的 LineChart，支持阈值虚线和异常标记
class TrendChart extends StatelessWidget {
  final List<TelemetryRecord> data;
  final TrendMetric metric;
  final TrendRange range;

  const TrendChart({
    super.key,
    required this.data,
    required this.metric,
    required this.range,
  });

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);

    if (data.isEmpty) {
      return SizedBox(
        height: 200,
        child: Center(
          child: Column(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              Icon(
                Icons.show_chart,
                size: 48,
                color: AppColors.textSecondary.withValues(alpha: 0.4),
              ),
              const SizedBox(height: 8),
              const Text(
                '暂无数据',
                style: TextStyle(
                  fontSize: 14,
                  color: AppColors.textSecondary,
                ),
              ),
            ],
          ),
        ),
      );
    }

    final spots = _buildSpots();
    final thresholds = _getThresholds();
    final minY = _getMinY(spots, thresholds);
    final maxY = _getMaxY(spots, thresholds);

    return Semantics(
      label: '${metric.label}趋势图表，共${data.length}个数据点',
      excludeSemantics: true,
      child: SizedBox(
        height: 220,
        child: Padding(
          padding: const EdgeInsets.only(right: 16, top: 8),
          child: LineChart(
            LineChartData(
              gridData: FlGridData(
                show: true,
                drawVerticalLine: false,
                horizontalInterval: _getYInterval(minY, maxY),
                getDrawingHorizontalLine: (value) => FlLine(
                  color: AppColors.textSecondary.withValues(alpha: 0.15),
                  strokeWidth: 1,
                  dashArray: [5, 3],
                ),
              ),
              titlesData: FlTitlesData(
                topTitles: const AxisTitles(
                  sideTitles: SideTitles(showTitles: false),
                ),
                rightTitles: const AxisTitles(
                  sideTitles: SideTitles(showTitles: false),
                ),
                bottomTitles: AxisTitles(
                  sideTitles: SideTitles(
                    showTitles: true,
                    reservedSize: 28,
                    interval: _getXInterval(),
                    getTitlesWidget: (value, meta) =>
                        _buildXTitle(value, meta, theme),
                  ),
                ),
                leftTitles: AxisTitles(
                  sideTitles: SideTitles(
                    showTitles: true,
                    reservedSize: 42,
                    interval: _getYInterval(minY, maxY),
                    getTitlesWidget: (value, meta) =>
                        _buildYTitle(value, meta, theme),
                  ),
                ),
              ),
              borderData: FlBorderData(show: false),
              minY: minY,
              maxY: maxY,
              lineBarsData: [
                // 主数据线
                LineChartBarData(
                  spots: spots,
                  isCurved: true,
                  curveSmoothness: 0.3,
                  preventCurveOverShooting: true,
                  color: AppColors.chartLine,
                  barWidth: 2,
                  dotData: FlDotData(
                    show: true,
                    getDotPainter: (spot, _, barData, index) {
                      // 异常数据点用红色
                      final isAbnormal = _isAbnormal(spot.y);
                      return FlDotCirclePainter(
                        radius: isAbnormal ? 4 : 2,
                        color: isAbnormal
                            ? AppColors.chartThreshold
                            : AppColors.chartLine,
                        strokeWidth: 0,
                      );
                    },
                  ),
                  belowBarData: BarAreaData(
                    show: true,
                    color: AppColors.chartFill,
                  ),
                ),
              ],
              // 阈值虚线
              extraLinesData: ExtraLinesData(
                horizontalLines: thresholds
                    .map(
                      (t) => HorizontalLine(
                        y: t,
                        color: AppColors.chartThreshold.withValues(alpha: 0.5),
                        strokeWidth: 1,
                        dashArray: [6, 4],
                      ),
                    )
                    .toList(),
              ),
              lineTouchData: LineTouchData(
                touchTooltipData: LineTouchTooltipData(
                  getTooltipItems: (touchedSpots) {
                    return touchedSpots.map((spot) {
                      final dt = DateTime.fromMillisecondsSinceEpoch(
                        spot.x.toInt(),
                      );
                      final timeStr =
                          '${dt.hour.toString().padLeft(2, '0')}'
                          ':${dt.minute.toString().padLeft(2, '0')}';
                      return LineTooltipItem(
                        '$timeStr\n${_formatValue(spot.y)} ${metric.unit}',
                        TextStyle(
                          color: _isAbnormal(spot.y)
                              ? AppColors.chartThreshold
                              : Colors.white,
                          fontSize: 12,
                          fontWeight: FontWeight.w600,
                        ),
                      );
                    }).toList();
                  },
                ),
              ),
            ),
          ),
        ),
      ),
    );
  }

  /// 构建数据点
  List<FlSpot> _buildSpots() {
    return data.map((r) {
      final x = r.timestamp.millisecondsSinceEpoch.toDouble();
      final y = _getValue(r);
      return FlSpot(x, y);
    }).toList();
  }

  /// 从记录中提取指标值
  double _getValue(TelemetryRecord r) {
    switch (metric) {
      case TrendMetric.heartRate:
        return r.heartRate.toDouble();
      case TrendMetric.spo2:
        return r.spo2.toDouble();
      case TrendMetric.temp:
        return r.temp;
      case TrendMetric.steps:
        return r.steps.toDouble();
    }
  }

  /// 获取阈值列表
  List<double> _getThresholds() {
    switch (metric) {
      case TrendMetric.heartRate:
        return [
          HealthThresholds.heartRateLow.toDouble(),
          HealthThresholds.heartRateHigh.toDouble(),
        ];
      case TrendMetric.spo2:
        return [HealthThresholds.spo2Low.toDouble()];
      case TrendMetric.temp:
        return [HealthThresholds.tempLow, HealthThresholds.tempHigh];
      case TrendMetric.steps:
        return [];
    }
  }

  /// 判断数据点是否异常
  bool _isAbnormal(double value) {
    switch (metric) {
      case TrendMetric.heartRate:
        return value > HealthThresholds.heartRateHigh ||
            (value > 0 && value < HealthThresholds.heartRateLow);
      case TrendMetric.spo2:
        return value > 0 && value < HealthThresholds.spo2Low;
      case TrendMetric.temp:
        return value > HealthThresholds.tempHigh ||
            (value > 0 && value < HealthThresholds.tempLow);
      case TrendMetric.steps:
        return false;
    }
  }

  /// 格式化数值显示
  String _formatValue(double value) {
    switch (metric) {
      case TrendMetric.heartRate:
      case TrendMetric.spo2:
      case TrendMetric.steps:
        return value.toInt().toString();
      case TrendMetric.temp:
        return value.toStringAsFixed(1);
    }
  }

  /// X 轴刻度间隔（毫秒）
  double _getXInterval() {
    switch (range) {
      case TrendRange.hours24:
        return const Duration(hours: 2).inMilliseconds.toDouble();
      case TrendRange.days7:
        return const Duration(days: 1).inMilliseconds.toDouble();
    }
  }

  /// Y 轴刻度间隔
  double _getYInterval(double minY, double maxY) {
    final range = maxY - minY;
    if (range <= 0) return 1;
    if (range <= 10) return 2;
    if (range <= 50) return 10;
    if (range <= 100) return 20;
    return (range / 5).roundToDouble();
  }

  /// 计算 Y 轴最小值（保证阈值线与边界之间有足够留白）
  double _getMinY(List<FlSpot> spots, List<double> thresholds) {
    double minVal = spots
        .map((s) => s.y)
        .reduce((a, b) => a < b ? a : b);
    for (final t in thresholds) {
      if (t < minVal) minVal = t;
    }
    // 使用绝对值留白：取数据范围的 15% 与固定值 5 中的较大者
    final dataRange = spots.map((s) => s.y).reduce((a, b) => a > b ? a : b) -
        spots.map((s) => s.y).reduce((a, b) => a < b ? a : b);
    final padding = (dataRange * 0.15).clamp(5.0, double.infinity);
    return (minVal - padding).floorToDouble();
  }

  /// 计算 Y 轴最大值（保证阈值线与边界之间有足够留白）
  double _getMaxY(List<FlSpot> spots, List<double> thresholds) {
    double maxVal = spots
        .map((s) => s.y)
        .reduce((a, b) => a > b ? a : b);
    for (final t in thresholds) {
      if (t > maxVal) maxVal = t;
    }
    final dataRange = spots.map((s) => s.y).reduce((a, b) => a > b ? a : b) -
        spots.map((s) => s.y).reduce((a, b) => a < b ? a : b);
    final padding = (dataRange * 0.15).clamp(5.0, double.infinity);
    return (maxVal + padding).ceilToDouble();
  }

  /// 构建 X 轴标题
  Widget _buildXTitle(double value, TitleMeta meta, ThemeData theme) {
    final dt = DateTime.fromMillisecondsSinceEpoch(value.toInt());
    final String text;
    switch (range) {
      case TrendRange.hours24:
        text = '${dt.hour.toString().padLeft(2, '0')}:00';
      case TrendRange.days7:
        text = '${dt.month}/${dt.day}';
    }

    return SideTitleWidget(
      meta: meta,
      child: Text(
        text,
        style: const TextStyle(
          fontSize: 10,
          color: AppColors.textSecondary,
        ),
      ),
    );
  }

  /// 构建 Y 轴标题
  Widget _buildYTitle(double value, TitleMeta meta, ThemeData theme) {
    return SideTitleWidget(
      meta: meta,
      child: Text(
        _formatValue(value),
        style: const TextStyle(
          fontSize: 10,
          color: AppColors.textSecondary,
        ),
      ),
    );
  }
}
