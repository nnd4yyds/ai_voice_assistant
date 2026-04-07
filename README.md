# ESP32-S3 语音助手

基于 ESP32-S3 的智能语音助手，支持唤醒词检测、语音识别、大模型对话和语音合成。

## 特性

- **唤醒词检测**：基于 ESP-SR WakeNet 模型，唤醒词为"你好小智"
- **语音识别（ASR）**：集成百度语音识别 API，将语音转为文字
- **大模型对话**：集成智谱 GLM 大模型，支持多轮对话
- **语音合成（TTS）**：集成百度语音合成 API，将回复转为语音播放
- **WiFi 管理**：事件驱动 STA 连接，自动重连（最多 5 次）
- **FreeRTOS 多任务**：唤醒检测与语音处理并行运行

## 工作流程

```
┌─────────────────┐
│  等待唤醒词     │
│  "你好小智"     │
└────────┬────────┘
         │ 检测到唤醒词
         ▼
┌─────────────────┐
│  播放"我在"     │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  录音 5 秒      │
│  (16kHz PCM)    │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  百度 ASR       │
│  语音 → 文字    │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  智谱 GLM       │
│  对话处理       │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  百度 TTS       │
│  文字 → 语音    │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  扬声器播放     │
│  回复语音       │
└─────────────────┘
         │
         ▼
    返回等待唤醒词
```

## 硬件需求

### 开发板

- ESP32-S3（16MB Flash，DIO 模式，80MHz，8MB PSRAM）

### 引脚配置说明

**代码中的 GPIO 配置（`main/secrets.h`）：**
```c
#define I2S_MIC_BCK_IO     4     // BCK时钟，麦克风和扬声器共用
#define I2S_MIC_WS_IO      5     // WS字选择，麦克风和扬声器共用
#define I2S_MIC_DI_IO      6     // 麦克风数据输入
#define I2S_SPK_BCK_IO     4     // 与麦克风共用 BCK
#define I2S_SPK_WS_IO      5     // 与麦克风共用 WS
#define I2S_SPK_DO_IO      7     // 扬声器数据输出
```

### 麦克风（I2S 输入）

**推荐模块：INMP441**

| 信号 | GPIO | 说明 |
|------|------|------|
| SCK (BCLK) | GPIO 4 | 与扬声器共用时钟 |
| WS (LRC) | GPIO 5 | 与扬声器共用字选择 |
| SD (DIN) | GPIO 6 | 数据输入 |

**注意**：INMP441 输出 32 位 I2S 数据（24 位有效），程序会自动转换为 16 位 PCM。

### 扬声器（I2S 输出）

**推荐模块：MAX98357A**

| 信号 | GPIO | 说明 |
|------|------|------|
| BCLK | GPIO 4 | 与麦克风共用时钟 |
| LRC (WS) | GPIO 5 | 与扬声器共用字选择 |
| DIN (DOUT) | GPIO 7 | 数据输出 |

### 接线步骤

**步骤一：确认开发板引脚编号**
1. 查看你的 ESP32-S3 开发板型号
2. 在开发板文档中找到 GPIO 编号对应表
3. 确认以下 GPIO 引脚在开发板上的物理位置：
   - **GPIO 4**（BCK 时钟，共用）
   - **GPIO 5**（WS 字选择，共用）
   - **GPIO 6**（麦克风数据）
   - **GPIO 7**（扬声器数据）

**步骤二：连接硬件**
- 麦克风和扬声器共用 I2S 时钟线（BCLK 和 WS）
- 麦克风和扬声器的数据线（DIN/DOUT）分别连接到不同的 GPIO

### 音频参数

**麦克风配置：**
- 采样率：16000 Hz
- 位深：32 bit（INMP441 原始输出）
- 声道：单声道
- 输出格式：自动转换为 16 bit PCM

**扬声器配置：**
- 采样率：16000 Hz
- 位深：16 bit
- 声道：单声道

## 项目结构

```
├── main/
│   ├── main.c                  # 入口：初始化与任务创建
│   ├── secrets.h               # 凭据与引脚配置（已忽略）
│   ├── CMakeLists.txt          # 组件注册与依赖
│   ├── idf_component.yml       # 组件清单（esp-sr）
│   ├── api/
│   │   ├── zhipu_api.c/h       # 智谱 GLM API 客户端（对话）
│   │   └── baidu_api.c/h       # 百度语音识别与语音合成接口
│   ├── audio/
│   │   ├── i2s_audio.c/h       # I2S 麦克风与扬声器驱动
│   │   ├── wakeup.c/h          # ESP-SR 唤醒词检测任务
│   │   └── voice_handler.c/h   # 语音处理流程（录音→ASR→GLM→TTS）
│   └── wifi/
│       └── wifi_manager.c/h    # WiFi STA 连接管理
├── managed_components/
│   └── espressif__esp-sr/      # ESP-SR 组件（含 ESP-DSP 依赖）
└── build/                      # 编译输出
```

## 快速开始

### 1. 准备环境

```bash
# 设置 ESP-IDF 环境（以 v5.2.5 为例）
. $IDF_PATH/export.sh   # Linux/macOS
. $env:IDF_PATH/export.ps1  # Windows
```

### 2. 配置凭据

复制模板并填写你的配置信息：

```bash
cp main/secrets.h.example main/secrets.h
```

编辑 `main/secrets.h`，填入以下内容：

```c
// WiFi配置
#define WIFI_SSID "你的WiFi名称"
#define WIFI_PASSWORD "你的WiFi密码"

// 百度语音技术配置
#define BAIDU_API_KEY "你的百度API Key"
#define BAIDU_SECRET_KEY "你的百度Secret Key"

// 智谱AI (ChatGLM) 配置
#define ZHIPU_API_KEY "你的智谱API Key"
```

### 3. 编译与烧录

```bash
# 编译
idf.py build

# 烧录并监控串口输出（替换为你的端口号）
idf.py -p COM13 flash monitor
```

### 4. 使用说明

1. 设备启动后会打印 `Wakeup detection running...`
2. 对着麦克风说 **"你好小智"**（唤醒词）
3. 听到 **"我在"** 后，开始说话（5秒内）
4. 等待 GLM 处理并听到语音回复
5. 再次说唤醒词开始下一轮对话

## 架构说明

### FreeRTOS 任务

| 任务 | 功能 |
|------|------|
| `task_wakeup_detection` | 持续检测唤醒词"你好小智" |
| `task_serial_test` | 串口测试命令（chat、clear、quit） |

### 模块说明

| 模块 | 职责 |
|------|------|
| `wifi/wifi_manager` | WiFi STA 连接与状态管理 |
| `audio/i2s_audio` | I2S 麦克风录音（32位→16位转换）与扬声器播放（16位） |
| `audio/wakeup` | ESP-SR 唤醒词检测（WakeNet + AFE） |
| `audio/voice_handler` | 语音处理流程：录音 → ASR → GLM → TTS |
| `api/zhipu_api` | 智谱 GLM 大模型对话接口 |
| `api/baidu_api` | 百度语音识别与语音合成接口 |

### API 调用流程

```
用户语音 → INMP441麦克风 → I2S读取 → 16bit PCM
                                    ↓
                              百度ASR API
                                    ↓
                              文字内容
                                    ↓
                              智谱GLM API
                                    ↓
                              回复文字
                                    ↓
                              百度TTS API
                                    ↓
                              音频数据
                                    ↓
                              MAX98357A扬声器 → 用户听到回复
```

## 故障排除

| 问题 | 排查方法 |
|------|----------|
| WiFi 连接失败 | 检查 SSID/密码，确认信号强度 |
| 唤醒词无响应 | 检查麦克风 I2S 接线，确认说"你好小智" |
| ASR 转换失败 | 检查百度 API Key 是否有效，确认网络连接 |
| GLM 对话失败 | 检查智谱 API Key 是否有效 |
| TTS 播放无声 | 检查扬声器 I2S 接线（BCLK/WS/DOUT），确认供电 |
| 编译失败 | 运行 `idf.py fullclean` 后重新编译 |

## 依赖

| 组件 | 版本 | 来源 |
|------|------|------|
| ESP-IDF | 5.2.5 | 本地安装 |
| esp-sr | 1.9.5 | Espressif 组件注册表 |
| esp-dsp | 1.4.12 | esp-sr 传递依赖 |

## 许可证

本项目基于 ESP-IDF 框架开发，遵循 Espressif 相关开源协议。

## 技术说明

### INMP441 麦克风数据格式

INMP441 是一款 I2S 数字麦克风，输出格式为：
- **I2S 数据宽度**：32 位
- **有效数据**：24 位（存储在 32 位字中）
- **数据对齐**：MSB 对齐

**数据转换流程：**
1. I2S 驱动读取 32 位原始数据
2. 右移 16 位，提取高 16 位
3. 输出 16 位 PCM 格式用于语音识别 API

### I2S 引脚共享

本设计使用 INMP441（麦克风）和 MAX98357A（扬声器）共用 I2S 时钟线（BCLK 和 WS）：
- 两种模块都作为 I2S 从设备，主设备为 ESP32-S3
- 共享时钟可以减少 GPIO 使用，简化接线
- 麦克风和扬声器可以在不同的 I2S 通道（I2S_NUM_0 和 I2S_NUM_1）独立工作

### 唤醒词模型

使用 ESP-SR 提供的预训练唤醒词模型：
- 模型名称：`wn9_nihaoxiaozhi_tts`
- 唤醒词：**"你好小智"**

**自定义唤醒词：**

如需自定义唤醒词（如"嘬嘬嘬"），请参考：
https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/wake_word_engine/ESP_Wake_Words_Customization.html