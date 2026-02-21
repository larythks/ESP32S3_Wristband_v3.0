import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../data/ble_provider.dart';
import '../data/models.dart';

class DevicePage extends StatefulWidget {
  const DevicePage({super.key});

  @override
  State<DevicePage> createState() => _DevicePageState();
}

class _DevicePageState extends State<DevicePage> {
  AlarmData? _lastShownAlarm;
  bool _isDisconnecting = false;

  Future<void> _disconnect(BleProvider ble) async {
    if (_isDisconnecting) return;

    setState(() {
      _isDisconnecting = true;
    });

    try {
      debugPrint('[DevicePage] disconnect requested');
      await ble.disconnect();
      debugPrint(
        '[DevicePage] disconnect finished, state=${ble.connectionState}',
      );
    } catch (e, st) {
      debugPrint('[DevicePage] disconnect error: $e');
      debugPrint('$st');
    } finally {
      if (mounted) {
        setState(() {
          _isDisconnecting = false;
        });
      }
    }
  }

  Future<bool> _onWillPop(BleProvider ble) async {
    if (_isDisconnecting) return false;

    final state = ble.connectionState;
    final needDisconnect =
        state == BleConnectionState.connected ||
        state == BleConnectionState.connecting ||
        state == BleConnectionState.disconnecting;

    if (!needDisconnect) return true;

    await _disconnect(ble);
    return ble.connectionState == BleConnectionState.disconnected;
  }

  @override
  Widget build(BuildContext context) {
    return Consumer<BleProvider>(
      builder: (context, ble, child) {
        if (ble.connectionState == BleConnectionState.disconnected) {
          WidgetsBinding.instance.addPostFrameCallback((_) {
            if (mounted) {
              Navigator.popUntil(context, ModalRoute.withName('/scan'));
            }
          });
        }

        final alarm = ble.latestAlarm;
        if (alarm != null && alarm != _lastShownAlarm) {
          _lastShownAlarm = alarm;
          WidgetsBinding.instance.addPostFrameCallback((_) {
            ScaffoldMessenger.of(context).showSnackBar(
              SnackBar(
                content: Text(
                  '报警: ${alarm.alarmType.name} - 值 ${alarm.triggerValue}',
                ),
                backgroundColor: Colors.red,
                duration: const Duration(seconds: 5),
              ),
            );
          });
        }

        final telemetry = ble.latestTelemetry;

        return PopScope(
          canPop:
              !_isDisconnecting &&
              ble.connectionState == BleConnectionState.disconnected,
          onPopInvokedWithResult: (didPop, _) async {
            if (didPop) return;
            await _onWillPop(ble);
          },
          child: Scaffold(
            appBar: AppBar(
              title: const Text('CareBand'),
              actions: [
                Chip(
                  label: Text(
                    _isDisconnecting
                        ? '断开中'
                        : (ble.isConnected ? '已连接' : '未连接'),
                    style: const TextStyle(fontSize: 12),
                  ),
                  backgroundColor: _isDisconnecting
                      ? Colors.orange.shade100
                      : (ble.isConnected
                            ? Colors.green.shade100
                            : Colors.grey.shade300),
                ),
                IconButton(
                  icon: _isDisconnecting
                      ? const SizedBox(
                          width: 18,
                          height: 18,
                          child: CircularProgressIndicator(strokeWidth: 2),
                        )
                      : const Icon(Icons.link_off),
                  tooltip: '断开连接',
                  onPressed: _isDisconnecting
                      ? null
                      : () async {
                          await _disconnect(ble);
                        },
                ),
              ],
            ),
            body: telemetry == null
                ? const Center(child: Text('等待数据...'))
                : Padding(
                    padding: const EdgeInsets.all(16.0),
                    child: Column(
                      children: [
                        _buildCard(
                          icon: Icons.thermostat,
                          title: '温度',
                          value: telemetry.isTempValid
                              ? '${telemetry.temperature.toStringAsFixed(1)}°C'
                              : '--',
                          color: Colors.orange,
                        ),
                        const SizedBox(height: 12),
                        _buildCard(
                          icon: Icons.favorite,
                          title: '心率',
                          value: telemetry.isHrValid
                              ? '${telemetry.heartRate} bpm'
                              : '--',
                          color: Colors.red,
                        ),
                        const SizedBox(height: 12),
                        _buildCard(
                          icon: Icons.water_drop,
                          title: '血氧',
                          value: telemetry.isSpo2Valid
                              ? '${telemetry.spo2}%'
                              : '--',
                          color: Colors.blue,
                        ),
                        const SizedBox(height: 12),
                        _buildCard(
                          icon: Icons.directions_walk,
                          title: '步数',
                          value: '${telemetry.steps}',
                          color: Colors.green,
                        ),
                        const SizedBox(height: 12),
                        _buildCard(
                          icon: Icons.battery_std,
                          title: '电量',
                          value: '${telemetry.battery}%',
                          color: Colors.amber,
                        ),
                      ],
                    ),
                  ),
          ),
        );
      },
    );
  }

  Widget _buildCard({
    required IconData icon,
    required String title,
    required String value,
    required Color color,
  }) {
    return Card(
      child: ListTile(
        leading: Icon(icon, color: color, size: 32),
        title: Text(title),
        trailing: Text(
          value,
          style: const TextStyle(fontSize: 24, fontWeight: FontWeight.bold),
        ),
      ),
    );
  }
}
