import 'package:flutter_test/flutter_test.dart';

import 'package:mobile_flutter/main.dart';

void main() {
  testWidgets('CareBandApp can be instantiated', (WidgetTester tester) async {
    await tester.pumpWidget(const CareBandApp());
    // Verify the app starts and renders without crashing.
    expect(find.text('CareBand'), findsOneWidget);
  });
}
