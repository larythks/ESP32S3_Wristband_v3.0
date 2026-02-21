import 'package:flutter/material.dart';
import 'package:intl/intl.dart';
import '../../data/models.dart';

/// 报警弹窗组件
class AlarmDialog extends StatelessWidget {
  final AlarmData alarm;
  final VoidCallback onAck;
  final VoidCallback onDismiss;

  const AlarmDialog({
    super.key,
    required this.alarm,
    required this.onAck,
    required this.onDismiss,
  });

  @override
  Widget build(BuildContext context) {
    final alarmColor = alarm.alarmType.color;
    const errorColor = Color(0xFFE63946);

    return AlertDialog(
      backgroundColor: Colors.white,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(16),
      ),
      title: Column(
        children: [
          // 圆形图标背景
          Container(
            width: 80,
            height: 80,
            decoration: BoxDecoration(
              color: alarmColor.withValues(alpha: 0.1),
              shape: BoxShape.circle,
            ),
            child: Icon(
              alarm.alarmType.icon,
              size: 40,
              color: alarmColor,
            ),
          ),
          const SizedBox(height: 16),
          Text(
            alarm.alarmType.displayName,
            style: const TextStyle(
              fontSize: 24,
              fontWeight: FontWeight.w700,
            ),
          ),
        ],
      ),
      content: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          Text(
            '触发值: ${alarm.alarmType.formatValue(alarm.triggerValue)}',
            style: Theme.of(context).textTheme.bodyLarge,
          ),
          const SizedBox(height: 8),
          Text(
            '时间: ${DateFormat('yyyy-MM-dd HH:mm:ss').format(alarm.timestamp)}',
            style: Theme.of(context).textTheme.bodySmall?.copyWith(
                  color: const Color(0xFF6C757D),
                ),
          ),
        ],
      ),
      actions: [
        Row(
          children: [
            Expanded(
              child: OutlinedButton(
                onPressed: onDismiss,
                child: const Text('忽略'),
              ),
            ),
            const SizedBox(width: 12),
            Expanded(
              child: FilledButton(
                style: FilledButton.styleFrom(
                  backgroundColor: errorColor,
                ),
                onPressed: onAck,
                child: const Text('确认'),
              ),
            ),
          ],
        ),
      ],
    );
  }
}
