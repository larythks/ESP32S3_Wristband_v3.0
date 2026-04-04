import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../providers/device_provider.dart';
import '../services/notification_service.dart';
import 'tabs/alarm_tab.dart';
import 'tabs/dashboard_tab.dart';
import 'tabs/settings_tab.dart';
import 'tabs/trend_tab.dart';

/// 主页（四 Tab 底部导航）
///
/// Tab: 仪表盘 / 趋势 / 报警 / 设置
class HomePage extends StatefulWidget {
  const HomePage({super.key});

  @override
  State<HomePage> createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> {
  int _currentIndex = 0;

  /// 四个 Tab 页面
  final List<Widget> _tabs = const [
    DashboardTab(),
    TrendTab(),
    AlarmTab(),
    SettingsTab(),
  ];

  @override
  void initState() {
    super.initState();
    _setupNotificationTap();
  }

  /// 配置通知点击回调
  void _setupNotificationTap() {
    NotificationService.instance.onTap = (payload) {
      debugPrint('[HomePage] 通知点击: payload=$payload');
      if (payload != null && payload.startsWith('alarm_')) {
        // 跳转到报警 Tab（index=2）
        final provider = context.read<DeviceProvider>();
        provider.pendingTabIndex = 2;
      }
    };
  }

  @override
  Widget build(BuildContext context) {
    // 监听 pendingTabIndex 跳转
    final provider = context.watch<DeviceProvider>();
    if (provider.pendingTabIndex != null) {
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (mounted && provider.pendingTabIndex != null) {
          setState(() {
            _currentIndex = provider.pendingTabIndex!;
          });
          provider.pendingTabIndex = null;
        }
      });
    }

    return Scaffold(
      body: IndexedStack(
        index: _currentIndex,
        children: _tabs,
      ),
      bottomNavigationBar: NavigationBar(
        selectedIndex: _currentIndex,
        onDestinationSelected: (index) {
          setState(() => _currentIndex = index);
        },
        destinations: [
          const NavigationDestination(
            icon: Icon(Icons.dashboard_outlined),
            selectedIcon: Icon(Icons.dashboard),
            label: '仪表盘',
          ),
          const NavigationDestination(
            icon: Icon(Icons.trending_up_outlined),
            selectedIcon: Icon(Icons.trending_up),
            label: '趋势',
          ),
          NavigationDestination(
            icon: Badge(
              isLabelVisible: provider.unackedAlarmCount > 0,
              label: Text('${provider.unackedAlarmCount}'),
              child: const Icon(Icons.notifications_outlined),
            ),
            selectedIcon: Badge(
              isLabelVisible: provider.unackedAlarmCount > 0,
              label: Text('${provider.unackedAlarmCount}'),
              child: const Icon(Icons.notifications),
            ),
            label: '报警',
          ),
          const NavigationDestination(
            icon: Icon(Icons.settings_outlined),
            selectedIcon: Icon(Icons.settings),
            label: '设置',
          ),
        ],
      ),
    );
  }
}
