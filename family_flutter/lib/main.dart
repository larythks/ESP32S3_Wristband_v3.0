import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import 'package:family_flutter/providers/device_provider.dart';
import 'package:family_flutter/providers/health_analysis_provider.dart';
import 'package:family_flutter/services/notification_service.dart';
import 'package:family_flutter/ui/binding_page.dart';
import 'package:family_flutter/ui/home_page.dart';
import 'theme/app_theme.dart';

/// 全局 Navigator Key，用于通知点击后的页面跳转
final GlobalKey<NavigatorState> navigatorKey = GlobalKey<NavigatorState>();

void main() async {
  WidgetsFlutterBinding.ensureInitialized();

  // 初始化通知服务
  try {
    await NotificationService.instance.init();
  } catch (e) {
    debugPrint('[main] NotificationService init failed: $e');
  }

  // 通知点击回调：解析 payload，跳转到报警 Tab
  NotificationService.instance.onTap = (payload) {
    final context = navigatorKey.currentContext;
    if (context != null) {
      final provider = Provider.of<DeviceProvider>(context, listen: false);
      // 设置 pendingTabIndex，让 HomePage 切换到报警 Tab
      provider.pendingTabIndex = 2;
    }
  };

  runApp(const FamilyCareBandApp());
}

class FamilyCareBandApp extends StatelessWidget {
  const FamilyCareBandApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MultiProvider(
      providers: [
        ChangeNotifierProvider(create: (_) => DeviceProvider()),
        ChangeNotifierProvider(create: (_) => HealthAnalysisProvider()),
      ],
      child: MaterialApp(
        navigatorKey: navigatorKey,
        title: '家属监护',
        theme: buildAppTheme(),
        home: const _AppEntry(),
        debugShowCheckedModeBanner: false,
      ),
    );
  }
}

/// 入口路由：已绑定设备 → HomePage，未绑定 → BindingPage
class _AppEntry extends StatelessWidget {
  const _AppEntry();

  @override
  Widget build(BuildContext context) {
    return Consumer<DeviceProvider>(
      builder: (context, provider, _) {
        if (provider.isBound) {
          return const HomePage();
        }
        return const BindingPage();
      },
    );
  }
}
