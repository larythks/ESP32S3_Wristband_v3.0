import 'package:flutter/material.dart';
import 'package:permission_handler/permission_handler.dart';
import 'package:provider/provider.dart';
import '../data/ble_provider.dart';
import '../data/models.dart';
import 'widgets/device_tile.dart';

class ScanPage extends StatefulWidget {
  const ScanPage({super.key});

  @override
  State<ScanPage> createState() => _ScanPageState();
}

class _ScanPageState extends State<ScanPage> {
  @override
  void initState() {
    super.initState();
    _checkPermissions();
  }

  Future<void> _checkPermissions() async {
    await [
      Permission.bluetoothScan,
      Permission.bluetoothConnect,
      Permission.location,
    ].request();
  }

  @override
  Widget build(BuildContext context) {
    return Consumer<BleProvider>(
      builder: (context, ble, child) {
        // 连接成功后自动跳转到设备页
        if (ble.connectionState == BleConnectionState.connected) {
          WidgetsBinding.instance.addPostFrameCallback((_) {
            Navigator.pushNamed(context, '/device');
          });
        }

        final isScanning = ble.connectionState == BleConnectionState.scanning;

        // 过滤只显示 CareBand 设备
        final filteredResults = ble.scanResults
            .where((r) => r.device.platformName.contains('CareBand'))
            .toList();

        return Scaffold(
          appBar: AppBar(
            title: const Text('CareBand'),
            actions: [
              if (ble.connectionState == BleConnectionState.connecting)
                const Padding(
                  padding: EdgeInsets.all(16.0),
                  child: SizedBox(
                    width: 20,
                    height: 20,
                    child: CircularProgressIndicator(strokeWidth: 2),
                  ),
                )
              else
                IconButton(
                  icon: Icon(isScanning ? Icons.stop : Icons.search),
                  onPressed: () {
                    if (isScanning) {
                      ble.stopScan();
                    } else {
                      ble.startScan();
                    }
                  },
                ),
            ],
          ),
          body: Column(
            children: [
              if (isScanning)
                const LinearProgressIndicator(),
              if (ble.errorMessage != null)
                Padding(
                  padding: const EdgeInsets.all(8.0),
                  child: Text(
                    ble.errorMessage!,
                    style: const TextStyle(color: Colors.red),
                  ),
                ),
              Expanded(
                child: filteredResults.isEmpty
                    ? Center(
                        child: Text(
                          isScanning ? '正在搜索 CareBand 设备...' : '点击搜索按钮开始扫描',
                          style: Theme.of(context).textTheme.bodyLarge,
                        ),
                      )
                    : ListView.builder(
                        itemCount: filteredResults.length,
                        itemBuilder: (context, index) {
                          final result = filteredResults[index];
                          return DeviceTile(
                            result: result,
                            onTap: () => ble.connectDevice(result.device),
                          );
                        },
                      ),
              ),
            ],
          ),
        );
      },
    );
  }
}
