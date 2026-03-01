import 'dart:async';

import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../../data/models.dart';
import '../../providers/device_provider.dart';
import '../../theme/app_colors.dart';
import '../../utils/formatters.dart';
import '../widgets/alarm_dialog.dart';
import '../widgets/animated_list_item.dart';
import '../widgets/health_card.dart';

/// 仪表盘 Tab
///
/// 布局：设备状态栏 + 四格健康数据卡片 + 最近报警记录
class DashboardTab extends StatelessWidget {
  const DashboardTab({super.key});

  @override
  Widget build(BuildContext context) {
    final statusBarHeight = MediaQuery.of(context).padding.top;
    return Consumer<DeviceProvider>(
      builder: (context, provider, _) {
        return RefreshIndicator(
          onRefresh: () => provider.refreshHistory(),
          child: ListView(
            padding: EdgeInsets.fromLTRB(16, statusBarHeight + 16, 16, 16),
            children: [
              // 设备状态栏
              _DeviceStatusBar(provider: provider),
              const SizedBox(height: 16),

              // 四格健康数据
              _HealthDataGrid(provider: provider),
              const SizedBox(height: 16),

              // 手动测量按钮
              _ManualMeasureButton(provider: provider),
              const SizedBox(height: 16),

              // 最近报警记录
              _RecentAlarms(provider: provider),
            ],
          ),
        );
      },
    );
  }
}

/// 设备状态栏（与 mobile Chip 风格统一）
class _DeviceStatusBar extends StatelessWidget {
  final DeviceProvider provider;

  const _DeviceStatusBar({required this.provider});

  @override
  Widget build(BuildContext context) {
    // 确定状态
    final String statusText;
    final Color statusColor;
    final IconData statusIcon;

    if (!provider.mqttConnected) {
      statusText = '未连接';
      statusColor = AppColors.offline;
      statusIcon = Icons.cloud_off;
    } else if (provider.deviceOnline) {
      statusText = '设备在线';
      statusColor = AppColors.normal;
      statusIcon = Icons.cloud_done;
    } else {
      statusText = '设备离线';
      statusColor = AppColors.abnormal;
      statusIcon = Icons.cloud_off;
    }

    return Semantics(
      label: '设备状态: $statusText',
      child: Card(
        color: Colors.white,
        elevation: 1.5,
        shadowColor: Colors.black.withValues(alpha: 0.08),
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(16),
        ),
        child: Padding(
          padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 14),
          child: Row(
            children: [
              // 圆形状态图标（与 mobile HealthCard 图标风格统一）
              Container(
                width: 36,
                height: 36,
                decoration: BoxDecoration(
                  color: statusColor.withValues(alpha: 0.12),
                  shape: BoxShape.circle,
                ),
                child: Icon(statusIcon, color: statusColor, size: 20),
              ),
              const SizedBox(width: 12),
              Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    statusText,
                    style: TextStyle(
                      fontSize: 15,
                      fontWeight: FontWeight.w600,
                      color: statusColor,
                    ),
                  ),
                  if (provider.lastSeen != null)
                    Text(
                      '更新于 ${formatShortTime(provider.lastSeen!)}',
                      style: TextStyle(
                        fontSize: 12,
                        color: AppColors.textSecondary,
                      ),
                    ),
                ],
              ),
              const Spacer(),
              // 右侧小圆点（与 mobile 连接状态指示风格统一）
              Icon(
                Icons.circle,
                size: 10,
                color: statusColor,
              ),
            ],
          ),
        ),
      ),
    );
  }
}

/// 四格健康数据
class _HealthDataGrid extends StatelessWidget {
  final DeviceProvider provider;

  const _HealthDataGrid({required this.provider});

  @override
  Widget build(BuildContext context) {
    final telemetry = provider.latestTelemetry;
    final isOffline = !provider.deviceOnline;

    final cards = [
      // 温度（与 mobile 布局顺序一致）
      HealthCard(
        icon: Icons.thermostat,
        label: '温度',
        value: telemetry != null
            ? telemetry.temp.toStringAsFixed(1)
            : '--',
        unit: '°C',
        color: AppColors.metricTemp,
        status: _getStatus(
          isOffline: isOffline,
          value: telemetry?.temp,
          highThreshold: HealthThresholds.tempHigh,
          lowThreshold: HealthThresholds.tempLow,
        ),
      ),

      // 心率
      HealthCard(
        icon: Icons.favorite,
        label: '心率',
        value: telemetry != null ? '${telemetry.heartRate}' : '--',
        unit: 'bpm',
        color: AppColors.metricHeartRate,
        status: _getStatus(
          isOffline: isOffline,
          value: telemetry?.heartRate.toDouble(),
          highThreshold: HealthThresholds.heartRateHigh.toDouble(),
          lowThreshold: HealthThresholds.heartRateLow.toDouble(),
        ),
      ),

      // 血氧
      HealthCard(
        icon: Icons.water_drop,
        label: '血氧',
        value: telemetry != null ? '${telemetry.spo2}' : '--',
        unit: '%',
        color: AppColors.metricSpo2,
        status: _getStatus(
          isOffline: isOffline,
          value: telemetry?.spo2.toDouble(),
          lowThreshold: HealthThresholds.spo2Low.toDouble(),
        ),
      ),

      // 步数
      HealthCard(
        icon: Icons.directions_walk,
        label: '步数',
        value: telemetry != null ? formatSteps(telemetry.steps) : '--',
        unit: '步',
        color: AppColors.metricSteps,
        status: isOffline
            ? HealthCardStatus.offline
            : HealthCardStatus.normal,
      ),
    ];

    return GridView.count(
      crossAxisCount: 2,
      shrinkWrap: true,
      physics: const NeverScrollableScrollPhysics(),
      mainAxisSpacing: 12,
      crossAxisSpacing: 12,
      childAspectRatio: 1.0,
      children: [
        for (int i = 0; i < cards.length; i++)
          AnimatedListItem(index: i, child: cards[i]),
      ],
    );
  }

  /// 根据阈值判断状态
  HealthCardStatus _getStatus({
    required bool isOffline,
    double? value,
    double? highThreshold,
    double? lowThreshold,
  }) {
    if (isOffline || value == null) return HealthCardStatus.offline;

    if (highThreshold != null && value > highThreshold) {
      return HealthCardStatus.abnormal;
    }
    if (lowThreshold != null && value > 0 && value < lowThreshold) {
      return HealthCardStatus.abnormal;
    }
    return HealthCardStatus.normal;
  }
}

/// 手动测量按钮（含 15 秒倒计时，参考 mobile_flutter）
class _ManualMeasureButton extends StatefulWidget {
  final DeviceProvider provider;

  const _ManualMeasureButton({required this.provider});

  @override
  State<_ManualMeasureButton> createState() => _ManualMeasureButtonState();
}

class _ManualMeasureButtonState extends State<_ManualMeasureButton> {
  static const int _measureDuration = 15;
  bool _isMeasuring = false;
  int _countdown = 0;
  Timer? _timer;

  @override
  void dispose() {
    _timer?.cancel();
    super.dispose();
  }

  void _startMeasure() {
    if (_isMeasuring) return;

    final provider = widget.provider;
    if (!provider.isBound || !provider.mqttConnected) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('设备未连接，无法发起测量')),
      );
      return;
    }

    provider.manualMeasure(start: true, durationSec: _measureDuration);

    setState(() {
      _isMeasuring = true;
      _countdown = _measureDuration;
    });

    _timer = Timer.periodic(const Duration(seconds: 1), (timer) {
      setState(() {
        _countdown--;
        if (_countdown <= 0) {
          _isMeasuring = false;
          timer.cancel();
        }
      });
    });
  }

  @override
  Widget build(BuildContext context) {
    final enabled = widget.provider.isBound &&
        widget.provider.mqttConnected &&
        !_isMeasuring;

    return FilledButton.tonal(
      onPressed: enabled ? _startMeasure : null,
      style: FilledButton.styleFrom(
        minimumSize: const Size(double.infinity, 52),
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(16),
        ),
      ),
      child: _isMeasuring
          ? Row(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                const SizedBox(
                  width: 18,
                  height: 18,
                  child: CircularProgressIndicator(strokeWidth: 2),
                ),
                const SizedBox(width: 10),
                Text(
                  '测量中 (${_countdown}s)',
                  style: const TextStyle(fontSize: 15),
                ),
              ],
            )
          : const Row(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                Icon(Icons.play_arrow, size: 20),
                SizedBox(width: 6),
                Text('手动测量', style: TextStyle(fontSize: 15)),
              ],
            ),
    );
  }
}

/// 最近报警记录（最新 3 条）
class _RecentAlarms extends StatelessWidget {
  final DeviceProvider provider;

  const _RecentAlarms({required this.provider});

  @override
  Widget build(BuildContext context) {
    final alarms = provider.alarmHistory.take(3).toList();

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Row(
          children: [
            Container(
              width: 28,
              height: 28,
              decoration: BoxDecoration(
                color: AppColors.abnormal.withValues(alpha: 0.12),
                shape: BoxShape.circle,
              ),
              child: const Icon(
                Icons.notifications_active,
                size: 16,
                color: AppColors.abnormal,
              ),
            ),
            const SizedBox(width: 8),
            Text(
              '最近报警记录',
              style: const TextStyle(
                fontSize: 16,
                fontWeight: FontWeight.w600,
                color: AppColors.textPrimary,
              ),
            ),
            const Spacer(),
            if (provider.unackedAlarmCount > 0)
              Container(
                padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 2),
                decoration: BoxDecoration(
                  color: AppColors.abnormal,
                  borderRadius: BorderRadius.circular(10),
                ),
                child: Text(
                  '${provider.unackedAlarmCount}',
                  style: const TextStyle(
                    fontSize: 11,
                    fontWeight: FontWeight.w600,
                    color: Colors.white,
                  ),
                ),
              ),
          ],
        ),
        const SizedBox(height: 12),

        if (alarms.isEmpty)
          Card(
            color: Colors.white,
            elevation: 1.5,
            shadowColor: Colors.black.withValues(alpha: 0.08),
            shape: RoundedRectangleBorder(
              borderRadius: BorderRadius.circular(16),
            ),
            child: Padding(
              padding: const EdgeInsets.all(24),
              child: Column(
                children: [
                  Container(
                    width: 48,
                    height: 48,
                    decoration: BoxDecoration(
                      color: AppColors.normal.withValues(alpha: 0.12),
                      shape: BoxShape.circle,
                    ),
                    child: const Icon(
                      Icons.check_circle_outline,
                      size: 28,
                      color: AppColors.normal,
                    ),
                  ),
                  const SizedBox(height: 12),
                  const Text(
                    '暂无报警记录',
                    style: TextStyle(
                      fontSize: 15,
                      fontWeight: FontWeight.w600,
                      color: AppColors.textPrimary,
                    ),
                  ),
                  const SizedBox(height: 4),
                  const Text(
                    '设备运行正常',
                    style: TextStyle(
                      fontSize: 12,
                      color: AppColors.textSecondary,
                    ),
                  ),
                ],
              ),
            ),
          )
        else
          ...alarms.asMap().entries.map(
            (entry) => AnimatedListItem(
              index: entry.key,
              child: _AlarmTile(alarm: entry.value),
            ),
          ),
      ],
    );
  }
}

/// 单条报警记录（与 mobile alarm_tab 风格统一）
class _AlarmTile extends StatelessWidget {
  final AlarmRecord alarm;

  const _AlarmTile({required this.alarm});

  @override
  Widget build(BuildContext context) {
    final typeColor = alarm.alarmType.color;

    return Semantics(
      label: '${alarm.alarmType.displayName}, ${formatDateTime(alarm.timestamp)}, ${alarm.acknowledged ? "已确认" : "未确认"}',
      child: Card(
        color: Colors.white,
        elevation: 1.5,
        shadowColor: Colors.black.withValues(alpha: 0.08),
        margin: const EdgeInsets.only(bottom: 8),
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(16),
          side: !alarm.acknowledged
              ? BorderSide(color: typeColor.withValues(alpha: 0.4), width: 1)
              : BorderSide.none,
        ),
        child: InkWell(
          borderRadius: BorderRadius.circular(16),
          onTap: () => AlarmDialog.show(context, alarm),
          child: Padding(
            padding: const EdgeInsets.all(16),
            child: Row(
              children: [
                // 圆形图标（44px，与 mobile 统一）
                Container(
                  width: 44,
                  height: 44,
                  decoration: BoxDecoration(
                    color: typeColor.withValues(alpha: 0.12),
                    shape: BoxShape.circle,
                  ),
                  child: Icon(alarm.alarmType.icon, color: typeColor, size: 22),
                ),
                const SizedBox(width: 12),

                // 信息
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
                        formatDateTime(alarm.timestamp),
                        style: const TextStyle(
                          fontSize: 12,
                          color: AppColors.textSecondary,
                        ),
                      ),
                    ],
                  ),
                ),

                // 确认状态
                if (alarm.acknowledged)
                  const Icon(Icons.check_circle, color: AppColors.normal, size: 20)
                else
                  Container(
                    padding: const EdgeInsets.symmetric(
                      horizontal: 8, vertical: 3,
                    ),
                    decoration: BoxDecoration(
                      color: typeColor.withValues(alpha: 0.12),
                      borderRadius: BorderRadius.circular(8),
                    ),
                    child: Text(
                      '未确认',
                      style: TextStyle(
                        fontSize: 11,
                        fontWeight: FontWeight.w500,
                        color: typeColor,
                      ),
                    ),
                  ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}
