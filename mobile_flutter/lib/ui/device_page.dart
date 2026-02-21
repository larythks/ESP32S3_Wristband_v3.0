import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../data/ble_provider.dart';
import '../data/models.dart';
import 'tabs/dashboard_tab.dart';
import 'tabs/alarm_tab.dart';
import 'tabs/settings_tab.dart';
import 'widgets/alarm_dialog.dart';

class DevicePage extends StatefulWidget {
  const DevicePage({super.key});

  @override
  State<DevicePage> createState() => _DevicePageState();
}

class _DevicePageState extends State<DevicePage> {
  int _currentIndex = 0;
  AlarmData? _lastShownAlarm;

  final List<Widget> _tabs = const [
    DashboardTab(),
    AlarmTab(),
    SettingsTab(),
  ];

  void _handleAlarmDialog(BuildContext context, BleProvider ble) {
    final alarm = ble.latestAlarm;
    if (alarm != null && alarm != _lastShownAlarm) {
      _lastShownAlarm = alarm;
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (mounted) {
          showDialog(
            context: context,
            barrierDismissible: true,
            builder: (_) => AlarmDialog(
              alarm: alarm,
              onAck: () {
                ble.ackAlarm(alarm.eventId);
                Navigator.of(context).pop();
              },
              onDismiss: () => Navigator.of(context).pop(),
            ),
          );
        }
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    return Consumer<BleProvider>(
      builder: (context, ble, child) {
        // 断连自动返回扫描页（设置页断开连接 或 外部断连时触发）
        if (ble.connectionState == BleConnectionState.disconnected) {
          WidgetsBinding.instance.addPostFrameCallback((_) {
            if (mounted) {
              Navigator.popUntil(context, ModalRoute.withName('/scan'));
            }
          });
        }

        // 报警弹窗触发
        _handleAlarmDialog(context, ble);

        return Scaffold(
          body: IndexedStack(
            index: _currentIndex,
            children: _tabs,
          ),
          bottomNavigationBar: NavigationBar(
            selectedIndex: _currentIndex,
            onDestinationSelected: (i) => setState(() => _currentIndex = i),
            destinations: const [
              NavigationDestination(
                icon: Icon(Icons.dashboard_outlined),
                selectedIcon: Icon(Icons.dashboard),
                label: '数据',
              ),
              NavigationDestination(
                icon: Icon(Icons.warning_amber_outlined),
                selectedIcon: Icon(Icons.warning_amber_rounded),
                label: '报警',
              ),
              NavigationDestination(
                icon: Icon(Icons.settings_outlined),
                selectedIcon: Icon(Icons.settings),
                label: '设置',
              ),
            ],
          ),
        );
      },
    );
  }
}
