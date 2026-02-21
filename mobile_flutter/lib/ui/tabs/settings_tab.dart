import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../../data/ble_provider.dart';

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
    final batteryText = ble.latestTelemetry != null
        ? '${ble.latestTelemetry!.battery}%'
        : '--';

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
            _buildInfoRow(context, '电量', batteryText),
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
        ],
      ),
    );
  }
}
