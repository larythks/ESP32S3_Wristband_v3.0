import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
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
  static final Guid _careBandServiceUuid = Guid(
    '0000ff00-0000-1000-8000-00805f9b34fb',
  );

  bool _navigatedToDevice = false;

  bool _isCareBandDevice(ScanResult result) {
    final platformName = result.device.platformName.toLowerCase();
    final advName = result.advertisementData.advName.toLowerCase();
    final hasCareBandName =
        platformName.contains('careband') || advName.contains('careband');
    final hasCareBandService = result.advertisementData.serviceUuids.contains(
      _careBandServiceUuid,
    );
    return hasCareBandName || hasCareBandService;
  }

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
        // 连接成功后自动跳转到设备页（仅一次）
        if (ble.connectionState == BleConnectionState.connected &&
            !_navigatedToDevice) {
          _navigatedToDevice = true;
          WidgetsBinding.instance.addPostFrameCallback((_) {
            if (mounted) {
              Navigator.pushNamed(context, '/device');
            }
          });
        } else if (ble.connectionState == BleConnectionState.disconnected) {
          _navigatedToDevice = false;
        }

        final isScanning = ble.connectionState == BleConnectionState.scanning;

        // 过滤只显示 CareBand 设备
        final filteredResults = ble.scanResults
            .where(_isCareBandDevice)
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
              // 已连接设备入口按钮
              if (ble.isConnected)
                Card(
                  margin: const EdgeInsets.all(12.0),
                  color: Colors.green.shade50,
                  child: ListTile(
                    leading: const Icon(
                      Icons.bluetooth_connected,
                      color: Colors.green,
                      size: 32,
                    ),
                    title: Text(
                      ble.connectedDeviceName ?? 'CareBand',
                      style: const TextStyle(fontWeight: FontWeight.bold),
                    ),
                    subtitle: const Text('已连接 - 点击查看设备数据'),
                    trailing: const Icon(Icons.arrow_forward_ios, size: 16),
                    onTap: () {
                      Navigator.pushNamed(context, '/device');
                    },
                  ),
                ),
              if (isScanning) const LinearProgressIndicator(),
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
