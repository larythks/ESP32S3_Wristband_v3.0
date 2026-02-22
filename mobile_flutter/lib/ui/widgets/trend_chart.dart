import 'package:fl_chart/fl_chart.dart';
import 'package:flutter/material.dart';
import '../../data/models.dart';

/// Metric type for the trend chart.
enum TrendMetric { heartRate, spo2, temperature }

class TrendChart extends StatelessWidget {
  final List<TelemetryData> history;
  final TrendMetric selectedMetric;

  const TrendChart({
    super.key,
    required this.history,
    required this.selectedMetric,
  });

  static const Color _hrColor = Color(0xFFE63946);
  static const Color _spo2Color = Color(0xFF457B9D);
  static const Color _tempColor = Color(0xFFF4A261);

  Color get _lineColor {
    switch (selectedMetric) {
      case TrendMetric.heartRate:
        return _hrColor;
      case TrendMetric.spo2:
        return _spo2Color;
      case TrendMetric.temperature:
        return _tempColor;
    }
  }

  String get _unitLabel {
    switch (selectedMetric) {
      case TrendMetric.heartRate:
        return 'bpm';
      case TrendMetric.spo2:
        return '%';
      case TrendMetric.temperature:
        return '\u00B0C';
    }
  }

  /// Filter history to only include points where the selected metric is valid.
  List<TelemetryData> get _validPoints {
    return history.where((d) {
      switch (selectedMetric) {
        case TrendMetric.heartRate:
          return d.isHrValid;
        case TrendMetric.spo2:
          return d.isSpo2Valid;
        case TrendMetric.temperature:
          return d.isTempValid;
      }
    }).toList();
  }

  double _getValue(TelemetryData d) {
    switch (selectedMetric) {
      case TrendMetric.heartRate:
        return d.heartRate.toDouble();
      case TrendMetric.spo2:
        return d.spo2.toDouble();
      case TrendMetric.temperature:
        return d.temperature;
    }
  }

  @override
  Widget build(BuildContext context) {
    final valid = _validPoints;

    if (valid.length < 2) {
      return const SizedBox(
        height: 200,
        child: Center(
          child: Text(
            '数据采集中，暂无趋势',
            style: TextStyle(
              fontSize: 14,
              color: Color(0xFF6C757D),
            ),
          ),
        ),
      );
    }

    final spots = <FlSpot>[];
    for (int i = 0; i < valid.length; i++) {
      spots.add(FlSpot(i.toDouble(), _getValue(valid[i])));
    }

    // Compute Y bounds with padding
    double minY = spots.map((s) => s.y).reduce((a, b) => a < b ? a : b);
    double maxY = spots.map((s) => s.y).reduce((a, b) => a > b ? a : b);
    final yRange = maxY - minY;
    if (yRange < 1) {
      minY -= 1;
      maxY += 1;
    } else {
      minY -= yRange * 0.1;
      maxY += yRange * 0.1;
    }

    return SizedBox(
      height: 200,
      child: LineChart(
        LineChartData(
          minX: 0,
          maxX: (spots.length - 1).toDouble(),
          minY: minY,
          maxY: maxY,
          gridData: FlGridData(
            show: true,
            drawVerticalLine: false,
            horizontalInterval: _gridInterval(minY, maxY),
            getDrawingHorizontalLine: (value) => FlLine(
              color: const Color(0xFFE0E0E0),
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
                interval: _xLabelInterval(spots.length),
                getTitlesWidget: (value, meta) {
                  final idx = value.toInt();
                  if (idx < 0 || idx >= valid.length) {
                    return const SizedBox.shrink();
                  }
                  final t = valid[idx].timestamp;
                  final label =
                      '${t.hour.toString().padLeft(2, '0')}:${t.minute.toString().padLeft(2, '0')}';
                  return SideTitleWidget(
                    meta: meta,
                    child: Text(
                      label,
                      style: const TextStyle(
                        fontSize: 10,
                        color: Color(0xFF6C757D),
                      ),
                    ),
                  );
                },
              ),
            ),
            leftTitles: AxisTitles(
              sideTitles: SideTitles(
                showTitles: true,
                reservedSize: 42,
                interval: _gridInterval(minY, maxY),
                getTitlesWidget: (value, meta) {
                  // 过滤掉与边界过近的标签，避免重叠
                  final interval = _gridInterval(minY, maxY);
                  final threshold = interval * 0.4;
                  if ((value - minY).abs() < threshold && value != minY) {
                    return const SizedBox.shrink();
                  }
                  if ((value - maxY).abs() < threshold && value != maxY) {
                    return const SizedBox.shrink();
                  }
                  return Text(
                    selectedMetric == TrendMetric.temperature
                        ? value.toStringAsFixed(1)
                        : value.toInt().toString(),
                    style: const TextStyle(
                      fontSize: 10,
                      color: Color(0xFF6C757D),
                    ),
                  );
                },
              ),
            ),
          ),
          borderData: FlBorderData(show: false),
          lineBarsData: [
            LineChartBarData(
              spots: spots,
              isCurved: true,
              color: _lineColor,
              barWidth: 2.5,
              dotData: const FlDotData(show: false),
              belowBarData: BarAreaData(
                show: true,
                color: _lineColor.withValues(alpha: 0.08),
              ),
            ),
          ],
          lineTouchData: LineTouchData(
            handleBuiltInTouches: true,
            touchTooltipData: LineTouchTooltipData(
              getTooltipColor: (_) => const Color(0xFF212529),
              tooltipRoundedRadius: 8,
              getTooltipItems: (touchedSpots) {
                return touchedSpots.map((spot) {
                  final valText = selectedMetric == TrendMetric.temperature
                      ? spot.y.toStringAsFixed(1)
                      : spot.y.toInt().toString();
                  return LineTooltipItem(
                    '$valText $_unitLabel',
                    const TextStyle(
                      color: Colors.white,
                      fontSize: 13,
                      fontWeight: FontWeight.w600,
                    ),
                  );
                }).toList();
              },
            ),
          ),
        ),
      ),
    );
  }

  double _gridInterval(double minY, double maxY) {
    final range = maxY - minY;
    if (range <= 5) return 1;
    if (range <= 20) return 5;
    if (range <= 50) return 10;
    return 20;
  }

  double _xLabelInterval(int count) {
    if (count <= 6) return 1;
    if (count <= 12) return 2;
    if (count <= 20) return 4;
    if (count <= 40) return 8;
    return 10;
  }
}
