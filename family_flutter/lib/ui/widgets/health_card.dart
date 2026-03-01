import 'package:flutter/material.dart';

import '../../theme/app_colors.dart';

/// 健康数据卡片状态
enum HealthCardStatus {
  /// 正常
  normal,
  /// 异常
  abnormal,
  /// 离线
  offline,
}

/// 健康数据卡片组件（与 mobile_flutter 风格统一）
///
/// 布局：圆形彩色图标 + 标题 → 大数值 + 单位 → 可选副标题
class HealthCard extends StatelessWidget {
  final IconData icon;
  final String label;
  final String value;
  final String unit;
  final Color color;
  final HealthCardStatus status;

  const HealthCard({
    super.key,
    required this.icon,
    required this.label,
    required this.value,
    required this.unit,
    required this.color,
    this.status = HealthCardStatus.normal,
  });

  @override
  Widget build(BuildContext context) {
    final isOffline = status == HealthCardStatus.offline;
    final displayValue = isOffline ? '--' : value;
    final valueColor = isOffline
        ? AppColors.textSecondary
        : AppColors.textPrimary;

    return Semantics(
      label: '$label: ${isOffline ? "离线" : "$value $unit"}',
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
              // 顶部：圆形图标 + 标题
              Row(
                children: [
                  Container(
                    width: 36,
                    height: 36,
                    decoration: BoxDecoration(
                      color: color.withValues(alpha: 0.12),
                      shape: BoxShape.circle,
                    ),
                    child: Icon(icon, color: color, size: 20),
                  ),
                  const SizedBox(width: 8),
                  Expanded(
                    child: Text(
                      label,
                      style: const TextStyle(
                        fontSize: 14,
                        fontWeight: FontWeight.w500,
                        color: AppColors.textSecondary,
                      ),
                      overflow: TextOverflow.ellipsis,
                    ),
                  ),
                ],
              ),
              const Spacer(),
              // 中部：大数值 + 单位（带动画切换）
              AnimatedSwitcher(
                duration: const Duration(milliseconds: 300),
                transitionBuilder: (child, animation) => FadeTransition(
                  opacity: animation,
                  child: child,
                ),
                child: Row(
                  key: ValueKey(displayValue),
                  crossAxisAlignment: CrossAxisAlignment.baseline,
                  textBaseline: TextBaseline.alphabetic,
                  children: [
                    Text(
                      displayValue,
                      style: TextStyle(
                        fontSize: 36,
                        fontWeight: FontWeight.w700,
                        color: valueColor,
                        height: 1.1,
                      ),
                    ),
                    const SizedBox(width: 4),
                    Text(
                      unit,
                      style: TextStyle(
                        fontSize: 16,
                        fontWeight: FontWeight.w400,
                        color: valueColor.withValues(alpha: 0.7),
                      ),
                    ),
                  ],
                ),
              ),
              // 底部：异常状态提示
              if (status == HealthCardStatus.abnormal) ...[
                const SizedBox(height: 4),
                Text(
                  '异常',
                  style: TextStyle(
                    fontSize: 12,
                    fontWeight: FontWeight.w500,
                    color: AppColors.abnormal,
                  ),
                ),
              ],
            ],
          ),
        ),
      ),
    );
  }
}
