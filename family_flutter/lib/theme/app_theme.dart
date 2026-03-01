import 'package:flutter/material.dart';
import 'package:google_fonts/google_fonts.dart';
import 'app_colors.dart';

/// 构建应用主题
ThemeData buildAppTheme() {
  return ThemeData(
    colorScheme: ColorScheme.fromSeed(
      seedColor: AppColors.primary,
      brightness: Brightness.light,
      error: AppColors.abnormal,
    ),
    useMaterial3: true,
    scaffoldBackgroundColor: AppColors.surface,
    textTheme: GoogleFonts.notoSansScTextTheme().copyWith(
      headlineMedium: GoogleFonts.notoSansSc(
        fontWeight: FontWeight.w700,
      ),
      headlineSmall: GoogleFonts.notoSansSc(
        fontWeight: FontWeight.w700,
      ),
      titleMedium: GoogleFonts.notoSansSc(
        fontWeight: FontWeight.w600,
      ),
    ),
    cardTheme: CardThemeData(
      elevation: 1.5,
      shadowColor: Colors.black.withValues(alpha: 0.08),
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(16),
      ),
      color: Colors.white,
      surfaceTintColor: Colors.transparent,
    ),
    navigationBarTheme: NavigationBarThemeData(
      indicatorColor: AppColors.primary.withValues(alpha: 0.12),
      backgroundColor: Colors.white,
      surfaceTintColor: Colors.transparent,
    ),
    pageTransitionsTheme: const PageTransitionsTheme(
      builders: {
        TargetPlatform.android: FadeUpwardsPageTransitionsBuilder(),
        TargetPlatform.iOS: CupertinoPageTransitionsBuilder(),
      },
    ),
  );
}
