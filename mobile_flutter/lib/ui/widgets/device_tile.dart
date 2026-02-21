import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';

class DeviceTile extends StatelessWidget {
  final ScanResult result;
  final VoidCallback onTap;

  const DeviceTile({super.key, required this.result, required this.onTap});

  Widget _buildSignalIcon(int rssi) {
    IconData icon;
    Color color;
    if (rssi >= -60) {
      icon = Icons.signal_cellular_4_bar;
      color = Colors.green;
    } else if (rssi >= -80) {
      icon = Icons.signal_cellular_alt;
      color = Colors.orange;
    } else {
      icon = Icons.signal_cellular_alt_1_bar;
      color = Colors.red;
    }
    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        Icon(icon, color: color, size: 20),
        const SizedBox(width: 4),
        Text('$rssi dBm', style: TextStyle(color: color, fontSize: 12)),
      ],
    );
  }

  @override
  Widget build(BuildContext context) {
    final deviceName = result.device.platformName.isNotEmpty
        ? result.device.platformName
        : (result.advertisementData.advName.isNotEmpty
              ? result.advertisementData.advName
              : 'Unknown Device');

    return ListTile(
      title: Text(deviceName),
      subtitle: Text(result.device.remoteId.toString()),
      trailing: _buildSignalIcon(result.rssi),
      onTap: onTap,
    );
  }
}
