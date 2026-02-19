"""
音频文件批量生成脚本
功能：
1. 使用 edge-tts 生成中文语音 (微软晓晓)
2. 使用 ffmpeg 转换为 ESP32 友好的 WAV 格式 (16kHz, 16bit, Mono)
3. 输出到 spiffs_data 目录

依赖：
pip install edge-tts
需要安装 ffmpeg 并添加到环境变量 PATH
"""

import asyncio
import os
import subprocess
import shutil

# 配置
VOICE = "zh-CN-XiaoxiaoNeural"  # 微软晓晓
RATE = "-15%"                    # 语速
OUTPUT_DIR = os.path.join(os.path.dirname(os.path.dirname(__file__)), "spiffs_data")
TEMP_DIR = os.path.join(os.path.dirname(__file__), "temp_audio")

# 音频文件清单
AUDIO_FILES = [
    # --- 1. 报警类 (固定语句) ---
    ("alarm_help",          "我需要帮助"),
    ("alarm_fall",          "检测到跌倒，五秒后呼叫家人"),
    ("alarm_temp_high",     "环境温度过高"),
    ("alarm_temp_low",      "环境温度过低"),
    ("alarm_hr_high",       "心率过高"),
    ("alarm_hr_low",        "心率过低"),
    ("alarm_spo2_low",      "血氧过低"),
    
    # --- 2. 语音识别反馈 (固定语句) ---
    ("wakeup_ok",           "我在"),
    ("cmd_not_recognized",  "没听清"),
    ("call_family",         "正在呼叫家人"),
    
    # --- 3. 数值播报前缀 ---
    ("prefix_hr",           "当前心率为"),
    ("prefix_steps",        "当前步数为"),
    ("prefix_temp",         "当前环境温度为"),
    ("prefix_spo2",         "当前血氧为"),
    ("prefix_time",         "当前时间是"),
    
    # --- 4. 单位 ---
    ("unit_bpm",            "次每分钟"),
    ("unit_steps",          "步"),
    ("unit_degree",         "摄氏度"),
    ("unit_percent",        "百分之"),
    ("time_dot",            "点"),  # 用于时间 "3点45"
    
    # --- 5. 数字 (0-10, 100) ---
    ("num_0",               "零"),
    ("num_1",               "一"),
    ("num_2",               "二"),
    ("num_3",               "三"),
    ("num_4",               "四"),
    ("num_5",               "五"),
    ("num_6",               "六"),
    ("num_7",               "七"),
    ("num_8",               "八"),
    ("num_9",               "九"),
    ("num_10",              "十"),
    ("num_100",             "百"),
    ("num_dot",             "点"),  # 用于体温 "37点5"
]

async def check_dependencies():
    """检查依赖工具"""
    # 检查 ffmpeg
    try:
        subprocess.run(["ffmpeg", "-version"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except FileNotFoundError:
        print("错误: 未找到 ffmpeg。请安装 ffmpeg 并将其添加到系统 PATH 中。")
        return False
    return True

async def generate_one(name: str, text: str):
    """生成单个音频文件"""
    mp3_path = os.path.join(TEMP_DIR, f"{name}.mp3")
    wav_path = os.path.join(OUTPUT_DIR, f"{name}.wav")

    # 1. edge-tts 生成 MP3
    import edge_tts
    communicate = edge_tts.Communicate(text, VOICE, rate=RATE)
    await communicate.save(mp3_path)
    
    # 2. ffmpeg 转换格式
    # -ar 16000: 采样率 16kHz
    # -ac 1: 单声道
    # -sample_fmt s16: 16位深度
    # silenceremove: 移除首尾静音
    cmd = [
        "ffmpeg", "-y", "-v", "error",
        "-i", mp3_path,
        "-ar", "16000",
        "-ac", "1",
        "-sample_fmt", "s16",
        "-af", "silenceremove=start_periods=1:start_silence=0.1:start_threshold=-50dB,areverse,silenceremove=start_periods=1:start_silence=0.1:start_threshold=-50dB,areverse",
        wav_path
    ]
    
    subprocess.run(cmd, check=True)
    
    size = os.path.getsize(wav_path)
    print(f"生成: {name:<20} | 内容: {text:<10} | 大小: {size/1024:.1f} KB")

async def main():
    if not await check_dependencies():
        return

    # 创建目录
    if os.path.exists(TEMP_DIR):
        shutil.rmtree(TEMP_DIR)
    os.makedirs(TEMP_DIR)
    
    if not os.path.exists(OUTPUT_DIR):
        os.makedirs(OUTPUT_DIR)
        
    print(f"=== 开始生成音频文件 ===")
    print(f"语音: {VOICE}")
    print(f"输出: {OUTPUT_DIR}")
    print("-" * 50)

    try:
        tasks = []
        for name, text in AUDIO_FILES:
            tasks.append(generate_one(name, text))
        
        await asyncio.gather(*tasks)
        
        print("-" * 50)
        print(f"完成! 共生成 {len(AUDIO_FILES)} 个文件。")
        
        # 统计总大小
        total_size = sum(os.path.getsize(os.path.join(OUTPUT_DIR, f)) for f in os.listdir(OUTPUT_DIR) if f.endswith('.wav'))
        print(f"总大小: {total_size/1024:.2f} KB ({total_size/1024/1024:.2f} MB)")
        
    except Exception as e:
        print(f"\n发生错误: {e}")
    finally:
        # 清理临时文件
        if os.path.exists(TEMP_DIR):
            shutil.rmtree(TEMP_DIR)

if __name__ == "__main__":
    asyncio.run(main())
