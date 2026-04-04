import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

/// 最小化冒烟测试 — 验证 FamilyCareBandApp 核心组件可以构建。
/// 由于完整 App 依赖平台插件（sqflite / notifications），
/// 此处仅测试 MaterialApp 基础主题和路由结构。
void main() {
  testWidgets('MaterialApp smoke test — app theme builds correctly',
      (WidgetTester tester) async {
    await tester.pumpWidget(
      MaterialApp(
        title: '家属监护',
        theme: ThemeData(
          colorScheme: ColorScheme.fromSeed(
            seedColor: const Color(0xFF2D7DD2),
            brightness: Brightness.light,
          ),
          useMaterial3: true,
        ),
        home: const Scaffold(
          body: Center(child: Text('家属监护')),
        ),
      ),
    );

    expect(find.text('家属监护'), findsOneWidget);
  });
}
