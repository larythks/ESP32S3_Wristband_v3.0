import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../../data/models.dart';
import '../../providers/device_provider.dart';
import '../../providers/health_analysis_provider.dart';
import '../../theme/app_colors.dart';
import '../widgets/alarm_card.dart';
import '../widgets/alarm_stats.dart';
import '../widgets/animated_list_item.dart';

/// 报警筛选类型
enum _AlarmFilter {
  all('全部'),
  unacked('未确认'),
  heartRate('心率'),
  spo2('血氧'),
  temp('温度'),
  fall('跌倒'),
  manual('手动');

  final String label;
  const _AlarmFilter(this.label);
}

/// 报警 Tab
///
/// 顶部统计区域 + 筛选 + 报警历史列表（按时间倒序）
class AlarmTab extends StatefulWidget {
  const AlarmTab({super.key});

  @override
  State<AlarmTab> createState() => _AlarmTabState();
}

class _AlarmTabState extends State<AlarmTab> {
  _AlarmFilter _filter = _AlarmFilter.all;
  bool _showStats = false;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addPostFrameCallback((_) => _loadStats());
  }

  Future<void> _loadStats() async {
    final deviceId = context.read<DeviceProvider>().deviceId;
    if (deviceId == null) return;

    await context.read<HealthAnalysisProvider>().loadAlarmStats(deviceId);
  }

  Future<void> _refresh() async {
    await context.read<DeviceProvider>().refreshHistory();
    await _loadStats();
  }

  /// 根据筛选条件过滤报警列表
  List<AlarmRecord> _filterAlarms(List<AlarmRecord> alarms) {
    switch (_filter) {
      case _AlarmFilter.all:
        return alarms;
      case _AlarmFilter.unacked:
        return alarms.where((a) => !a.acknowledged).toList();
      case _AlarmFilter.heartRate:
        return alarms
            .where((a) =>
                a.alarmType == FamilyAlarmType.heartRateHigh ||
                a.alarmType == FamilyAlarmType.heartRateLow)
            .toList();
      case _AlarmFilter.spo2:
        return alarms
            .where((a) => a.alarmType == FamilyAlarmType.spo2Low)
            .toList();
      case _AlarmFilter.temp:
        return alarms
            .where((a) =>
                a.alarmType == FamilyAlarmType.tempHigh ||
                a.alarmType == FamilyAlarmType.tempLow)
            .toList();
      case _AlarmFilter.fall:
        return alarms
            .where((a) => a.alarmType == FamilyAlarmType.fall)
            .toList();
      case _AlarmFilter.manual:
        return alarms
            .where((a) =>
                a.alarmType == FamilyAlarmType.manual ||
                a.alarmType == FamilyAlarmType.callFamily)
            .toList();
    }
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);

    return Consumer2<DeviceProvider, HealthAnalysisProvider>(
      builder: (context, deviceProvider, analysisProvider, _) {
        final filteredAlarms = _filterAlarms(deviceProvider.alarmHistory);

        return RefreshIndicator(
          onRefresh: _refresh,
          child: CustomScrollView(
            slivers: [
              // 顶部统计概览
              SliverToBoxAdapter(
                child: Padding(
                  padding: EdgeInsets.fromLTRB(16, MediaQuery.of(context).padding.top + 16, 16, 0),
                  child: _buildStatsHeader(
                    theme,
                    deviceProvider,
                    analysisProvider,
                  ),
                ),
              ),

              // 展开的统计图表
              SliverToBoxAdapter(
                child: AnimatedCrossFade(
                  firstChild: const SizedBox.shrink(),
                  secondChild: Padding(
                    padding: const EdgeInsets.fromLTRB(16, 12, 16, 0),
                    child: AlarmStats(
                      typeStats: analysisProvider.alarmTypeStats,
                      dailyCounts: analysisProvider.dailyAlarmCounts,
                    ),
                  ),
                  crossFadeState: _showStats ? CrossFadeState.showSecond : CrossFadeState.showFirst,
                  duration: const Duration(milliseconds: 300),
                ),
              ),

              // 筛选条
              SliverToBoxAdapter(
                child: Padding(
                  padding: const EdgeInsets.fromLTRB(16, 12, 16, 8),
                  child: _buildFilterChips(theme),
                ),
              ),

              // 报警列表
              if (filteredAlarms.isEmpty)
                SliverFillRemaining(
                  hasScrollBody: false,
                  child: Center(
                    child: Column(
                      mainAxisAlignment: MainAxisAlignment.center,
                      children: [
                        Container(
                          width: 64,
                          height: 64,
                          decoration: BoxDecoration(
                            color: AppColors.textSecondary.withValues(alpha: 0.08),
                            shape: BoxShape.circle,
                          ),
                          child: const Icon(
                            Icons.notifications_off_outlined,
                            size: 32,
                            color: AppColors.textSecondary,
                          ),
                        ),
                        const SizedBox(height: 16),
                        const Text(
                          '暂无报警记录',
                          style: TextStyle(
                            fontSize: 15,
                            fontWeight: FontWeight.w500,
                            color: AppColors.textSecondary,
                          ),
                        ),
                      ],
                    ),
                  ),
                )
              else
                SliverPadding(
                  padding: const EdgeInsets.symmetric(horizontal: 16),
                  sliver: SliverList(
                    delegate: SliverChildBuilderDelegate(
                      (context, index) {
                        return AnimatedListItem(
                          index: index,
                          child: AlarmCard(alarm: filteredAlarms[index]),
                        );
                      },
                      childCount: filteredAlarms.length,
                    ),
                  ),
                ),

              // 底部间距
              const SliverToBoxAdapter(
                child: SizedBox(height: 16),
              ),
            ],
          ),
        );
      },
    );
  }

  /// 统计概览头部（白色卡片风格，与 mobile 统一）
  Widget _buildStatsHeader(
    ThemeData theme,
    DeviceProvider deviceProvider,
    HealthAnalysisProvider analysisProvider,
  ) {
    final totalAlarms = deviceProvider.alarmHistory.length;
    final unacked = deviceProvider.unackedAlarmCount;

    return Card(
      color: Colors.white,
      elevation: 1.5,
      shadowColor: Colors.black.withValues(alpha: 0.08),
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(16),
      ),
      child: InkWell(
        borderRadius: BorderRadius.circular(16),
        onTap: () => setState(() => _showStats = !_showStats),
        child: Padding(
          padding: const EdgeInsets.all(20),
          child: Row(
            children: [
              // 总数
              _StatBadge(
                label: '总计',
                value: '$totalAlarms',
                color: AppColors.primary,
              ),
              const SizedBox(width: 24),
              // 未确认
              _StatBadge(
                label: '未确认',
                value: '$unacked',
                color: unacked > 0
                    ? AppColors.abnormal
                    : AppColors.normal,
              ),
              const Spacer(),
              // 展开/收起
              Container(
                width: 32,
                height: 32,
                decoration: BoxDecoration(
                  color: AppColors.primary.withValues(alpha: 0.08),
                  shape: BoxShape.circle,
                ),
                child: Icon(
                  _showStats
                      ? Icons.keyboard_arrow_up
                      : Icons.keyboard_arrow_down,
                  color: AppColors.primary,
                  size: 20,
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }

  /// 筛选条
  Widget _buildFilterChips(ThemeData theme) {
    return SingleChildScrollView(
      scrollDirection: Axis.horizontal,
      child: Row(
        children: _AlarmFilter.values.map((f) {
          final isSelected = f == _filter;
          return Padding(
            padding: const EdgeInsets.only(right: 8),
            child: FilterChip(
              selected: isSelected,
              label: Text(f.label),
              materialTapTargetSize: MaterialTapTargetSize.padded,
              onSelected: (_) {
                setState(() => _filter = f);
              },
            ),
          );
        }).toList(),
      ),
    );
  }
}

/// 统计小徽章（与 mobile 文本风格统一）
class _StatBadge extends StatelessWidget {
  final String label;
  final String value;
  final Color color;

  const _StatBadge({
    required this.label,
    required this.value,
    required this.color,
  });

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      mainAxisSize: MainAxisSize.min,
      children: [
        Text(
          label,
          style: const TextStyle(
            fontSize: 12,
            color: AppColors.textSecondary,
          ),
        ),
        const SizedBox(height: 2),
        Text(
          value,
          style: TextStyle(
            fontSize: 28,
            fontWeight: FontWeight.w700,
            color: color,
            height: 1.1,
          ),
        ),
      ],
    );
  }
}
