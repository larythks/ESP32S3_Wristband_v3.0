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
  final Set<int> _shownAlarmIds = {};
  bool _isShowingDialog = false;
  BleProvider? _bleRef;

  final List<Widget> _tabs = const [
    DashboardTab(),
    AlarmTab(),
    SettingsTab(),
  ];

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (!mounted) return;
      _bleRef = context.read<BleProvider>();

      // 初始化：处理通知点击待跳转
      if (_bleRef!.pendingTabIndex != null) {
        setState(() {
          _currentIndex = _bleRef!.pendingTabIndex!;
        });
        _bleRef!.pendingTabIndex = null;
      }

      _bleRef!.addListener(_onProviderChanged);
      // 初始检查未确认报警
      _showNextUnackedAlarm();
    });
  }

  @override
  void dispose() {
    _bleRef?.removeListener(_onProviderChanged);
    super.dispose();
  }

  void _onProviderChanged() {
    if (!mounted || _bleRef == null) return;

    // 断连自动返回扫描页
    if (_bleRef!.connectionState == BleConnectionState.disconnected) {
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (mounted) {
          Navigator.popUntil(context, ModalRoute.withName('/scan'));
        }
      });
      return;
    }

    // 通知点击跳转
    if (_bleRef!.pendingTabIndex != null) {
      final idx = _bleRef!.pendingTabIndex!;
      _bleRef!.pendingTabIndex = null;
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (mounted) setState(() => _currentIndex = idx);
      });
    }

    // 检查未确认报警弹窗
    _showNextUnackedAlarm();
  }

  void _showNextUnackedAlarm() {
    if (_isShowingDialog || !mounted || _bleRef == null) return;

    final ble = _bleRef!;
    final alarm = ble.alarmHistory.cast<AlarmData?>().firstWhere(
      (a) => !a!.isAcked && !_shownAlarmIds.contains(a.eventId),
      orElse: () => null,
    );
    if (alarm == null) return;

    _shownAlarmIds.add(alarm.eventId);
    _isShowingDialog = true;

    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (!mounted) {
        _isShowingDialog = false;
        return;
      }
      showDialog(
        context: context,
        barrierDismissible: true,
        builder: (dialogContext) => AlarmDialog(
          alarm: alarm,
          onAck: () {
            ble.ackAlarm(alarm.eventId);
            Navigator.of(dialogContext).pop();
          },
          onDismiss: () => Navigator.of(dialogContext).pop(),
        ),
      ).then((_) {
        _isShowingDialog = false;
        _showNextUnackedAlarm();
      });
    });
  }

  @override
  Widget build(BuildContext context) {
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
  }
}
