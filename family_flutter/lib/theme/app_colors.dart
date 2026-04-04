import 'package:flutter/material.dart';

/// CareBand 家属端语义化颜色常量
///
/// 所有 UI 组件应引用此类，禁止在 Widget 中硬编码 Color 值
abstract class AppColors {
  // ---- 品牌色 ----
  static const primary = Color(0xFF2D7DD2); // 信赖蓝
  static const surface = Color(0xFFF8F9FB); // 页面背景
  static const info = Color(0xFF0891B2); // 信息青

  // ---- 文本色（与 mobile 统一） ----
  static const textPrimary = Color(0xFF212529); // 主文本
  static const textSecondary = Color(0xFF6C757D); // 次要文本

  // ---- 状态色 ----
  static const normal = Color(0xFF059669); // 健康/正常 Emerald-600
  static const abnormal = Color(0xFFE63946); // 异常/警告（与 mobile 统一）
  static const offline = Color(0xFF6C757D); // 离线/禁用（与 mobile 统一）

  // ---- 报警类型色（与 mobile 指标色系对齐：柔和色调） ----
  static const alarmHeartRate = Color(0xFFE63946); // 心率（柔和红）
  static const alarmSpo2 = Color(0xFF457B9D); // 血氧（灰蓝）
  static const alarmTemp = Color(0xFFF4A261); // 温度（暖橙）
  static const alarmFall = Color(0xFF8B5CF6); // 跌倒（紫罗兰）
  static const alarmManual = Color(0xFFE63946); // 手动/SOS
  static const alarmCallFamily = Color(0xFF2D7DD2); // 呼叫家人
  static const alarmUnknown = Color(0xFF6C757D); // 未知

  // ---- 图表色 ----
  static const chartLine = Color(0xFF2D7DD2);
  static const chartThreshold = Color(0xFFE63946);
  static const chartFill = Color(0x142D7DD2); // 8% opacity

  // ---- 摘要卡片指标色（与 mobile HealthCard 色系统一） ----
  static const metricHeartRate = Color(0xFFE63946); // 柔和红
  static const metricSpo2 = Color(0xFF457B9D); // 灰蓝
  static const metricTemp = Color(0xFFF4A261); // 暖橙
  static const metricSteps = Color(0xFF45B7A0); // 青绿
}
