# ISSUE 日志

---
### ISSUE-001
- **发现日期**: 2026-02-20
- **原因**: 默认生成的 `test/widget_test.dart` 引用了 `MyApp` 类，但项目主入口类已重命名为 `CareBandApp`，导致 `flutter analyze` 报错 `The name 'MyApp' isn't a class`
- **后果**: `flutter analyze` 失败，无法通过静态分析检查
- **解决方案**: 将 `widget_test.dart` 中的 `MyApp` 替换为 `CareBandApp`，并将测试内容更新为验证应用可正常实例化
- **涉及文件**: `test/widget_test.dart`

---
### 代码审查结果 (2026-02-20)

针对以下方面进行了全面审查，未发现其他问题:

1. **BLE UUID**: 5 个特征 UUID (Service FF00, Telemetry FF01, Alarm FF02, Command FF03, Status FF04) 与固件定义完全一致
2. **字节偏移量**:
   - Telemetry (20B): temp@0(int16), hr@2(uint8), spo2@3(uint8), steps@4(uint32), battery@8(uint8), dataValid@9(uint8), timestamp@10(uint32) -- 全部正确
   - Alarm (16B): eventId@0(uint32), alarmType@4(uint8), value@5(int16), battery@7(uint8), timestamp@8(uint32) -- 全部正确
   - Status (3B): deviceState@0, bleConnCount@1, alarmState@2 -- 全部正确
3. **字节序**: 所有多字节字段均使用 `Endian.little`，与固件一致
4. **data_valid bitmap**: 温度检查 bit 0 (0x01)，心率和血氧共用 bit 1 (0x02) -- 正确
5. **命令构造**: ACK=9B, SYNC_TIME=9B, REQUEST_REPORT=5B, MANUAL_MEASURE=7B -- 长度和结构均正确
6. **测试覆盖**: 63 个测试全部通过，覆盖了解析器、命令构造器和数据模型
