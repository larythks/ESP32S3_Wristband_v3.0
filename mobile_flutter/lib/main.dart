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
          colorScheme: ColorScheme.fromSeed(
            seedColor: const Color(0xFF2D7DD2),
            brightness: Brightness.light,
            error: const Color(0xFFE63946),
          ),
          useMaterial3: true,
          cardTheme: CardThemeData(
            elevation: 1,
            shape: RoundedRectangleBorder(
              borderRadius: BorderRadius.circular(16),
            ),
            color: Colors.white,
            surfaceTintColor: Colors.transparent,
          ),
          scaffoldBackgroundColor: const Color(0xFFF8F9FB),
          navigationBarTheme: NavigationBarThemeData(
            indicatorColor: const Color(0xFF2D7DD2).withValues(alpha: 0.12),
            backgroundColor: Colors.white,
            surfaceTintColor: Colors.transparent,
          ),
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
