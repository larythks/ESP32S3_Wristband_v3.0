import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../providers/device_provider.dart';
import '../theme/app_colors.dart';
import 'home_page.dart';

/// 设备绑定页面
///
/// 用户输入 device_id 绑定设备，成功后跳转到主页
class BindingPage extends StatefulWidget {
  const BindingPage({super.key});

  @override
  State<BindingPage> createState() => _BindingPageState();
}

class _BindingPageState extends State<BindingPage> {
  final _controller = TextEditingController();
  bool _isBinding = false;

  @override
  void dispose() {
    _controller.dispose();
    super.dispose();
  }

  Future<void> _onBind() async {
    final deviceId = _controller.text.trim();
    if (deviceId.isEmpty) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('请输入设备 ID')),
      );
      return;
    }

    setState(() => _isBinding = true);

    try {
      await context.read<DeviceProvider>().bindDevice(deviceId);

      if (!mounted) return;

      // 绑定成功，跳转到主页（替换当前路由）
      Navigator.of(context).pushReplacement(
        MaterialPageRoute(builder: (_) => const HomePage()),
      );
    } catch (e) {
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('绑定失败: $e')),
      );
    } finally {
      if (mounted) {
        setState(() => _isBinding = false);
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    return Semantics(
      label: 'CareBand 家属端设备绑定页面',
      child: Scaffold(
        backgroundColor: AppColors.surface,
        body: SafeArea(
          child: Padding(
            padding: const EdgeInsets.symmetric(horizontal: 32),
            child: Column(
              mainAxisAlignment: MainAxisAlignment.center,
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                // 圆形图标（与 mobile 风格统一）
                Center(
                  child: Container(
                    width: 96,
                    height: 96,
                    decoration: BoxDecoration(
                      color: AppColors.primary.withValues(alpha: 0.12),
                      shape: BoxShape.circle,
                    ),
                    child: const Icon(
                      Icons.watch,
                      size: 48,
                      color: AppColors.primary,
                    ),
                  ),
                ),
                const SizedBox(height: 28),

                // 标题
                const Text(
                  'CareBand 家属端',
                  textAlign: TextAlign.center,
                  style: TextStyle(
                    fontSize: 28,
                    fontWeight: FontWeight.w700,
                    color: AppColors.textPrimary,
                  ),
                ),
                const SizedBox(height: 8),
                const Text(
                  '请输入手环设备 ID 进行绑定',
                  textAlign: TextAlign.center,
                  style: TextStyle(
                    fontSize: 15,
                    color: AppColors.textSecondary,
                  ),
                ),
                const SizedBox(height: 40),

                // 输入框
                TextField(
                  controller: _controller,
                  decoration: InputDecoration(
                    labelText: '设备 ID',
                    hintText: '例如: CAREBAND_001',
                    prefixIcon: const Icon(Icons.devices),
                    border: OutlineInputBorder(
                      borderRadius: BorderRadius.circular(12),
                    ),
                    enabledBorder: OutlineInputBorder(
                      borderRadius: BorderRadius.circular(12),
                      borderSide: BorderSide(
                        color: AppColors.textSecondary.withValues(alpha: 0.3),
                      ),
                    ),
                    focusedBorder: OutlineInputBorder(
                      borderRadius: BorderRadius.circular(12),
                      borderSide: const BorderSide(
                        color: AppColors.primary,
                        width: 2,
                      ),
                    ),
                  ),
                  textInputAction: TextInputAction.done,
                  onSubmitted: (_) => _onBind(),
                ),
                const SizedBox(height: 24),

                // 绑定按钮
                FilledButton(
                  onPressed: _isBinding ? null : _onBind,
                  style: FilledButton.styleFrom(
                    padding: const EdgeInsets.symmetric(vertical: 16),
                    shape: RoundedRectangleBorder(
                      borderRadius: BorderRadius.circular(12),
                    ),
                  ),
                  child: _isBinding
                      ? const SizedBox(
                          width: 20,
                          height: 20,
                          child: CircularProgressIndicator(
                            strokeWidth: 2,
                            color: Colors.white,
                          ),
                        )
                      : const Text(
                          '绑定设备',
                          style: TextStyle(
                            fontSize: 16,
                            fontWeight: FontWeight.w600,
                          ),
                        ),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}
