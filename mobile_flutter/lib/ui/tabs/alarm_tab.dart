import 'package:flutter/material.dart';
import 'package:intl/intl.dart';
import 'package:provider/provider.dart';
import '../../data/ble_provider.dart';
import '../../data/models.dart';

/// 报警记录页面
class AlarmTab extends StatelessWidget {
  const AlarmTab({super.key});

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('报警记录'),
      ),
      body: Consumer<BleProvider>(
        builder: (context, ble, child) {
          final alarms = ble.alarmHistory;

          if (alarms.isEmpty) {
            return Center(
              child: Column(
                mainAxisSize: MainAxisSize.min,
                children: [
                  Icon(
                    Icons.notifications_off_outlined,
                    size: 64,
                    color: Colors.grey.shade400,
                  ),
                  const SizedBox(height: 16),
                  Text(
                    '暂无报警记录',
                    style: TextStyle(
                      fontSize: 16,
                      color: Colors.grey.shade500,
                    ),
                  ),
                ],
              ),
            );
          }

          // 按时间倒序排列（newest first）
          final sorted = List<AlarmData>.from(alarms)
            ..sort((a, b) => b.timestamp.compareTo(a.timestamp));

          return ListView.builder(
            padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
            itemCount: sorted.length,
            itemBuilder: (context, index) {
              final alarm = sorted[index];
              return _AlarmCard(alarm: alarm);
            },
          );
        },
      ),
    );
  }
}

class _AlarmCard extends StatelessWidget {
  final AlarmData alarm;

  const _AlarmCard({required this.alarm});

  static const _errorColor = Color(0xFFE63946);

  @override
  Widget build(BuildContext context) {
    final alarmColor = alarm.alarmType.color;

    return Card(
      margin: const EdgeInsets.only(bottom: 12),
      elevation: 1,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(16),
      ),
      clipBehavior: Clip.antiAlias,
      child: Container(
        decoration: alarm.isAcked
            ? null
            : const BoxDecoration(
                border: Border(
                  left: BorderSide(
                    color: _errorColor,
                    width: 4,
                  ),
                ),
              ),
        padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
        child: Row(
          children: [
            // 左侧图标
            Container(
              width: 44,
              height: 44,
              decoration: BoxDecoration(
                color: alarmColor.withValues(alpha: 0.12),
                shape: BoxShape.circle,
              ),
              child: Icon(
                alarm.alarmType.icon,
                size: 22,
                color: alarmColor,
              ),
            ),
            const SizedBox(width: 12),
            // 中间信息
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    alarm.alarmType.displayName,
                    style: Theme.of(context).textTheme.titleMedium?.copyWith(
                          fontWeight: FontWeight.bold,
                        ),
                  ),
                  const SizedBox(height: 4),
                  Text(
                    alarm.alarmType.formatValue(alarm.triggerValue),
                    style: Theme.of(context).textTheme.bodySmall?.copyWith(
                          color: const Color(0xFF6C757D),
                        ),
                  ),
                ],
              ),
            ),
            // 右侧时间和状态
            Column(
              crossAxisAlignment: CrossAxisAlignment.end,
              children: [
                Text(
                  DateFormat('HH:mm').format(alarm.timestamp),
                  style: Theme.of(context).textTheme.bodySmall?.copyWith(
                        color: const Color(0xFF6C757D),
                      ),
                ),
                const SizedBox(height: 4),
                if (alarm.isAcked)
                  Row(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      Icon(
                        Icons.check_circle,
                        size: 14,
                        color: Colors.green.shade600,
                      ),
                      const SizedBox(width: 4),
                      Text(
                        '已确认',
                        style: TextStyle(
                          fontSize: 12,
                          color: Colors.green.shade600,
                        ),
                      ),
                    ],
                  )
                else
                  Column(
                    crossAxisAlignment: CrossAxisAlignment.end,
                    children: [
                      const Text(
                        '未确认',
                        style: TextStyle(
                          fontSize: 12,
                          color: _errorColor,
                        ),
                      ),
                      SizedBox(
                        height: 28,
                        child: TextButton(
                          style: TextButton.styleFrom(
                            padding: const EdgeInsets.symmetric(horizontal: 8),
                            minimumSize: Size.zero,
                            tapTargetSize: MaterialTapTargetSize.shrinkWrap,
                          ),
                          onPressed: () async {
                            final ok = await context
                                .read<BleProvider>()
                                .ackAlarm(alarm.eventId);
                            if (!ok && context.mounted) {
                              ScaffoldMessenger.of(context).showSnackBar(
                                const SnackBar(
                                  content: Text('确认失败：未找到对应的报警记录'),
                                  duration: Duration(seconds: 2),
                                ),
                              );
                            }
                          },
                          child: const Text(
                            '确认',
                            style: TextStyle(
                              fontSize: 12,
                              color: _errorColor,
                            ),
                          ),
                        ),
                      ),
                    ],
                  ),
              ],
            ),
          ],
        ),
      ),
    );
  }
}
