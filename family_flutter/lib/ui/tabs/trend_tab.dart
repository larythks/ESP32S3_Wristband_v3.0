import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../../providers/device_provider.dart';
import '../../providers/health_analysis_provider.dart';
import '../../theme/app_colors.dart';
import '../widgets/animated_list_item.dart';
import '../widgets/summary_card.dart';
import '../widgets/trend_chart.dart';

/// 趋势 Tab
///
/// 时间范围切换（24h/7天）+ 指标切换 + 折线图 + 每日摘要
class TrendTab extends StatefulWidget {
  const TrendTab({super.key});

  @override
  State<TrendTab> createState() => _TrendTabState();
}

class _TrendTabState extends State<TrendTab> {
  TrendRange _range = TrendRange.hours24;
  TrendMetric _metric = TrendMetric.heartRate;

  @override
  void initState() {
    super.initState();
    // 初始化加载数据
    WidgetsBinding.instance.addPostFrameCallback((_) => _loadData());
  }

  Future<void> _loadData() async {
    final deviceId = context.read<DeviceProvider>().deviceId;
    if (deviceId == null) return;

    final analysisProvider = context.read<HealthAnalysisProvider>();
    await Future.wait([
      analysisProvider.loadTrendData(deviceId, _range),
      analysisProvider.loadWeekSummaries(deviceId),
    ]);
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final statusBarHeight = MediaQuery.of(context).padding.top;

    return Consumer<HealthAnalysisProvider>(
      builder: (context, provider, _) {
        return RefreshIndicator(
          onRefresh: _loadData,
          child: ListView(
            padding: EdgeInsets.fromLTRB(16, statusBarHeight + 16, 16, 16),
            children: [
              // 时间范围切换
              _buildRangeSelector(theme),
              const SizedBox(height: 12),

              // 指标切换
              _buildMetricSelector(theme),
              const SizedBox(height: 16),

              // 折线图（与 mobile 趋势图卡片风格统一）
              Card(
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
                      Text(
                        '${_metric.label}趋势 (${_metric.unit})',
                        style: const TextStyle(
                          fontSize: 16,
                          fontWeight: FontWeight.w600,
                          color: AppColors.textPrimary,
                        ),
                      ),
                      const SizedBox(height: 12),
                      TrendChart(
                        data: provider.trendData,
                        metric: _metric,
                        range: _range,
                      ),
                    ],
                  ),
                ),
              ),
              const SizedBox(height: 16),

              // 每日摘要（今天）
              if (provider.todaySummary != null) ...[
                const Text(
                  '今日摘要',
                  style: TextStyle(
                    fontSize: 16,
                    fontWeight: FontWeight.w600,
                    color: AppColors.textPrimary,
                  ),
                ),
                const SizedBox(height: 8),
                AnimatedListItem(
                  index: 0,
                  child: SummaryCard(summary: provider.todaySummary!),
                ),
                const SizedBox(height: 16),
              ],

              // 7 天摘要
              if (provider.weekSummaries.isNotEmpty) ...[
                const Text(
                  '近 7 天摘要',
                  style: TextStyle(
                    fontSize: 16,
                    fontWeight: FontWeight.w600,
                    color: AppColors.textPrimary,
                  ),
                ),
                const SizedBox(height: 8),
                ...provider.weekSummaries.reversed.toList().asMap().entries.map(
                  (entry) => Padding(
                    padding: const EdgeInsets.only(bottom: 8),
                    child: AnimatedListItem(
                      index: entry.key,
                      child: SummaryCard(summary: entry.value),
                    ),
                  ),
                ),
              ],
            ],
          ),
        );
      },
    );
  }

  /// 时间范围选择器
  Widget _buildRangeSelector(ThemeData theme) {
    return SegmentedButton<TrendRange>(
      segments: const [
        ButtonSegment(
          value: TrendRange.hours24,
          label: Text('24 小时'),
          icon: Icon(Icons.schedule, size: 18),
        ),
        ButtonSegment(
          value: TrendRange.days7,
          label: Text('7 天'),
          icon: Icon(Icons.date_range, size: 18),
        ),
      ],
      selected: {_range},
      onSelectionChanged: (selected) {
        setState(() => _range = selected.first);
        _loadData();
      },
    );
  }

  /// 指标选择器
  Widget _buildMetricSelector(ThemeData theme) {
    return SingleChildScrollView(
      scrollDirection: Axis.horizontal,
      child: Row(
        children: TrendMetric.values.map((m) {
          final isSelected = m == _metric;
          return Padding(
            padding: const EdgeInsets.only(right: 8),
            child: FilterChip(
              selected: isSelected,
              label: Text(m.label),
              avatar: Icon(m.icon, size: 16),
              onSelected: (_) {
                setState(() => _metric = m);
              },
            ),
          );
        }).toList(),
      ),
    );
  }
}
