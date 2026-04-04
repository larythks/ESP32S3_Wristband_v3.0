import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../../data/models.dart';
import '../../providers/device_provider.dart';
import '../../theme/app_colors.dart';
import '../../utils/formatters.dart';

/// 报警记录卡片组件（与 mobile_flutter 风格统一）
///
/// 显示报警类型图标、中文描述、时间、ACK 状态及按钮
class AlarmCard extends StatelessWidget {
  final AlarmRecord alarm;

  const AlarmCard({super.key, required this.alarm});

  @override
  Widget build(BuildContext context) {
    final typeColor = alarm.alarmType.color;

    return Semantics(
      label: '${alarm.alarmType.displayName}, 触发值: ${alarm.alarmType.formatValue(alarm.value)}, ${formatDateTimeMinute(alarm.timestamp)}, ${alarm.acknowledged ? "已确认" : "未确认"}',
      child: Card(
        color: Colors.white,
        elevation: 1.5,
        shadowColor: Colors.black.withValues(alpha: 0.08),
        margin: const EdgeInsets.only(bottom: 8),
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(16),
          side: !alarm.acknowledged
              ? BorderSide(
                  color: typeColor.withValues(alpha: 0.4),
                  width: 1,
                )
              : BorderSide.none,
        ),
        child: Padding(
          padding: const EdgeInsets.all(16),
          child: Row(
            children: [
              // 圆形类型图标（与 mobile 统一）
              Container(
                width: 44,
                height: 44,
                decoration: BoxDecoration(
                  color: typeColor.withValues(alpha: 0.12),
                  shape: BoxShape.circle,
                ),
                child: Icon(
                  alarm.alarmType.icon,
                  color: typeColor,
                  size: 22,
                ),
              ),
              const SizedBox(width: 12),

              // 信息区域
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      alarm.alarmType.displayName,
                      style: const TextStyle(
                        fontSize: 15,
                        fontWeight: FontWeight.w600,
                        color: AppColors.textPrimary,
                      ),
                    ),
                    const SizedBox(height: 2),
                    Text(
                      '触发值: ${alarm.alarmType.formatValue(alarm.value)}',
                      style: const TextStyle(
                        fontSize: 13,
                        color: AppColors.textSecondary,
                      ),
                    ),
                    const SizedBox(height: 2),
                    Text(
                      formatDateTimeMinute(alarm.timestamp),
                      style: const TextStyle(
                        fontSize: 11,
                        color: AppColors.textSecondary,
                      ),
                    ),
                  ],
                ),
              ),

              // ACK 状态 / 按钮
              _buildAckWidget(context, typeColor),
            ],
          ),
        ),
      ),
    );
  }

  Widget _buildAckWidget(BuildContext context, Color typeColor) {
    if (alarm.acknowledged) {
      return Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          const Icon(
            Icons.check_circle,
            color: AppColors.normal,
            size: 22,
          ),
          const SizedBox(height: 2),
          const Text(
            '已确认',
            style: TextStyle(
              fontSize: 10,
              fontWeight: FontWeight.w500,
              color: AppColors.normal,
            ),
          ),
        ],
      );
    }

    return FilledButton.tonal(
      onPressed: () async {
        final provider = context.read<DeviceProvider>();
        final success = await provider.ackAlarm(alarm.eventId);
        if (context.mounted) {
          ScaffoldMessenger.of(context).showSnackBar(
            SnackBar(
              content: Text(success ? '报警已确认' : '确认失败，请重试'),
              duration: const Duration(seconds: 2),
            ),
          );
        }
      },
      style: FilledButton.styleFrom(
        backgroundColor: typeColor.withValues(alpha: 0.12),
        foregroundColor: typeColor,
        padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 6),
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(10),
        ),
        tapTargetSize: MaterialTapTargetSize.padded,
      ),
      child: const Text('确认', style: TextStyle(fontSize: 12)),
    );
  }
}
