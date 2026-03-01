import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../../data/ble_provider.dart';
import '../../data/sqlite_repository.dart';

/// 设置页面
class SettingsTab extends StatelessWidget {
  const SettingsTab({super.key});

  static const _errorColor = Color(0xFFE63946);

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('设置'),
      ),
      body: Consumer<BleProvider>(
        builder: (context, ble, child) {
          return SingleChildScrollView(
            padding: const EdgeInsets.all(16),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                // Section 1: 设备信息
                _buildSectionTitle(context, '设备信息'),
                const SizedBox(height: 8),
                _buildDeviceInfoCard(context, ble),
                const SizedBox(height: 24),

                // Section 2: 操作
                _buildSectionTitle(context, '操作'),
                const SizedBox(height: 8),
                _buildActionsCard(context, ble),
                const SizedBox(height: 32),

                // 断开连接按钮
                Center(
                  child: OutlinedButton(
                    style: OutlinedButton.styleFrom(
                      foregroundColor: _errorColor,
                      side: const BorderSide(color: _errorColor),
                      padding: const EdgeInsets.symmetric(
                        horizontal: 32,
                        vertical: 12,
                      ),
                    ),
                    onPressed: () => ble.disconnect(),
                    child: const Text('断开连接'),
                  ),
                ),
                const SizedBox(height: 24),

                // 版本信息
                Center(
                  child: Text(
                    'CareBand App v1.0.0',
                    style: Theme.of(context).textTheme.bodySmall?.copyWith(
                          color: const Color(0xFF6C757D),
                        ),
                  ),
                ),
                const SizedBox(height: 16),
              ],
            ),
          );
        },
      ),
    );
  }

  Widget _buildSectionTitle(BuildContext context, String title) {
    return Padding(
      padding: const EdgeInsets.only(left: 4),
      child: Text(
        title,
        style: Theme.of(context).textTheme.titleMedium?.copyWith(
              fontWeight: FontWeight.w600,
            ),
      ),
    );
  }

  Widget _buildDeviceInfoCard(BuildContext context, BleProvider ble) {
    final isConnected = ble.isConnected;
    final deviceName = ble.connectedDeviceName ?? 'CareBand';

    return Card(
      elevation: 1,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(16),
      ),
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 16),
        child: Column(
          children: [
            _buildInfoRow(context, '设备名称', deviceName),
            const Divider(height: 24),
            _buildConnectionRow(context, isConnected),
            const Divider(height: 24),
            _buildCloudRow(context, ble.mqttConnected, isConnected),
          ],
        ),
      ),
    );
  }

  Widget _buildInfoRow(BuildContext context, String label, String value) {
    return Row(
      mainAxisAlignment: MainAxisAlignment.spaceBetween,
      children: [
        Text(
          label,
          style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                color: const Color(0xFF6C757D),
              ),
        ),
        Text(
          value,
          style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                fontWeight: FontWeight.w500,
              ),
        ),
      ],
    );
  }

  Widget _buildConnectionRow(BuildContext context, bool isConnected) {
    return Row(
      mainAxisAlignment: MainAxisAlignment.spaceBetween,
      children: [
        Text(
          '连接状态',
          style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                color: const Color(0xFF6C757D),
              ),
        ),
        Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            Container(
              width: 8,
              height: 8,
              decoration: BoxDecoration(
                color: isConnected ? Colors.green : Colors.red,
                shape: BoxShape.circle,
              ),
            ),
            const SizedBox(width: 6),
            Text(
              isConnected ? '已连接' : '未连接',
              style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                    fontWeight: FontWeight.w500,
                  ),
            ),
          ],
        ),
      ],
    );
  }

  Widget _buildCloudRow(BuildContext context, bool mqttConnected, bool bleConnected) {
    final Color dotColor;
    final String label;
    if (!bleConnected) {
      dotColor = Colors.grey;
      label = '未启用';
    } else if (mqttConnected) {
      dotColor = Colors.green;
      label = '已连接';
    } else {
      dotColor = Colors.red;
      label = '未连接';
    }

    return Row(
      mainAxisAlignment: MainAxisAlignment.spaceBetween,
      children: [
        Text(
          '云端连接',
          style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                color: const Color(0xFF6C757D),
              ),
        ),
        Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            Container(
              width: 8,
              height: 8,
              decoration: BoxDecoration(
                color: dotColor,
                shape: BoxShape.circle,
              ),
            ),
            const SizedBox(width: 6),
            Text(
              label,
              style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                    fontWeight: FontWeight.w500,
                  ),
            ),
          ],
        ),
      ],
    );
  }

  Widget _buildActionsCard(BuildContext context, BleProvider ble) {
    return Card(
      elevation: 1,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(16),
      ),
      child: Column(
        children: [
          ListTile(
            leading: const Icon(Icons.access_time),
            title: const Text('同步时间'),
            trailing: const Icon(Icons.chevron_right),
            onTap: () async {
              await ble.syncTime();
              if (context.mounted) {
                ScaffoldMessenger.of(context).showSnackBar(
                  const SnackBar(content: Text('时间已同步')),
                );
              }
            },
          ),
          const Divider(height: 1, indent: 16, endIndent: 16),
          ListTile(
            leading: const Icon(Icons.sync),
            title: const Text('请求立即上报'),
            trailing: const Icon(Icons.chevron_right),
            onTap: () async {
              await ble.requestReport();
              if (context.mounted) {
                ScaffoldMessenger.of(context).showSnackBar(
                  const SnackBar(content: Text('已请求上报')),
                );
              }
            },
          ),
          const Divider(height: 1, indent: 16, endIndent: 16),
          ListTile(
            leading: const Icon(Icons.delete_forever, color: _errorColor),
            title: const Text(
              '清除所有数据',
              style: TextStyle(color: _errorColor),
            ),
            trailing: const Icon(Icons.chevron_right, color: _errorColor),
            onTap: () => _showClearAllDataDialog(context, ble),
          ),
        ],
      ),
    );
  }

  void _showClearAllDataDialog(BuildContext context, BleProvider ble) {
    final repository = SqliteDataRepository();

    showDialog(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text(
          '清除所有数据',
          style: TextStyle(color: _errorColor),
        ),
        content: const Text(
          '此操作将永久删除所有本地数据，包括：\n\n'
          '• 所有遥测记录\n'
          '• 所有报警记录（包括未确认的）\n\n'
          '此操作不可恢复，是否继续？',
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.of(ctx).pop(),
            child: const Text('取消'),
          ),
          FilledButton(
            style: FilledButton.styleFrom(
              backgroundColor: _errorColor,
            ),
            onPressed: () async {
              Navigator.of(ctx).pop();
              final result = await repository.clearAllData();
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
