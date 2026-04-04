import 'package:flutter/material.dart';

/// 列表项入场动画包装器
///
/// 用法: AnimatedListItem(index: 0, child: YourWidget())
/// 效果: fade + slide-up, 交错延迟
class AnimatedListItem extends StatelessWidget {
  final int index;
  final Widget child;
  final Duration baseDuration;
  final Duration staggerDelay;

  const AnimatedListItem({
    super.key,
    required this.index,
    required this.child,
    this.baseDuration = const Duration(milliseconds: 400),
    this.staggerDelay = const Duration(milliseconds: 60),
  });

  @override
  Widget build(BuildContext context) {
    final totalDuration = baseDuration + staggerDelay * index;

    return TweenAnimationBuilder<double>(
      tween: Tween(begin: 0, end: 1),
      duration: totalDuration,
      curve: Curves.easeOutCubic,
      builder: (context, value, child) {
        return Opacity(
          opacity: value.clamp(0, 1),
          child: Transform.translate(
            offset: Offset(0, 24 * (1 - value)),
            child: child,
          ),
        );
      },
      child: child,
    );
  }
}
