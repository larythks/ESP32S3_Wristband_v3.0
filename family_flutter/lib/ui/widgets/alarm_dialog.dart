import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../../data/models.dart';
import '../../providers/device_provider.dart';
import '../../theme/app_colors.dart';
import '../../utils/formatters.dart';

/// 报警弹窗组件（与 mobile_flutter 风格统一）
///
/// 全屏居中对话框，80px 大图标 + 报警详情 + 操作按钮
class AlarmDialog extends StatelessWidget {
  final AlarmRecord alarm;

  const AlarmDialog({super.key, required this.alarm});

  /// 显示报警弹窗的便捷方法
  static Future<void> show(BuildContext context, AlarmRecord alarm) {
    return showDialog(
      context: context,
      barrierDismissible: false,
      builder: (_) => AlarmDialog(alarm: alarm),
    );
  }

  @override
  Widget build(BuildContext context) {
    final typeColor = alarm.alarmType.color;

    return AlertDialog(
      backgroundColor: Colors.white,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(16),
      ),
      title: Column(
        children: [
          // 80px 圆形图标（与 mobile 统一）
          Container(
            width: 80,
            height: 80,
            decoration: BoxDecoration(
              color: typeColor.withValues(alpha: 0.1),
              shape: BoxShape.circle,
            ),
            child: Icon(
              alarm.alarmType.icon,
              size: 40,
              color: typeColor,
            ),
          ),
          const SizedBox(height: 16),
          Text(
            alarm.alarmType.displayName,
            style: const TextStyle(
              fontSize: 24,
              fontWeight: FontWeight.w700,
              color: AppColors.textPrimary,
            ),
          ),
        ],
      ),
      content: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          Text(
            '触发值: ${alarm.alarmType.formatValue(alarm.value)}',
            style: const TextStyle(
              fontSize: 16,
              color: AppColors.textPrimary,
            ),
          ),
          const SizedBox(height: 8),
          Text(
            '时间: ${formatFullDateTime(alarm.timestamp)}',
            style: const TextStyle(
              fontSize: 13,
              color: AppColors.textSecondary,
            ),
          ),
          const SizedBox(height: 8),
          if (alarm.acknowledged)
            Container(
              padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 4),
              decoration: BoxDecoration(
                color: AppColors.normal.withValues(alpha: 0.12),
                borderRadius: BorderRadius.circular(10),
              ),
              child: const Row(
                mainAxisSize: MainAxisSize.min,
                children: [
                  Icon(Icons.check_circle, size: 16, color: AppColors.normal),
                  SizedBox(width: 4),
                  Text(
                    '已确认',
                    style: TextStyle(
                      fontSize: 13,
                      fontWeight: FontWeight.w500,
                      color: AppColors.normal,
                    ),
                  ),
                ],
              ),
            ),
        ],
      ),
      actions: [
        Row(
          children: [
            // 关闭按钮
            Expanded(
              child: OutlinedButton(
                onPressed: () => Navigator.of(context).pop(),
                style: OutlinedButton.styleFrom(
                  padding: const EdgeInsets.symmetric(vertical: 12),
                  shape: RoundedRectangleBorder(
                    borderRadius: BorderRadius.circular(12),
                  ),
                ),
                child: const Text('关闭'),
              ),
            ),
            // ACK 确认按钮（未确认时显示）
            if (!alarm.acknowledged) ...[
              const SizedBox(width: 12),
              Expanded(
                child: FilledButton(
                  onPressed: () async {
                    final provider = context.read<DeviceProvider>();
                    final success =
                        await provider.ackAlarm(alarm.eventId);
                    if (context.mounted) {
                      ScaffoldMessenger.of(context).showSnackBar(
                        SnackBar(
                          content: Text(
                            success ? '报警已确认' : '确认失败，请重试',
                          ),
                        ),
                      );
                      Navigator.of(context).pop();
                    }
                  },
                  style: FilledButton.styleFrom(
                    backgroundColor: AppColors.abnormal,
                    padding: const EdgeInsets.symmetric(vertical: 12),
                    shape: RoundedRectangleBorder(
                      borderRadius: BorderRadius.circular(12),
                    ),
                  ),
                  child: const Text('确认 (ACK)'),
                ),
              ),
            ],
          ],
        ),
      ],
    );
  }
}
