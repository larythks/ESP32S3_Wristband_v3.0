import 'dart:async';
import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../../data/ble_provider.dart';
import '../widgets/health_card.dart';
import '../widgets/trend_chart.dart';

class DashboardTab extends StatefulWidget {
  const DashboardTab({super.key});

  @override
  State<DashboardTab> createState() => _DashboardTabState();
}

class _DashboardTabState extends State<DashboardTab> {
  TrendMetric _selectedMetric = TrendMetric.heartRate;
  bool _isMeasuring = false;
  int _countdown = 0;
  Timer? _timer;

  static const Color _primaryColor = Color(0xFF2D7DD2);
  static const Color _tempColor = Color(0xFFF4A261);
  static const Color _hrColor = Color(0xFFE63946);
  static const Color _spo2Color = Color(0xFF457B9D);
  static const Color _stepsColor = Color(0xFF45B7A0);

  void _startMeasure(BleProvider ble) {
    if (_isMeasuring) return;

    ble.manualMeasure(start: true, durationSec: 15);

    setState(() {
      _isMeasuring = true;
      _countdown = 15;
    });

    _timer?.cancel();
    _timer = Timer.periodic(const Duration(seconds: 1), (timer) {
      if (!mounted) {
        timer.cancel();
        return;
      }
      setState(() {
        _countdown--;
      });
      if (_countdown <= 0) {
        timer.cancel();
        setState(() {
          _isMeasuring = false;
        });
      }
    });
  }

  @override
  void dispose() {
    _timer?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Consumer<BleProvider>(
      builder: (context, ble, _) {
        final data = ble.latestTelemetry;
        final connected = ble.isConnected;

        return Scaffold(
          backgroundColor: const Color(0xFFF8F9FB),
          appBar: AppBar(
            title: const Text('CareBand'),
            centerTitle: false,
            actions: [
              Padding(
                padding: const EdgeInsets.only(right: 12),
                child: Chip(
                  avatar: Icon(
                    Icons.circle,
                    size: 10,
                    color: connected
                        ? const Color(0xFF45B7A0)
                        : const Color(0xFF6C757D),
                  ),
                  label: Text(
                    connected ? '已连接' : '未连接',
                    style: TextStyle(
                      fontSize: 12,
                      color: connected
                          ? const Color(0xFF45B7A0)
                          : const Color(0xFF6C757D),
                    ),
                  ),
                  backgroundColor: connected
                      ? const Color(0xFF45B7A0).withValues(alpha: 0.12)
                      : const Color(0xFF6C757D).withValues(alpha: 0.12),
                  side: BorderSide.none,
                  padding: const EdgeInsets.symmetric(horizontal: 4),
                  materialTapTargetSize: MaterialTapTargetSize.shrinkWrap,
                ),
              ),
            ],
          ),
          body: SingleChildScrollView(
            padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                // --- Health Cards Grid ---
                GridView.count(
                  crossAxisCount: 2,
                  crossAxisSpacing: 12,
                  mainAxisSpacing: 12,
                  childAspectRatio: 1.0,
                  shrinkWrap: true,
                  physics: const NeverScrollableScrollPhysics(),
                  children: [
                    HealthCard(
                      title: '温度',
                      value: data != null
                          ? data.temperature.toStringAsFixed(1)
                          : '--',
                      unit: '\u00B0C',
                      icon: Icons.thermostat,
                      color: _tempColor,
                      isValid: data?.isTempValid ?? false,
                    ),
                    HealthCard(
                      title: '心率',
                      value: data != null
                          ? data.heartRate.toString()
                          : '--',
                      unit: 'bpm',
                      icon: Icons.favorite,
                      color: _hrColor,
                      isValid: data?.isHrValid ?? false,
                    ),
                    HealthCard(
                      title: '血氧',
                      value: data != null
                          ? data.spo2.toString()
                          : '--',
                      unit: '%',
                      icon: Icons.water_drop,
                      color: _spo2Color,
                      isValid: data?.isSpo2Valid ?? false,
                    ),
                    HealthCard(
                      title: '步数',
                      value: data != null
                          ? data.steps.toString()
                          : '--',
                      unit: '步',
                      icon: Icons.directions_walk,
                      color: _stepsColor,
                      isValid: data != null,
                    ),
                  ],
                ),
                const SizedBox(height: 16),

                // --- Trend Chart Section ---
                Card(
                  color: const Color(0xFFFFFFFF),
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
                        const Text(
                          '趋势图',
                          style: TextStyle(
                            fontSize: 16,
                            fontWeight: FontWeight.w600,
                            color: Color(0xFF212529),
                          ),
                        ),
                        const SizedBox(height: 12),
                        SizedBox(
                          width: double.infinity,
                          child: SegmentedButton<TrendMetric>(
                            segments: const [
                              ButtonSegment(
                                value: TrendMetric.heartRate,
                                label: Text('心率'),
                                icon: Icon(Icons.favorite, size: 16),
                              ),
                              ButtonSegment(
                                value: TrendMetric.spo2,
                                label: Text('血氧'),
                                icon: Icon(Icons.water_drop, size: 16),
                              ),
                              ButtonSegment(
                                value: TrendMetric.temperature,
                                label: Text('温度'),
                                icon: Icon(Icons.thermostat, size: 16),
                              ),
                            ],
                            selected: {_selectedMetric},
                            onSelectionChanged: (selection) {
                              setState(() {
                                _selectedMetric = selection.first;
                              });
                            },
                            style: ButtonStyle(
                              visualDensity: VisualDensity.compact,
                              textStyle: WidgetStatePropertyAll(
                                const TextStyle(fontSize: 13),
                              ),
                            ),
                          ),
                        ),
                        const SizedBox(height: 16),
                        TrendChart(
                          history: ble.telemetryHistory,
                          selectedMetric: _selectedMetric,
                        ),
                      ],
                    ),
                  ),
                ),
                const SizedBox(height: 16),

                // --- Manual Measure Button ---
                FilledButton.tonal(
                  onPressed: _isMeasuring ? null : () => _startMeasure(ble),
                  style: FilledButton.styleFrom(
                    backgroundColor: _primaryColor.withValues(alpha: 0.12),
                    foregroundColor: _primaryColor,
                    disabledBackgroundColor:
                        _primaryColor.withValues(alpha: 0.06),
                    disabledForegroundColor:
                        _primaryColor.withValues(alpha: 0.4),
                    padding: const EdgeInsets.symmetric(vertical: 14),
                    shape: RoundedRectangleBorder(
                      borderRadius: BorderRadius.circular(12),
                    ),
                  ),
                  child: _isMeasuring
                      ? Row(
                          mainAxisAlignment: MainAxisAlignment.center,
                          children: [
                            SizedBox(
                              width: 18,
                              height: 18,
                              child: CircularProgressIndicator.adaptive(
                                strokeWidth: 2,
                              ),
                            ),
                            const SizedBox(width: 10),
                            Text(
                              '测量中 (${_countdown}s)',
                              style: const TextStyle(
                                fontSize: 15,
                                fontWeight: FontWeight.w600,
                              ),
                            ),
                          ],
                        )
                      : const Row(
                          mainAxisAlignment: MainAxisAlignment.center,
                          children: [
                            Icon(Icons.play_arrow, size: 20),
                            SizedBox(width: 6),
                            Text(
                              '手动测量',
                              style: TextStyle(
                                fontSize: 15,
                                fontWeight: FontWeight.w600,
                              ),
                            ),
                          ],
                        ),
                ),
                const SizedBox(height: 24),
              ],
            ),
          ),
        );
      },
    );
  }
}
