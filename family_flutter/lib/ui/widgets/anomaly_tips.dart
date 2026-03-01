import 'package:flutter/material.dart';

import '../../data/models.dart';
import '../../providers/health_analysis_provider.dart';
import '../../theme/app_colors.dart';
import '../../utils/formatters.dart';

/// 异常提示列表组件
///
/// 显示超阈值数据点的中文描述列表
class AnomalyTipsWidget extends StatelessWidget {
  final List<AnomalyTip> tips;

  const AnomalyTipsWidget({super.key, required this.tips});

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);

    if (tips.isEmpty) {
      return Card(
        elevation: 0,
        color: AppColors.normal.withValues(alpha: 0.08),
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(12),
        ),
        child: Padding(
          padding: const EdgeInsets.all(16),
          child: Row(
            children: [
              const Icon(
                Icons.check_circle,
                color: AppColors.normal,
                size: 24,
              ),
              const SizedBox(width: 12),
              Expanded(
                child: Text(
                  '当前时段内未发现异常数据',
                  style: theme.textTheme.bodyMedium?.copyWith(
                    color: AppColors.normal,
                    fontWeight: FontWeight.w500,
                  ),
                ),
              ),
            ],
          ),
        ),
      );
    }

    // 限制显示最新的 10 条
    final displayTips = tips.length > 10 ? tips.sublist(0, 10) : tips;

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        // 标题
        Row(
          children: [
            const Icon(
              Icons.warning_amber_rounded,
              color: AppColors.abnormal,
              size: 20,
            ),
            const SizedBox(width: 8),
            Text(
              '异常提示 (${tips.length})',
              style: theme.textTheme.titleMedium?.copyWith(
                fontWeight: FontWeight.w600,
              ),
            ),
          ],
        ),
        const SizedBox(height: 8),

        // 提示列表
        ...displayTips.map((tip) => Semantics(
          label: tip.description,
          child: _AnomalyTipTile(tip: tip),
        )),

        // 更多提示
        if (tips.length > 10)
          Padding(
            padding: const EdgeInsets.only(top: 4),
            child: Text(
              '还有 ${tips.length - 10} 条异常记录',
              style: theme.textTheme.bodySmall?.copyWith(
                color: theme.colorScheme.onSurfaceVariant,
              ),
            ),
          ),
      ],
    );
  }
}

/// 单条异常提示
class _AnomalyTipTile extends StatelessWidget {
  final AnomalyTip tip;

  const _AnomalyTipTile({required this.tip});

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final color = tip.relatedAlarmType?.color ?? AppColors.abnormal;

    return Card(
      elevation: 0,
      margin: const EdgeInsets.only(bottom: 6),
      color: color.withValues(alpha: 0.06),
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(10),
      ),
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
        child: Row(
          children: [
            // 指标图标
            Icon(
              tip.relatedAlarmType?.icon ?? Icons.warning,
              size: 18,
              color: color,
            ),
            const SizedBox(width: 10),

            // 描述
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    tip.description,
                    style: theme.textTheme.bodySmall?.copyWith(
                      fontWeight: FontWeight.w500,
                    ),
                  ),
                  const SizedBox(height: 2),
                  Text(
                    formatDateTime(tip.time),
                    style: theme.textTheme.bodySmall?.copyWith(
                      fontSize: 11,
                      color: theme.colorScheme.onSurfaceVariant,
                    ),
                  ),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }
}
