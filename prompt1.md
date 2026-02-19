# 迭代 3.2 + 3.3 团队开发任务                                                                                                              

  ## 一、项目背景与当前状态

  本项目为 ESP32-S3 智能陪护手环，基于 ESP-IDF v5.2.3，目标芯片 ESP32-S3。
  当前已完成迭代 1.1~3.1（驱动层、服务层、BLE、报警状态机），进度详见 PROGRESS.md。

  ### 当前已就绪的基础设施
  1. **SPIFFS 分区已配置**：partitions.csv 中 `storage` 分区 4MB（offset=0x610000）
  2. **WAV 音频文件已生成**：`spiffs_data/` 下 31 个文件（16kHz/16-bit/Mono），共约 1.05MB
     - 报警语：alarm_help/fall/temp_high/temp_low/hr_high/hr_low/spo2_low.wav
     - 语音反馈：wakeup_ok/cmd_not_recognized/call_family.wav
     - 数值前缀：prefix_hr/steps/temp/spo2.wav
     - 单位：unit_bpm/steps/degree/percent.wav
     - 数字：num_0~9/10/100/dot.wav
  3. **事件总线**：event_bus.h 已定义 EVT_VOICE_CMD 事件类型（尚未使用）
  4. **报警管理器**：alarm_manager.c 已实现状态机（IDLE→PRE_ALARM→ALARMING→ACKED），
     目前仅控制 WS2812 灯光和 BLE Alarm Notify，**尚未集成音频播放**
  5. **传感器数据查询 API**：
     - `health_get_status()` → 返回 health_status_t（含 heart_rate, spo2, temperature）
     - `pedometer_get_steps()` → 返回 uint32_t 步数
     - `sensor_get_latest()` → 返回 sensor_data_t
  6. **项目不使用 idf_component.yml**（组件管理器，声明 esp-sr 依赖除外），其他全部基于 CMakeLists.txt

  ### 关键技术决策（已确认）
  - **I2S 外设分配**：使用**单个 I2S 外设**（I2S_NUM_0），运行时在 TX（播放）和 RX（录音）模式间切换，信号量互斥
  - **I2S API**：使用 ESP-IDF v5.x **新版 I2S API**（`driver/i2s_std.h`）
  - **ESP-SR 引入方式**：通过创建 `main/idf_component.yml` 使用 ESP-IDF 组件管理器引入
  - **开发顺序**：迭代 3.2 先行，迭代 3.3 依赖 3.2 的播放能力

  ### 硬件引脚
  | 外设 | 引脚 |
  |------|------|
  | MAX98357A 扬声器 | BCLK=GPIO17, LRCK=GPIO16, DIN=GPIO18 |
  | INMP441 麦克风 | SCK=GPIO17, WS=GPIO16, SD=GPIO15 |
  | 注意 | 扬声器和麦克风共用 GPIO17(SCK/BCLK) 和 GPIO16(WS/LRC)，必须分时复用 |

  ---

  ## 二、团队结构与职责边界

  ### 角色定义

  | 角色 | 名称 | 职责范围 | 不做（边界） |
  |------|------|---------|-------------|
  | **团队负责人** | team-lead | 任务拆分、子任务分配、代码审查协调、进度追踪、PROGRESS.md/ISSUE.md 更新 | 不直接写业务代码 |
  | **音频开发** | developer-audio | 迭代 3.2 全部代码：I2S 驱动、SPIFFS 挂载、WAV 解码、TTS 拼接引擎、I2S 资源互斥框架 | 不修改 ESP-SR        
  相关代码、不修改 voice_cmd 模块 |
  | **语音开发** | developer-voice | 迭代 3.3 全部代码：ESP-SR 集成、INMP441 麦克风驱动、语音命令回调、I2S 切换（录音侧） | 不修改 WAV
  播放逻辑、不修改 alarm_manager 核心状态机 |
  | **测试员** | tester | 每个子任务完成后进行代码审查：API 签名一致性、内存安全、线程安全、编译验证 | 不写功能代码 |

  ### 协作规则
  1. 所有子任务遵守 CLAUDE.md 规则：≤3 文件/子任务，方案先行，编译验证
  2. developer-audio 先完成 3.2 全部子任务后，developer-voice 再开始 3.3
  3. I2S 资源互斥接口由 developer-audio 在 3.2 中设计（提供 acquire/release API），developer-voice 在 3.3 中调用
  4. 公共头文件（event_bus.h）的修改由 team-lead 协调，避免冲突
  5. 每个子任务完成后必须验证编译通过

  ---

  ## 三、迭代 3.2 子任务分解（I2S 音频播放）

  ### 子任务 3.2-A：SPIFFS 挂载 + I2S 扬声器驱动 + WAV 解码播放
  **负责人**: developer-audio
  **修改/新增文件**（3 个）:
  1. `components/drivers/audio/include/audio_player.h`（新建）
  2. `components/drivers/audio/audio_player.c`（新建）
  3. `components/drivers/CMakeLists.txt`（修改：添加 audio 源文件和头文件路径）

  **实现内容**:
  - SPIFFS 挂载函数：`esp_err_t audio_spiffs_init(void)` — 挂载 `storage` 分区到 `/spiffs`
  - I2S TX 初始化（新版 API `i2s_new_channel` + `i2s_channel_init_std_mode`）：
    - 标准模式，采样率 16000Hz，16-bit，Mono
    - GPIO: BCLK=17, WS=16, DOUT=18
  - WAV 文件解码播放：`esp_err_t audio_play_wav(const char *filename)`
    - 从 SPIFFS 读取 WAV 文件，解析 44 字节标准 WAV 头
    - 通过 I2S DMA 缓冲区流式播放（建议 DMA buf_count=4, buf_size=1024）
    - 同步阻塞式播放（函数返回时播放完毕）
  - I2S 资源互斥框架：
    - `esp_err_t audio_i2s_acquire(i2s_mode_t mode)` — 获取 I2S 使用权并配置为 TX 或 RX 模式
    - `void audio_i2s_release(void)` — 释放 I2S 使用权
    - 内部使用 FreeRTOS Mutex 保护
  - `bool audio_is_playing(void)` — 查询播放状态

  **audio_player.h 对外接口**:
  ```c
  esp_err_t audio_player_init(void);       // SPIFFS 挂载 + I2S 初始化
  esp_err_t audio_play_wav(const char *filename);  // 播放 /spiffs/{filename}.wav
  void audio_play_stop(void);              // 停止当前播放
  bool audio_is_playing(void);             // 是否正在播放
  esp_err_t audio_i2s_acquire_tx(void);    // 获取 I2S 用于播放
  esp_err_t audio_i2s_acquire_rx(void);    // 获取 I2S 用于录音（供 3.3 调用）
  void audio_i2s_release(void);            // 释放 I2S

  验收标准:
  - 编译通过
  - 可在 main.c 中临时调用 audio_play_wav("wakeup_ok") 测试喇叭发声

  ---
  子任务 3.2-B：数字拼接 TTS 引擎

  负责人: developer-audio
  修改/新增文件（2 个）:
  1. components/drivers/audio/include/simple_tts.h（新建）
  2. components/drivers/audio/simple_tts.c（新建）

  实现内容:
  - 数值语音播报函数：
    - esp_err_t tts_speak_heart_rate(uint8_t bpm) → 播放 "当前心率为" + 数字 + "次每分钟"
    - esp_err_t tts_speak_spo2(uint8_t percent) → 播放 "当前血氧为" + 数字 + "百分之"
    - esp_err_t tts_speak_steps(uint32_t steps) → 播放 "当前步数为" + 数字 + "步"
    - esp_err_t tts_speak_temperature(float temp) → 播放 "当前环境温度为" + 数字 + "点" + 数字 + "摄氏度"
  - 内部数字分解函数：将整数分解为个位数字序列，依次播放对应 WAV
    - 例：75 → "七" + "十" + "五"；120 → "一" + "百" + "二" + "十"
  - 调用 audio_play_wav() 逐段播放，段间无需额外间隔（WAV 首尾已去静音）

  simple_tts.h 对外接口:
  esp_err_t tts_speak_heart_rate(uint8_t bpm);
  esp_err_t tts_speak_spo2(uint8_t percent);
  esp_err_t tts_speak_steps(uint32_t steps);
  esp_err_t tts_speak_temperature(float temp);
  esp_err_t tts_speak_number(int value);       // 通用数字播报

  验收标准:
  - 编译通过
  - 调用 tts_speak_heart_rate(75) 播放 "当前心率为七十五次每分钟"，连贯无明显断音

  ---
  子任务 3.2-C：报警管理器音频集成 + main.c 初始化

  负责人: developer-audio
  修改/新增文件（3 个）:
  1. components/services/alarm_manager/alarm_manager.c（修改）
  2. components/services/CMakeLists.txt（修改：alarm_manager 添加对 drivers 的 REQUIRES 依赖，如果尚未有的话通过 PRIV_REQUIRES 添加 audio      
  相关引用）
  3. main/main.c（修改：添加 audio_player_init 调用）

  实现内容:
  - alarm_manager.c 的 enter_state() 函数中：
    - PRE_ALARM 状态：播放 alarm_fall.wav（跌倒报警语）
    - ALARMING 状态：根据 alert_type_t 播放对应报警 WAV：
      - ALERT_TYPE_TEMP_HIGH → "alarm_temp_high"
      - ALERT_TYPE_TEMP_LOW → "alarm_temp_low"
      - ALERT_TYPE_HR_HIGH → "alarm_hr_high"
      - ALERT_TYPE_HR_LOW → "alarm_hr_low"
      - ALERT_TYPE_SPO2_LOW → "alarm_spo2_low"
      - ALERT_TYPE_MANUAL → "alarm_help"
      - ALERT_TYPE_FALL → "alarm_fall"（从 PRE_ALARM 升级时）
      - ALERT_TYPE_CALL_FAMILY → "call_family"
    - IDLE 状态：停止播放
  - 音频播放使用独立任务或非阻塞方式，不可阻塞状态机互斥锁
    - 建议：在 enter_state() 中通过 xTaskCreate 创建一次性播放任务，或使用事件通知独立音频任务
  - main/main.c：在 event_bus_init() 之后、alarm_manager_init() 之前调用 audio_player_init()

  注意事项:
  - alarm_manager.c 的 enter_state() 在持有 mutex 时调用，音频播放不能阻塞
  - 音频播放任务栈建议 4096 字节以上
  - audio_play_wav 是阻塞式的，所以需要在独立任务中调用

  验收标准:
  - 编译通过
  - 模拟跌倒 → 喇叭播放 "检测到跌倒，五秒后呼叫家人"
  - SW1 手动报警 → 喇叭播放 "我需要帮助"
  - IDLE 恢复后音频停止

  ---
  四、迭代 3.3 子任务分解（ESP-SR 语音识别）

  子任务 3.3-A：ESP-SR 组件引入 + INMP441 麦克风驱动

  负责人: developer-voice
  前置条件: 3.2 全部子任务已完成
  修改/新增文件（3 个）:
  1. main/idf_component.yml（新建：声明 esp-sr 依赖）
  2. components/services/voice_cmd/include/voice_cmd.h（新建）
  3. components/services/voice_cmd/voice_cmd.c（新建：框架 + 麦克风录音验证）

  实现内容:
  - main/idf_component.yml 内容:
  dependencies:
    espressif/esp-sr:
      version: "*"
  - voice_cmd.h 对外接口:
  esp_err_t voice_cmd_init(void);    // 初始化语音识别（麦克风 + ESP-SR）
  esp_err_t voice_cmd_start(void);   // 启动语音识别任务
  esp_err_t voice_cmd_stop(void);    // 停止语音识别任务
  - voice_cmd.c 框架:
    - 麦克风 I2S RX 配置（通过 audio_i2s_acquire_rx() 获取 I2S）：
        - 标准模式，采样率 16000Hz，16-bit，Mono
      - GPIO: BCLK=17, WS=16, DIN=15
    - 录音验证：读取麦克风数据并打印振幅，确认硬件工作

  注意事项:
  - 首次引入 idf_component.yml 后需要删除 build 目录重新构建（组件管理器会自动下载 esp-sr）
  - esp-sr 占用较大 PSRAM，需要在 sdkconfig 中确认 SPIRAM 已启用（当前已启用）
  - 此子任务重点是验证麦克风硬件和 esp-sr 编译通过，暂不实现完整识别逻辑

  验收标准:
  - idf.py build 编译通过（含 esp-sr 组件）
  - 串口打印麦克风采集到的音频振幅数据
  - 对着麦克风说话时振幅有明显变化

  ---
  子任务 3.3-B：唤醒词 + 命令词识别

  负责人: developer-voice
  修改/新增文件（2 个）:
  1. components/services/voice_cmd/voice_cmd.c（修改：添加 ESP-SR 识别逻辑）
  2. components/services/CMakeLists.txt（修改：添加 voice_cmd 源文件和头文件路径）

  实现内容:
  - ESP-SR 配置:
    - 唤醒词：使用内置 "Hi 乐鑫"（wn9_hilexin 或 ESP-IDF 5.2.3 中可用的版本）
    - 命令词（MultiNet）：
      - "救命" → 触发手动报警
      - "查询心率" → 播报当前心率
      - "查询步数" → 播报当前步数
      - "呼叫家人" → 发送呼叫通知
  - 语音识别任务（独立 FreeRTOS 任务，栈 8192+ 字节，运行在 PSRAM）:
    - 循环读取麦克风数据 → 送入 ESP-SR 引擎
    - 检测到唤醒词 → 播放 "wakeup_ok.wav"（"我在"） → 进入命令等待
    - 检测到命令词 → 通过事件总线发布 EVT_VOICE_CMD
    - 超时未识别 → 播放 "cmd_not_recognized.wav"（"没听清"）
  - I2S 资源互斥：
    - 语音播放时暂停语音识别（释放 I2S RX，获取 I2S TX）
    - 播放完成后恢复识别（释放 I2S TX，获取 I2S RX）
    - 使用 audio_player.h 的 acquire/release API
  - event_bus 集成：识别结果通过 EVT_VOICE_CMD 发布

  验收标准:
  - 编译通过
  - 距离 50cm 说 "Hi 乐鑫"，串口打印唤醒成功
  - 唤醒后说 "救命"，串口打印命令识别结果

  ---
  子任务 3.3-C：语音命令响应 + main.c 集成

  负责人: developer-voice
  修改/新增文件（3 个）:
  1. components/services/voice_cmd/voice_cmd.c（修改：添加命令响应逻辑）
  2. main/main.c（修改：添加 voice_cmd_init/start 调用 + EVT_VOICE_CMD 订阅）
  3. components/services/event_bus/include/event_bus.h（修改：在 event_data_t 联合体中添加 voice_cmd 数据类型，如果需要的话）

  实现内容:
  - 命令词响应逻辑（在 voice_cmd.c 内部或 main.c 事件回调中处理）：
    - "救命" → 调用 alarm_trigger(ALERT_TYPE_MANUAL, NULL)
    - "查询心率" → 调用 health_get_status() 获取心率 → tts_speak_heart_rate(hr)
    - "查询步数" → 调用 pedometer_get_steps() → tts_speak_steps(steps)
    - "呼叫家人" → 调用 alarm_trigger(ALERT_TYPE_CALL_FAMILY, NULL) + 播放 "call_family.wav"
  - event_bus.h 修改（如需要）：
    - 在 event_data_t 中添加语音命令数据字段（命令 ID 枚举）
  - main.c 初始化顺序：
    - 在 audio_player_init() 之后调用 voice_cmd_init()
    - 在所有服务启动后调用 voice_cmd_start()

  验收标准:
  - 编译通过
  - "Hi 乐鑫" → "我在" → "查询心率" → "当前心率为七十五次每分钟"
  - "Hi 乐鑫" → "我在" → "救命" → 触发报警状态机 + 声光报警
  - 语音播放期间不会出现 I2S 资源冲突

  ---
  五、构建编译方法

  在 Claude Code 中执行构建：
  powershell.exe -NoProfile -Command "Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue; Remove-Item Env:MINGW_PREFIX -ErrorAction
  SilentlyContinue; Remove-Item Env:MSYSTEM_PREFIX -ErrorAction SilentlyContinue; Remove-Item Env:MSYSTEM_CHOST -ErrorAction SilentlyContinue; 
  . 'D:\studying\Espressif\frameworks\esp-idf-v5.2.3\export.ps1'; cd 'F:\graduation_project\project\ESP32S3_Wristband_v3.0'; idf.py build 2>&1"

  引入 idf_component.yml 后首次构建需删除 build 目录：
  powershell.exe -NoProfile -Command "Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue; Remove-Item Env:MINGW_PREFIX -ErrorAction
  SilentlyContinue; Remove-Item Env:MSYSTEM_PREFIX -ErrorAction SilentlyContinue; Remove-Item Env:MSYSTEM_CHOST -ErrorAction SilentlyContinue; 
  . 'D:\studying\Espressif\frameworks\esp-idf-v5.2.3\export.ps1'; cd 'F:\graduation_project\project\ESP32S3_Wristband_v3.0'; Remove-Item       
  -Recurse -Force build -ErrorAction SilentlyContinue; idf.py build 2>&1"

  构建超时设置：600000ms（10 分钟），引入 esp-sr 后全量编译时间较长。

  ---
  六、关键约束提醒

  1. CLAUDE.md 规则：方案先行 → 用户批准 → 编码；每个子任务 ≤3 文件；调用 API 前必须 Read 头文件确认签名
  2. FreeRTOS 定时器回调：不可在定时器回调中调用音频播放（阻塞操作），需使用独立任务
  3. I2S 互斥：任何时刻只能有一个模块使用 I2S 外设（TX 播放 或 RX 录音）
  4. 内存预算：ESP-SR 约需 300-400KB PSRAM；音频 DMA 缓冲区建议放 PSRAM
  5. ISSUE.md：每个 bug 修复后必须记录到 ISSUE.md
  6. PROGRESS.md：每个子任务完成后必须更新 PROGRESS.md
  7. ESP-IDF v5.2.3 新版 I2S API：使用 driver/i2s_std.h，不使用已废弃的 driver/i2s.h

  ---
  七、执行顺序

  3.2-A (audio driver + SPIFFS + WAV) ──→ tester 审查
      ↓
  3.2-B (TTS 拼接引擎) ──→ tester 审查
      ↓
  3.2-C (alarm_manager 集成 + main.c) ──→ tester 审查
      ↓
  3.3-A (ESP-SR 引入 + 麦克风驱动) ──→ tester 审查
      ↓
  3.3-B (唤醒词 + 命令词识别) ──→ tester 审查
      ↓
  3.3-C (命令响应 + main.c 集成) ──→ tester 审查

  每个子任务完成后必须：编译通过 → tester 代码审查 → team-lead 确认 → 进入下一子任务。