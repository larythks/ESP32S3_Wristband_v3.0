// 通用格式化工具函数
// 替代各 widget 中重复的 _formatTime / _pad / _formatSteps

/// 短时间: "HH:mm:ss"
String formatShortTime(DateTime dt) {
  return '${_pad(dt.hour)}:${_pad(dt.minute)}:${_pad(dt.second)}';
}

/// 日期时间: "M/d HH:mm"
String formatDateTime(DateTime dt) {
  return '${dt.month}/${dt.day} ${_pad(dt.hour)}:${_pad(dt.minute)}';
}

/// 完整日期时间: "yyyy-MM-dd HH:mm:ss"
String formatFullDateTime(DateTime dt) {
  return '${dt.year}-${_pad(dt.month)}-${_pad(dt.day)} '
      '${_pad(dt.hour)}:${_pad(dt.minute)}:${_pad(dt.second)}';
}

/// 日期时间含分钟: "yyyy-MM-dd HH:mm"
String formatDateTimeMinute(DateTime dt) {
  return '${dt.year}-${_pad(dt.month)}-${_pad(dt.day)} '
      '${_pad(dt.hour)}:${_pad(dt.minute)}';
}

/// 中文日期: "yyyy年M月d日"
String formatDateChinese(DateTime dt) {
  return '${dt.year}年${dt.month}月${dt.day}日';
}

/// 步数千分位: "1,234"
String formatSteps(int steps) {
  if (steps < 1000) return '$steps';
  final str = steps.toString();
  final buffer = StringBuffer();
  for (int i = 0; i < str.length; i++) {
    if (i > 0 && (str.length - i) % 3 == 0) buffer.write(',');
    buffer.write(str[i]);
  }
  return buffer.toString();
}

String _pad(int n) => n.toString().padLeft(2, '0');
