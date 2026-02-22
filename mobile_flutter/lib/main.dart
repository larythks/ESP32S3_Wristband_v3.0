import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import 'data/ble_provider.dart';
import 'services/notification_service.dart';
import 'ui/scan_page.dart';
import 'ui/device_page.dart';

final GlobalKey<NavigatorState> navigatorKey = GlobalKey<NavigatorState>();

void main() async {
  WidgetsFlutterBinding.ensureInitialized();
  try {
    await NotificationService.instance.init();
  } catch (e) {
    debugPrint('[main] NotificationService init failed: $e');
  }

  // 通知点击回调：设置 pendingTabIndex 让 DevicePage 切换到 AlarmTab
  NotificationService.instance.onTap = (payload) {
    if (payload == 'alarm_tab') {
      final context = navigatorKey.currentContext;
      if (context != null) {
        final ble = Provider.of<BleProvider>(context, listen: false);
        ble.pendingTabIndex = 1;
        // 回到已有的 DevicePage，不创建新实例
        if (ble.isConnected) {
          navigatorKey.currentState?.popUntil(
            (route) => route.settings.name == '/device' || route.isFirst,
          );
        }
      }
    }
  };

  runApp(const CareBandApp());
}

class CareBandApp extends StatelessWidget {
  const CareBandApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MultiProvider(
      providers: [
        ChangeNotifierProvider(create: (_) => BleProvider()),
      ],
      child: MaterialApp(
        navigatorKey: navigatorKey,
        title: 'CareBand',
        theme: ThemeData(
          colorScheme: ColorScheme.fromSeed(
            seedColor: const Color(0xFF2D7DD2),
            brightness: Brightness.light,
            error: const Color(0xFFE63946),
          ),
          useMaterial3: true,
          cardTheme: CardThemeData(
            elevation: 1,
            shape: RoundedRectangleBorder(
              borderRadius: BorderRadius.circular(16),
            ),
            color: Colors.white,
            surfaceTintColor: Colors.transparent,
          ),
          scaffoldBackgroundColor: const Color(0xFFF8F9FB),
          navigationBarTheme: NavigationBarThemeData(
            indicatorColor: const Color(0xFF2D7DD2).withValues(alpha: 0.12),
            backgroundColor: Colors.white,
            surfaceTintColor: Colors.transparent,
          ),
        ),
        initialRoute: '/scan',
        routes: {
          '/scan': (context) => const ScanPage(),
          '/device': (context) => const DevicePage(),
        },
      ),
    );
  }
}
