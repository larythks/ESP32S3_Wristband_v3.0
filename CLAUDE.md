# CLAUDE.md

本文件用于约束与指导 AI（Claude / ChatGPT / 其他大模型）在本仓库中的协作行为。  
所有 AI 在执行任何任务前，**必须完整阅读并严格遵守本文件内容**。

---

## 一、编码前置约束（Mandatory Before Coding）

### 1. 方案先行原则

在编写任何代码之前，**必须先输出完整的实现方案**，包括但不限于：

- 目标说明
- 模块/文件影响范围
- 实现思路
- 关键技术点或风险点

并且 **必须等待用户明确批准后，才能开始编写代码**。

🚫 未经批准直接输出代码，视为违反协作规则。

---

### 2. 需求不明确时的澄清义务

如果需求存在以下任一情况：

- 描述不完整
- 存在多种合理实现路径
- 与现有项目结构或约定可能冲突

**必须在写任何代码之前提出澄清问题**，并等待用户回复。

🚫 不允许基于“自行假设”的需求直接实现。

---

## 二、任务规模控制规则（Task Decomposition）

### 3. 文件修改数量限制

如果一项任务预计需要：

- 新增 / 修改 **超过 3 个文件**

则必须 **立刻停止编码行为**，并先执行以下步骤：

1. 将任务拆解为多个更小的子任务  
2. 明确每个子任务涉及的文件范围  
3. 等待用户确认拆分方案后，再逐个执行

🚫 禁止在单次任务中大范围修改多个文件。

---

## 三、持续纠错与规则进化机制（Critical）

### 4. 用户纠正 → 规则固化

**每当用户纠正 AI 的一次行为或决策之后：**

- AI 必须将该纠正总结为一条**新的明确规则**
- 并将其追加写入本 `CLAUDE.md` 文件中

规则应当：
- 表述清晰
- 可执行
- 可防止同类问题再次发生

📌 该机制用于让协作规则随着项目推进不断进化。

---

## 四、最高优先级说明

- 本文件中的规则 **优先级高于任何默认 AI 行为**
- 若本文件规则与 AI 的内置偏好冲突，**以本文件为准**
- 未在本文件中明确允许的行为，应默认视为 **不允许**

---

## 五、任务完成与进度追踪（Progress Tracking）

### 5. 任务完成后的进度记录义务

**每当完成一个用户明确的任务或子任务后：**

- AI 必须在 `PROGRESS.md` 文件中记录该任务的完成情况
- 如果 `PROGRESS.md` 不存在，则先创建该文件
- 记录内容应包括：
  - 完成日期（YYYY-MM-DD）
  - 任务简述
  - 对应开发计划中的迭代/模块（参考 `development_plan.md`）
  - 修改/新增的文件列表
  - 验收状态（待验收/已验收）
  - 遗留问题或备注（如有）

📌 该机制用于跟踪项目实际进度与计划的对应关系。

---

## 六、规则变更说明

- 本文件只允许在 **用户明确纠正或指示** 的情况下进行修改
- AI **不得自行删除、弱化或重写已有规则**

---

## 七、API 调用规范（API Usage Rules）

### 6. 调用现有函数前必须先查看其签名

**在调用项目中已有的函数/API 之前，必须：**

1. 先使用 Read 工具查看该函数的头文件声明
2. 确认函数的完整参数列表（参数个数、类型、顺序）
3. 确认函数的返回值类型

🚫 禁止凭记忆或假设直接调用函数，必须以实际代码中的声明为准。

**典型错误案例：**
- `sh1106_draw_string(x, y, str)` ❌ 缺少 `color` 参数
- `sh1106_draw_string(x, y, str, color)` ✅ 正确

📌 该规则用于防止因 API 参数不匹配导致的编译错误。

---

## 八、输出规范（Output Rules）

### 7. 分段输出原则

**在使用工具输出内容时，必须：**

1. 将大段代码或内容拆分为多次输出
2. 每次输出保持适度的内容量，避免单次输出过多
3. 对于新建文件，先创建基础框架，再分段添加具体实现

🚫 禁止一次性输出过大的代码块或文件内容。

📌 该规则用于提高输出的可读性和可控性，便于用户审查和 AI 自身的错误检查。

---

## 九、FreeRTOS 定时器回调规范（Timer Callback Rules）

### 8. 定时器回调中避免执行重量级操作

**在 FreeRTOS 软件定时器回调函数中，必须注意：**

1. 定时器回调运行在 Timer Service 任务上下文中
2. Timer Service 任务默认栈大小较小（ESP-IDF 默认 2048 字节）
3. 如果回调中调用了其他函数（如 UI 绘制、I2C 通信等），会导致栈溢出

**解决方案（任选其一）：**

- **方案 A**：增大 `CONFIG_FREERTOS_TIMER_TASK_STACK_DEPTH`（推荐 4096 或更大）
- **方案 B**：在回调中仅设置标志位/发送事件，由独立任务处理实际操作
- **方案 C**：使用 `xTaskCreate` 创建独立任务执行重量级操作

🚫 禁止在未评估栈使用的情况下，在定时器回调中直接调用复杂函数链。

**典型错误案例：**
```c
// ❌ 错误：定时器回调中直接调用 UI 绘制
static void timer_callback(TimerHandle_t timer) {
    ui_update();  // 可能调用 sh1106_clear -> I2C 操作 -> 栈溢出
}

// ✅ 正确：增大 Timer Service 栈，或使用事件通知
static void timer_callback(TimerHandle_t timer) {
    xEventGroupSetBits(event_group, UI_UPDATE_BIT);  // 通知其他任务处理
}
```

📌 该规则用于防止 FreeRTOS Timer Service 任务栈溢出导致系统重启。

---

## 十、服务层开发规范（Service Layer Rules）

### 9. 事件总线数据发布完整性

**在实现基于事件总线的服务时，必须确保：**

1. 采集数据后，必须通过 `event_publish()` 发布事件通知订阅者
2. 数据结构中的 `timestamp` 字段必须在每次采样时更新
3. 仅更新内部数据而不发布事件，会导致订阅者回调永远不被触发

🚫 禁止只更新内部数据缓存而忘记发布事件。

**典型错误案例：**
```c
// ❌ 错误：只更新了内部数据，没有发布事件
static void sensor_task(void *arg) {
    sample_imu();
    // 缺少: event_publish(EVT_SENSOR_DATA, &evt_data);
}

// ✅ 正确：采样后发布事件
static void sensor_task(void *arg) {
    sample_imu();
    s_ctx.latest_data.timestamp = get_timestamp_ms();  // 更新时间戳
    event_data_t evt_data;
    memcpy(&evt_data.sensor, &s_ctx.latest_data, sizeof(sensor_data_t));
    event_publish(EVT_SENSOR_DATA, &evt_data);  // 发布事件
}
```

📌 该规则用于确保事件驱动架构的数据流完整性。

---

### 10. 避免在采样任务中执行阻塞操作

**在实现传感器采样任务时，必须注意：**

1. 某些传感器（如 DS18B20）的读取操作需要较长等待时间（750ms）
2. 在采样任务中直接阻塞会影响其他传感器的采样频率
3. 应使用**异步状态机模式**将长时间操作拆分为非阻塞步骤

**异步采样状态机模式：**

```c
// 定义状态
typedef enum {
    STATE_IDLE,         // 空闲
    STATE_CONVERTING,   // 转换中
    STATE_READY         // 可读取
} sample_state_t;

// ❌ 错误：阻塞式采样
esp_err_t read_temp(float *temp) {
    start_convert();
    vTaskDelay(pdMS_TO_TICKS(750));  // 阻塞 750ms！
    return read_result(temp);
}

// ✅ 正确：异步状态机
void sample_async(uint32_t now) {
    switch (state) {
        case STATE_IDLE:
            if (time_to_sample(now)) {
                start_convert();
                state = STATE_CONVERTING;
                convert_start_time = now;
            }
            break;
        case STATE_CONVERTING:
            if ((now - convert_start_time) >= 750) {
                read_result(&temp);
                state = STATE_IDLE;
            }
            break;
    }
}
```

📌 该规则用于确保多传感器系统中各传感器的采样频率不受影响。

---

## 十一、ESP-IDF 组件结构规范（Component Structure Rules）

### 11. 新建组件目录时必须创建顶层 CMakeLists.txt

**在 `components/` 下新建子目录（如 `services/`、`middleware/` 等）时，必须：**

1. 在该子目录下创建 `CMakeLists.txt` 文件
2. 使用 `idf_component_register()` 注册所有源文件和头文件路径
3. 在 `main/CMakeLists.txt` 的 `REQUIRES` 中引用该组件名（目录名）

**ESP-IDF 组件发现机制：**
- ESP-IDF 会扫描 `components/` 下的每个子目录
- 如果子目录没有 `CMakeLists.txt`，该目录会被忽略，其中的子组件也不会被发现
- 错误信息：`Component directory ... does not contain a CMakeLists.txt file`

**典型错误案例：**
```
components/
├── drivers/
│   ├── CMakeLists.txt      ✅ 存在
│   ├── mpu6050/
│   └── ...
└── services/
    ├── event_bus/
    │   └── CMakeLists.txt  ❌ 无法被发现！
    └── sensor_service/
        └── CMakeLists.txt  ❌ 无法被发现！
    （缺少 services/CMakeLists.txt）
```

**正确结构：**
```
components/
└── services/
    ├── CMakeLists.txt      ✅ 必须存在！
    ├── event_bus/
    │   ├── include/
    │   └── event_bus.c
    └── sensor_service/
        ├── include/
        └── sensor_service.c
```

**services/CMakeLists.txt 示例：**
```cmake
idf_component_register(
    SRCS
        "event_bus/event_bus.c"
        "sensor_service/sensor_service.c"
    INCLUDE_DIRS
        "event_bus/include"
        "sensor_service/include"
    REQUIRES freertos driver
)
```

🚫 禁止在 `components/` 下创建新目录后不添加顶层 `CMakeLists.txt`。

📌 该规则用于防止 ESP-IDF 组件发现失败导致的编译错误。