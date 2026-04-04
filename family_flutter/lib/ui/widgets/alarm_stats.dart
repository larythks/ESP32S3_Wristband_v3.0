import 'package:fl_chart/fl_chart.dart';
import 'package:flutter/material.dart';

import '../../data/models.dart';
import '../../theme/app_colors.dart';

/// 报警统计组件
///
/// 包含饼图（按报警类型分布）和柱状图（近 7 天每日报警次数）
class AlarmStats extends StatelessWidget {
  final Map<FamilyAlarmType, int> typeStats;
  final Map<DateTime, int> dailyCounts;

  const AlarmStats({
    super.key,
    required this.typeStats,
    required this.dailyCounts,
  });

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        // 饼图 - 按类型分布
        if (typeStats.isNotEmpty) ...[
          const Text(
            '报警类型分布',
            style: TextStyle(
              fontSize: 15,
              fontWeight: FontWeight.w600,
              color: AppColors.textPrimary,
            ),
          ),
          const SizedBox(height: 8),
          Semantics(
            label: '报警类型分布图表',
            excludeSemantics: true,
            child: _AlarmTypePieChart(typeStats: typeStats),
          ),
          const SizedBox(height: 16),
        ],

        // 柱状图 - 近 7 天趋势
        if (dailyCounts.isNotEmpty) ...[
          const Text(
            '近 7 天报警趋势',
            style: TextStyle(
              fontSize: 15,
              fontWeight: FontWeight.w600,
              color: AppColors.textPrimary,
            ),
          ),
          const SizedBox(height: 8),
          Semantics(
            label: '近7天报警趋势图表',
            excludeSemantics: true,
            child: _DailyAlarmBarChart(dailyCounts: dailyCounts),
          ),
        ],
      ],
    );
  }
}

/// 饼图：报警类型分布
class _AlarmTypePieChart extends StatelessWidget {
  final Map<FamilyAlarmType, int> typeStats;

  const _AlarmTypePieChart({required this.typeStats});

  @override
  Widget build(BuildContext context) {
    final total = typeStats.values.fold(0, (a, b) => a + b);

    return Card(
      color: Colors.white,
      elevation: 1.5,
      shadowColor: Colors.black.withValues(alpha: 0.08),
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(16),
      ),
      child: Padding(
        padding: const EdgeInsets.all(20),
        child: Row(
          children: [
            // 饼图
            SizedBox(
              width: 120,
              height: 120,
              child: PieChart(
                PieChartData(
                  sections: typeStats.entries.map((entry) {
                    final percentage =
                        total > 0 ? (entry.value / total * 100) : 0.0;
                    return PieChartSectionData(
                      value: entry.value.toDouble(),
                      color: entry.key.color,
                      radius: 24,
                      title: percentage >= 10
                          ? '${percentage.toStringAsFixed(0)}%'
                          : '',
                      titleStyle: const TextStyle(
                        fontSize: 10,
                        color: Colors.white,
                        fontWeight: FontWeight.bold,
                      ),
                    );
                  }).toList(),
                  sectionsSpace: 2,
                  centerSpaceRadius: 30,
                ),
              ),
            ),
            const SizedBox(width: 16),

            // 图例
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: typeStats.entries.map((entry) {
                  return Padding(
                    padding: const EdgeInsets.only(bottom: 4),
                    child: Row(
                      children: [
                        Container(
                          width: 10,
                          height: 10,
                          decoration: BoxDecoration(
                            color: entry.key.color,
                            shape: BoxShape.circle,
                          ),
                        ),
                        const SizedBox(width: 6),
                        Expanded(
                          child: Text(
                            entry.key.displayName,
                            style: const TextStyle(
                              fontSize: 12,
                              color: AppColors.textSecondary,
                            ),
                            overflow: TextOverflow.ellipsis,
                          ),
                        ),
                        Text(
                          '${entry.value}',
                          style: const TextStyle(
                            fontSize: 12,
                            fontWeight: FontWeight.w600,
                            color: AppColors.textPrimary,
                          ),
                        ),
                      ],
                    ),
                  );
                }).toList(),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

/// 柱状图：近 7 天每日报警次数
class _DailyAlarmBarChart extends StatelessWidget {
  final Map<DateTime, int> dailyCounts;

  const _DailyAlarmBarChart({required this.dailyCounts});

  @override
  Widget build(BuildContext context) {
    // 按日期排序
    final sortedEntries = dailyCounts.entries.toList()
      ..sort((a, b) => a.key.compareTo(b.key));

    final maxCount = sortedEntries
        .map((e) => e.value)
        .fold(0, (a, b) => a > b ? a : b);

    return Card(
      color: Colors.white,
      elevation: 1.5,
      shadowColor: Colors.black.withValues(alpha: 0.08),
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(16),
      ),
      child: Padding(
        padding: const EdgeInsets.fromLTRB(20, 20, 20, 8),
        child: SizedBox(
          height: 160,
          child: BarChart(
            BarChartData(
              maxY: (maxCount + 2).toDouble(),
              barGroups: sortedEntries.asMap().entries.map((mapEntry) {
                final index = mapEntry.key;
                final entry = mapEntry.value;
                return BarChartGroupData(
                  x: index,
                  barRods: [
                    BarChartRodData(
                      toY: entry.value.toDouble(),
                      color: entry.value > 0
                          ? AppColors.abnormal
                          : AppColors.offline.withValues(alpha: 0.3),
                      width: 20,
                      borderRadius: const BorderRadius.only(
                        topLeft: Radius.circular(4),
                        topRight: Radius.circular(4),
                      ),
                    ),
                  ],
                );
              }).toList(),
              titlesData: FlTitlesData(
                topTitles: const AxisTitles(
                  sideTitles: SideTitles(showTitles: false),
                ),
                rightTitles: const AxisTitles(
                  sideTitles: SideTitles(showTitles: false),
                ),
                leftTitles: AxisTitles(
                  sideTitles: SideTitles(
                    showTitles: true,
                    reservedSize: 28,
                    interval: maxCount > 4 ? (maxCount / 4).ceilToDouble() : 1,
                    getTitlesWidget: (value, meta) {
                      if (value % 1 != 0) return const SizedBox.shrink();
                      return Text(
                        '${value.toInt()}',
                        style: const TextStyle(
                          fontSize: 10,
                          color: AppColors.textSecondary,
                        ),
                      );
                    },
                  ),
                ),
                bottomTitles: AxisTitles(
                  sideTitles: SideTitles(
                    showTitles: true,
                    reservedSize: 24,
                    getTitlesWidget: (value, meta) {
                      final index = value.toInt();
                      if (index < 0 || index >= sortedEntries.length) {
                        return const SizedBox.shrink();
                      }
                      final dt = sortedEntries[index].key;
                      return SideTitleWidget(
                        meta: meta,
                        child: Text(
                          '${dt.month}/${dt.day}',
                          style: const TextStyle(
                            fontSize: 10,
                            color: AppColors.textSecondary,
                          ),
                        ),
                      );
                    },
                  ),
                ),
              ),
              gridData: FlGridData(
                show: true,
                drawVerticalLine: false,
                horizontalInterval:
                    maxCount > 4 ? (maxCount / 4).ceilToDouble() : 1,
                getDrawingHorizontalLine: (value) => FlLine(
                  color: AppColors.textSecondary.withValues(alpha: 0.15),
                  strokeWidth: 1,
                  dashArray: [5, 3],
                ),
              ),
              borderData: FlBorderData(show: false),
              barTouchData: BarTouchData(
                touchTooltipData: BarTouchTooltipData(
                  getTooltipItem: (group, groupIndex, rod, rodIndex) {
                    final dt = sortedEntries[groupIndex].key;
                    return BarTooltipItem(
                      '${dt.month}/${dt.day}\n${rod.toY.toInt()} 次',
                      const TextStyle(
                        color: Colors.white,
                        fontSize: 12,
                        fontWeight: FontWeight.w600,
                      ),
                    );
                  },
                ),
              ),
            ),
          ),
        ),
      ),
    );
  }
}
