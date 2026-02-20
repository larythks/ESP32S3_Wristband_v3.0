import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import 'data/ble_provider.dart';
import 'ui/scan_page.dart';
import 'ui/device_page.dart';

void main() {
  runApp(const CareBandApp());
}

class CareBandApp extends StatelessWidget {
  const CareBandApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MultiProvider(
      providers: [
        ChangeNotifierProvider(create: (_) => BleProvider()),
      ],
      child: MaterialApp(
        title: 'CareBand',
        theme: ThemeData(
          colorScheme: ColorScheme.fromSeed(seedColor: Colors.blue),
          useMaterial3: true,
        ),
        initialRoute: '/scan',
        routes: {
          '/scan': (context) => const ScanPage(),
          '/device': (context) => const DevicePage(),
        },
      ),
    );
  }
}
