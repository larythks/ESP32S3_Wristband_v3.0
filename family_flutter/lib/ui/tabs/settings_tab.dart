import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../data/family_repository.dart';
import '../../providers/device_provider.dart';
import '../../theme/app_colors.dart';
import '../../utils/formatters.dart';
import '../binding_page.dart';
import '../widgets/animated_list_item.dart';

/// 设置 Tab
///
/// 设备管理、连接状态、通知开关、数据管理、关于
class SettingsTab extends StatefulWidget {
  const SettingsTab({super.key});

  @override
  State<SettingsTab> createState() => _SettingsTabState();
}

class _SettingsTabState extends State<SettingsTab> {
  bool _notifyEnabled = true;
  bool _vibrateEnabled = true;

  @override
  void initState() {
    super.initState();
    _loadPrefs();
  }

  Future<void> _loadPrefs() async {
    final prefs = await SharedPreferences.getInstance();
    setState(() {
      _notifyEnabled = prefs.getBool('notify_enabled') ?? true;
      _vibrateEnabled = prefs.getBool('vibrate_enabled') ?? true;
    });
  }

  Future<void> _setNotifyEnabled(bool value) async {
    setState(() => _notifyEnabled = value);
    final prefs = await SharedPreferences.getInstance();
    await prefs.setBool('notify_enabled', value);
    debugPrint('[SettingsTab] 通知开关: $value');
  }

  Future<void> _setVibrateEnabled(bool value) async {
    setState(() => _vibrateEnabled = value);
    final prefs = await SharedPreferences.getInstance();
    await prefs.setBool('vibrate_enabled', value);
    debugPrint('[SettingsTab] 振动开关: $value');
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final statusBarHeight = MediaQuery.of(context).padding.top;

    return Consumer<DeviceProvider>(
      builder: (context, provider, _) {
        return ListView(
          padding: EdgeInsets.fromLTRB(16, statusBarHeight + 16, 16, 16),
          children: [
            // ---- 设备管理 ----
            AnimatedListItem(
              index: 0,
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  _SectionTitle(title: '设备管理'),
                  const SizedBox(height: 8),
                  _DeviceManagementCard(provider: provider),
                ],
              ),
            ),
            const SizedBox(height: 20),

            // ---- 连接状态 ----
            AnimatedListItem(
              index: 1,
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  _SectionTitle(title: '连接状态'),
                  const SizedBox(height: 8),
                  _ConnectionStatusCard(provider: provider),
                ],
              ),
            ),
            const SizedBox(height: 20),

            // ---- 远程操作 ----
            AnimatedListItem(
              index: 2,
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  _SectionTitle(title: '远程操作'),
                  const SizedBox(height: 8),
                  _RemoteCommandCard(provider: provider),
                ],
              ),
            ),
            const SizedBox(height: 20),

            // ---- 通知设置 ----
            AnimatedListItem(
              index: 3,
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  _SectionTitle(title: '通知设置'),
                  const SizedBox(height: 8),
                  _buildNotificationSettings(theme),
                ],
              ),
            ),
            const SizedBox(height: 20),

            // ---- 数据管理 ----
            AnimatedListItem(
              index: 4,
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  _SectionTitle(title: '数据管理'),
                  const SizedBox(height: 8),
                  _DataManagementCard(provider: provider),
                ],
              ),
            ),
            const SizedBox(height: 20),

            // ---- 关于 ----
            AnimatedListItem(
              index: 5,
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  _SectionTitle(title: '关于'),
                  const SizedBox(height: 8),
                  _buildAboutCard(theme),
                ],
              ),
            ),
            const SizedBox(height: 16),
          ],
        );
      },
    );
  }

  /// 通知设置（统一白色卡片风格）
  Widget _buildNotificationSettings(ThemeData theme) {
    return Card(
      color: Colors.white,
      elevation: 1.5,
      shadowColor: Colors.black.withValues(alpha: 0.08),
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(16),
      ),
      child: Column(
        children: [
          SwitchListTile(
            title: const Text('报警通知'),
            subtitle: const Text('收到报警时推送系统通知'),
            secondary: const Icon(Icons.notifications_active),
            value: _notifyEnabled,
            onChanged: _setNotifyEnabled,
          ),
          const Divider(height: 1, indent: 16, endIndent: 16),
          SwitchListTile(
            title: const Text('振动提醒'),
            subtitle: const Text('收到报警时振动'),
            secondary: const Icon(Icons.vibration),
            value: _vibrateEnabled,
            onChanged: _setVibrateEnabled,
          ),
        ],
      ),
    );
  }

  /// 关于信息（统一白色卡片风格 + 信息行）
  Widget _buildAboutCard(ThemeData theme) {
    return Card(
      color: Colors.white,
      elevation: 1.5,
      shadowColor: Colors.black.withValues(alpha: 0.08),
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(16),
      ),
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 16),
        child: Column(
          children: [
            _buildInfoRow('应用名称', 'CareBand 家属端'),
            const Divider(height: 24),
            _buildInfoRow('版本号', '1.0.0'),
            const Divider(height: 24),
            _buildInfoRow('项目', '毕业设计 - 老人健康监护'),
          ],
        ),
      ),
    );
  }

  /// 信息行（与 mobile 设置页信息行风格统一）
  Widget _buildInfoRow(String label, String value) {
    return Row(
      mainAxisAlignment: MainAxisAlignment.spaceBetween,
      children: [
        Text(
          label,
          style: const TextStyle(
            fontSize: 14,
            color: AppColors.textSecondary,
          ),
        ),
        Text(
          value,
          style: const TextStyle(
            fontSize: 14,
            fontWeight: FontWeight.w500,
            color: AppColors.textPrimary,
          ),
        ),
      ],
    );
  }
}

/// 段落标题（与 mobile 设置页风格统一）
class _SectionTitle extends StatelessWidget {
  final String title;

  const _SectionTitle({required this.title});

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(left: 4),
      child: Text(
        title,
        style: const TextStyle(
          fontSize: 16,
          fontWeight: FontWeight.w600,
          color: AppColors.textPrimary,
        ),
      ),
    );
  }
}

/// 设备管理卡片（与 mobile 风格统一：白色卡片 + 圆形图标）
class _DeviceManagementCard extends StatelessWidget {
  final DeviceProvider provider;

  const _DeviceManagementCard({required this.provider});

  @override
  Widget build(BuildContext context) {
    final statusColor = provider.isBound ? AppColors.normal : AppColors.offline;

    return Card(
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
            Row(
              children: [
                Container(
                  width: 44,
                  height: 44,
                  decoration: BoxDecoration(
                    color: statusColor.withValues(alpha: 0.12),
                    shape: BoxShape.circle,
                  ),
                  child: Icon(Icons.watch, color: statusColor, size: 24),
                ),
                const SizedBox(width: 12),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        provider.isBound ? '已绑定设备' : '未绑定设备',
                        style: const TextStyle(
                          fontSize: 15,
                          fontWeight: FontWeight.w600,
                          color: AppColors.textPrimary,
                        ),
                      ),
                      if (provider.isBound)
                        Text(
                          '设备 ID: ${provider.deviceId}',
                          style: const TextStyle(
                            fontSize: 13,
                            color: AppColors.textSecondary,
                          ),
                        ),
                    ],
                  ),
                ),
              ],
            ),
            const SizedBox(height: 16),

            // 操作按钮
            Row(
              mainAxisAlignment: MainAxisAlignment.end,
              children: [
                if (provider.isBound) ...[
                  OutlinedButton(
                    onPressed: () => _showUnbindDialog(context),
                    style: OutlinedButton.styleFrom(
                      foregroundColor: AppColors.abnormal,
                      side: const BorderSide(color: AppColors.abnormal),
                      shape: RoundedRectangleBorder(
                        borderRadius: BorderRadius.circular(12),
                      ),
                      padding: const EdgeInsets.symmetric(
                        horizontal: 16, vertical: 10,
                      ),
                    ),
                    child: const Text('解绑设备'),
                  ),
                  const SizedBox(width: 8),
                ],
                FilledButton(
                  onPressed: () {
                    Navigator.of(context).pushReplacement(
                      MaterialPageRoute(
                        builder: (_) => const BindingPage(),
                      ),
                    );
                  },
                  style: FilledButton.styleFrom(
                    shape: RoundedRectangleBorder(
                      borderRadius: BorderRadius.circular(12),
                    ),
                    padding: const EdgeInsets.symmetric(
                      horizontal: 16, vertical: 10,
                    ),
                  ),
                  child: Text(
                    provider.isBound ? '重新绑定' : '绑定设备',
                  ),
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }

  void _showUnbindDialog(BuildContext context) {
    showDialog(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('确认解绑'),
        content: const Text('解绑后将断开 MQTT 连接，不再接收设备数据。确定要解绑吗？'),
        actions: [
          TextButton(
            onPressed: () => Navigator.of(ctx).pop(),
            child: const Text('取消'),
          ),
          FilledButton(
            onPressed: () async {
              Navigator.of(ctx).pop();
              await context.read<DeviceProvider>().unbindDevice();
              if (context.mounted) {
                Navigator.of(context).pushReplacement(
                  MaterialPageRoute(
                    builder: (_) => const BindingPage(),
                  ),
                );
              }
            },
            style: FilledButton.styleFrom(
              backgroundColor: AppColors.abnormal,
            ),
            child: const Text('确认解绑'),
          ),
        ],
      ),
    );
  }
}

/// 连接状态卡片（与 mobile 信息行风格统一：标签 | 状态点 + 值）
class _ConnectionStatusCard extends StatelessWidget {
  final DeviceProvider provider;

  const _ConnectionStatusCard({required this.provider});

  @override
  Widget build(BuildContext context) {
    return Card(
      color: Colors.white,
      elevation: 1.5,
      shadowColor: Colors.black.withValues(alpha: 0.08),
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(16),
      ),
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 16),
        child: Column(
          children: [
            // MQTT 连接状态
            _buildStatusRow(
              label: 'MQTT 连接',
              value: provider.mqttConnected ? '已连接' : '未连接',
              color: provider.mqttConnected
                  ? AppColors.normal
                  : AppColors.offline,
            ),
            const Divider(height: 24),

            // 设备在线状态
            _buildStatusRow(
              label: '设备状态',
              value: provider.deviceOnline ? '在线' : '离线',
              color: provider.deviceOnline
                  ? AppColors.normal
                  : AppColors.offline,
            ),
            const Divider(height: 24),

            // 最后更新时间
            Row(
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              children: [
                const Text(
                  '最后更新',
                  style: TextStyle(
                    fontSize: 14,
                    color: AppColors.textSecondary,
                  ),
                ),
                Text(
                  provider.lastSeen != null
                      ? formatDateTime(provider.lastSeen!)
                      : '--',
                  style: const TextStyle(
                    fontSize: 14,
                    fontWeight: FontWeight.w500,
                    color: AppColors.textPrimary,
                  ),
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildStatusRow({
    required String label,
    required String value,
    required Color color,
  }) {
    return Row(
      mainAxisAlignment: MainAxisAlignment.spaceBetween,
      children: [
        Text(
          label,
          style: const TextStyle(
            fontSize: 14,
            color: AppColors.textSecondary,
          ),
        ),
        Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            Container(
              width: 8,
              height: 8,
              decoration: BoxDecoration(
                color: color,
                shape: BoxShape.circle,
              ),
            ),
            const SizedBox(width: 6),
            Text(
              value,
              style: TextStyle(
                fontSize: 14,
                fontWeight: FontWeight.w500,
                color: color,
              ),
            ),
          ],
        ),
      ],
    );
  }
}

/// 远程操作卡片（同步时间 + 请求上报）
class _RemoteCommandCard extends StatelessWidget {
  final DeviceProvider provider;

  const _RemoteCommandCard({required this.provider});

  @override
  Widget build(BuildContext context) {
    final enabled = provider.isBound && provider.mqttConnected;

    return Card(
      color: Colors.white,
      elevation: 1.5,
      shadowColor: Colors.black.withValues(alpha: 0.08),
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(16),
      ),
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 16),
        child: Column(
          children: [
            // 同步时间
            InkWell(
              onTap: enabled
                  ? () async {
                      await provider.syncTime();
                      if (context.mounted) {
                        ScaffoldMessenger.of(context).showSnackBar(
                          const SnackBar(content: Text('时间同步命令已发送')),
                        );
                      }
                    }
                  : null,
              borderRadius: BorderRadius.circular(8),
              child: Row(
                children: [
                  Container(
                    width: 36,
                    height: 36,
                    decoration: BoxDecoration(
                      color: AppColors.primary.withValues(alpha: 0.12),
                      shape: BoxShape.circle,
                    ),
                    child: const Icon(
                      Icons.access_time,
                      size: 18,
                      color: AppColors.primary,
                    ),
                  ),
                  const SizedBox(width: 12),
                  Expanded(
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text(
                          '同步时间',
                          style: TextStyle(
                            fontSize: 14,
                            fontWeight: FontWeight.w500,
                            color: enabled
                                ? AppColors.textPrimary
                                : AppColors.textSecondary,
                          ),
                        ),
                        const SizedBox(height: 2),
                        const Text(
                          '将手机时间同步到手环',
                          style: TextStyle(
                            fontSize: 12,
                            color: AppColors.textSecondary,
                          ),
                        ),
                      ],
                    ),
                  ),
                  Icon(
                    Icons.chevron_right,
                    color: enabled
                        ? AppColors.textSecondary
                        : AppColors.textSecondary.withValues(alpha: 0.4),
                  ),
                ],
              ),
            ),
            const Divider(height: 24),

            // 请求立即上报
            InkWell(
              onTap: enabled
                  ? () async {
                      await provider.requestReport();
                      if (context.mounted) {
                        ScaffoldMessenger.of(context).showSnackBar(
                          const SnackBar(content: Text('已请求设备立即上报数据')),
                        );
                      }
                    }
                  : null,
              borderRadius: BorderRadius.circular(8),
              child: Row(
                children: [
                  Container(
                    width: 36,
                    height: 36,
                    decoration: BoxDecoration(
                      color: AppColors.metricSpo2.withValues(alpha: 0.12),
                      shape: BoxShape.circle,
                    ),
                    child: const Icon(
                      Icons.sync,
                      size: 18,
                      color: AppColors.metricSpo2,
                    ),
                  ),
                  const SizedBox(width: 12),
                  Expanded(
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text(
                          '请求立即上报',
                          style: TextStyle(
                            fontSize: 14,
                            fontWeight: FontWeight.w500,
                            color: enabled
                                ? AppColors.textPrimary
                                : AppColors.textSecondary,
                          ),
                        ),
                        const SizedBox(height: 2),
                        const Text(
                          '请求手环立即上报最新数据',
                          style: TextStyle(
                            fontSize: 12,
                            color: AppColors.textSecondary,
                          ),
                        ),
                      ],
                    ),
                  ),
                  Icon(
                    Icons.chevron_right,
                    color: enabled
                        ? AppColors.textSecondary
                        : AppColors.textSecondary.withValues(alpha: 0.4),
                  ),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }
}

/// 数据管理卡片（统一白色卡片风格）
class _DataManagementCard extends StatelessWidget {
  final DeviceProvider provider;

  const _DataManagementCard({required this.provider});

  @override
  Widget build(BuildContext context) {
    return Card(
      color: Colors.white,
      elevation: 1.5,
      shadowColor: Colors.black.withValues(alpha: 0.08),
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(16),
      ),
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 16),
        child: Column(
          children: [
            // 遥测记录
            Row(
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              children: [
                const Text(
                  '遥测记录',
                  style: TextStyle(
                    fontSize: 14,
                    color: AppColors.textSecondary,
                  ),
                ),
                Text(
                  '${provider.telemetryHistory.length} 条',
                  style: const TextStyle(
                    fontSize: 14,
                    fontWeight: FontWeight.w500,
                    color: AppColors.textPrimary,
                  ),
                ),
              ],
            ),
            const Divider(height: 24),

            // 报警记录
            Row(
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              children: [
                const Text(
                  '报警记录',
                  style: TextStyle(
                    fontSize: 14,
                    color: AppColors.textSecondary,
                  ),
                ),
                Text(
                  '${provider.alarmHistory.length} 条',
                  style: const TextStyle(
                    fontSize: 14,
                    fontWeight: FontWeight.w500,
                    color: AppColors.textPrimary,
                  ),
                ),
              ],
            ),
            const Divider(height: 24),

            // 手动清理（7天前数据）
            InkWell(
              onTap: () => _showCleanupDialog(context),
              borderRadius: BorderRadius.circular(8),
              child: const Row(
                mainAxisAlignment: MainAxisAlignment.spaceBetween,
                children: [
                  Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        '清理旧数据',
                        style: TextStyle(
                          fontSize: 14,
                          fontWeight: FontWeight.w500,
                          color: AppColors.textPrimary,
                        ),
                      ),
                      SizedBox(height: 2),
                      Text(
                        '清理 7 天前的遥测和已确认报警',
                        style: TextStyle(
                          fontSize: 12,
                          color: AppColors.textSecondary,
                        ),
                      ),
                    ],
                  ),
                  Icon(
                    Icons.chevron_right,
                    color: AppColors.textSecondary,
                  ),
                ],
              ),
            ),
            const Divider(height: 24),

            // 清除所有数据
            InkWell(
              onTap: () => _showClearAllDialog(context),
              borderRadius: BorderRadius.circular(8),
              child: const Row(
                mainAxisAlignment: MainAxisAlignment.spaceBetween,
                children: [
                  Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        '清除所有数据',
                        style: TextStyle(
                          fontSize: 14,
                          fontWeight: FontWeight.w500,
                          color: AppColors.abnormal,
                        ),
                      ),
                      SizedBox(height: 2),
                      Text(
                        '清除所有遥测、报警和配置数据',
                        style: TextStyle(
                          fontSize: 12,
                          color: AppColors.textSecondary,
                        ),
                      ),
                    ],
                  ),
                  Icon(
                    Icons.delete_forever,
                    color: AppColors.abnormal,
                  ),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }

  void _showCleanupDialog(BuildContext context) {
    showDialog(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('数据清理'),
        content: const Text('将清理 7 天前的遥测数据和已确认的报警记录。未确认的报警不会被清理。'),
        actions: [
          TextButton(
            onPressed: () => Navigator.of(ctx).pop(),
            child: const Text('取消'),
          ),
          FilledButton(
            onPressed: () async {
              Navigator.of(ctx).pop();
              await FamilyRepository.instance.cleanup();
              await provider.refreshHistory();
              if (context.mounted) {
                ScaffoldMessenger.of(context).showSnackBar(
                  const SnackBar(content: Text('数据清理完成')),
                );
              }
            },
            child: const Text('确认清理'),
          ),
        ],
      ),
    );
  }

  void _showClearAllDialog(BuildContext context) {
    showDialog(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('清除所有数据'),
        titleTextStyle: const TextStyle(
          color: AppColors.abnormal,
          fontSize: 20,
          fontWeight: FontWeight.w600,
        ),
        content: const Text(
          '此操作将永久删除所有本地数据，包括：\n\n'
          '• 所有遥测记录\n'
          '• 所有报警记录（包括未确认的）\n'
          '• 应用配置\n\n'
          '此操作不可恢复，是否继续？',
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.of(ctx).pop(),
            child: const Text('取消'),
          ),
          FilledButton(
            style: FilledButton.styleFrom(
              backgroundColor: AppColors.abnormal,
            ),
            onPressed: () async {
              Navigator.of(ctx).pop();
              final result = await FamilyRepository.instance.clearAllData();
              await provider.refreshHistory();
              if (context.mounted) {
                ScaffoldMessenger.of(context).showSnackBar(
                  SnackBar(
                    content: Text(
                      '已清除：遥测 ${result['telemetry']} 条，报警 ${result['alarm']} 条',
                    ),
                  ),
                );
              }
            },
            child: const Text('确认清除'),
          ),
        ],
      ),
    );
  }
}
