import 'package:flutter/material.dart';

import '../../data/models.dart';
import '../../theme/app_colors.dart';
import '../../utils/formatters.dart';

/// 每日健康摘要卡片
///
/// 显示一天的平均值、最大最小值、步数总计、报警次数
class SummaryCard extends StatelessWidget {
  final DailySummary summary;

  const SummaryCard({super.key, required this.summary});

  @override
  Widget build(BuildContext context) {
    return Semantics(
      label: '${formatDateChinese(summary.date)} 健康摘要',
      child: Card(
        color: Colors.white,
        elevation: 1.5,
        shadowColor: Colors.black.withValues(alpha: 0.08),
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(16),
        ),
        child: Padding(
          padding: const EdgeInsets.all(20),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              // 日期标题
              Row(
                children: [
                  Container(
                    width: 28,
                    height: 28,
                    decoration: BoxDecoration(
                      color: AppColors.primary.withValues(alpha: 0.12),
                      shape: BoxShape.circle,
                    ),
                    child: const Icon(
                      Icons.calendar_today,
                      size: 14,
                      color: AppColors.primary,
                    ),
                  ),
                  const SizedBox(width: 8),
                  Text(
                    formatDateChinese(summary.date),
                    style: const TextStyle(
                      fontSize: 15,
                      fontWeight: FontWeight.w600,
                      color: AppColors.textPrimary,
                    ),
                  ),
                  const Spacer(),
                  if (summary.alarmCount > 0)
                    Container(
                      padding: const EdgeInsets.symmetric(
                        horizontal: 8,
                        vertical: 2,
                      ),
                      decoration: BoxDecoration(
                        color: AppColors.abnormal,
                        borderRadius: BorderRadius.circular(10),
                      ),
                      child: Text(
                        '${summary.alarmCount} 次报警',
                        style: const TextStyle(
                          fontSize: 11,
                          color: Colors.white,
                          fontWeight: FontWeight.w600,
                        ),
                      ),
                    ),
                ],
              ),
              const Divider(height: 20),

              // 数据行
              _SummaryRow(
                icon: Icons.favorite,
                label: '心率',
                avg: '${summary.avgHeartRate} bpm',
                range:
                    '${summary.minHeartRate} - ${summary.maxHeartRate} bpm',
                iconColor: AppColors.metricHeartRate,
              ),
              const SizedBox(height: 8),
              _SummaryRow(
                icon: Icons.water_drop,
                label: '血氧',
                avg: '${summary.avgSpo2}%',
                range: '最低 ${summary.minSpo2}%',
                iconColor: AppColors.metricSpo2,
              ),
              const SizedBox(height: 8),
              _SummaryRow(
                icon: Icons.thermostat,
                label: '温度',
                avg: '${summary.avgTemp.toStringAsFixed(1)}°C',
                range:
                    '${summary.minTemp.toStringAsFixed(1)} - '
                    '${summary.maxTemp.toStringAsFixed(1)}°C',
                iconColor: AppColors.metricTemp,
              ),
              const SizedBox(height: 8),
              _SummaryRow(
                icon: Icons.directions_walk,
                label: '步数',
                avg: '${summary.maxSteps} 步',
                range: '当日最高',
                iconColor: AppColors.metricSteps,
              ),
            ],
          ),
        ),
      ),
    );
  }
}

/// 摘要数据行
class _SummaryRow extends StatelessWidget {
  final IconData icon;
  final String label;
  final String avg;
  final String range;
  final Color iconColor;

  const _SummaryRow({
    required this.icon,
    required this.label,
    required this.avg,
    required this.range,
    required this.iconColor,
  });

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        Container(
          width: 24,
          height: 24,
          decoration: BoxDecoration(
            color: iconColor.withValues(alpha: 0.12),
            shape: BoxShape.circle,
          ),
          child: Icon(icon, size: 14, color: iconColor),
        ),
        const SizedBox(width: 8),
        SizedBox(
          width: 40,
          child: Text(
            label,
            style: const TextStyle(
              fontSize: 13,
              color: AppColors.textSecondary,
            ),
          ),
        ),
        const SizedBox(width: 8),
        Expanded(
          child: Text(
            avg,
            style: const TextStyle(
              fontSize: 14,
              fontWeight: FontWeight.w600,
              color: AppColors.textPrimary,
            ),
          ),
        ),
        Text(
          range,
          style: const TextStyle(
            fontSize: 12,
            color: AppColors.textSecondary,
          ),
        ),
      ],
    );
  }
}
