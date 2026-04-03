# ESP32-S3 语音助手

基于 ESP32-S3 的智能语音助手，支持本地唤醒词检测、云端语音识别（STT）、语音合成（TTS）以及文心大模型对话。

## 特性

- **本地唤醒词检测**：基于 ESP-SR 的 WakeNet 模型，支持 AEC/SE/VAD 音频前处理
- **语音识别（STT）**：对接百度 AI 开放平台，16kHz 中文普通话识别
- **语音合成（TTS）**：对接百度 AI 开放平台，中文语音播报
- **文心大模型对话**：集成百度千帆大模型服务，支持多轮对话
- **WiFi 管理**：事件驱动 STA 连接，自动重连（最多 5 次）
- **FreeRTOS 多任务**：唤醒检测、语音识别、语音合成并行运行

## 硬件需求

### 开发板

- ESP32-S3（4MB Flash，DIO 模式，80MHz）

### 麦克风（I2S 输入）

| 信号 | GPIO |
|------|------|
| BCLK | GPIO 41 |
| WS   | GPIO 42 |
| DIN  | GPIO 2  |

推荐模块：INMP441、SPH0645LM4H

### 扬声器（I2S 输出）

| 信号 | GPIO |
|------|------|
| BCLK | GPIO 17 |
| WS   | GPIO 18 |
| DOUT | GPIO 8  |

推荐模块：MAX98357A、UDA1334A

### 音频参数

- 采样率：16000 Hz
- 位深：16 bit
- 声道：单声道
- 音频缓冲区：80000 字节（约 5 秒）

## 项目结构

```
├── main/
│   ├── main.c                  # 入口：初始化与任务创建
│   ├── test_main.c             # 独立测试程序（WiFi + HTTP）
│   ├── secrets.h               # 凭据与引脚配置（已忽略）
│   ├── secrets.h.example       # secrets.h 模板
│   ├── CMakeLists.txt          # 组件注册与依赖
│   ├── idf_component.yml       # 组件清单（esp-sr）
│   ├── api/
│   │   ├── baidu_api.c/h       # 百度 API 客户端（STT/TTS/Token）
│   │   ├── ernie_api.c/h       # 文心大模型 API 客户端（对话）
│   ├── audio/
│   │   ├── i2s_audio.c/h       # I2S 麦克风与扬声器驱动
│   │   ├── wakeup.c/h          # ESP-SR 唤醒词检测任务
│   ├── tasks/
│   │   ├── app_tasks.c/h       # FreeRTOS 应用任务
│   └── wifi/
│       ├── wifi_manager.c/h    # WiFi STA 连接管理
├── managed_components/
│   └── espressif__esp-sr/      # ESP-SR 组件（含 ESP-DSP 依赖）
└── build/                      # 编译输出
```
├── main/
│   ├── main.c                  # 入口：初始化与任务创建
│   ├── test_main.c             # 独立测试程序（WiFi + HTTP）
│   ├── secrets.h               # 凭据与引脚配置（已忽略）
│   ├── secrets.h.example       # secrets.h 模板
│   ├── CMakeLists.txt          # 组件注册与依赖
│   ├── idf_component.yml       # 组件清单（esp-sr）
│   ├── api/
│   │   ├── baidu_api.c/h       # 百度 API 客户端（STT/TTS/Token）
│   ├── audio/
│   │   ├── i2s_audio.c/h       # I2S 麦克风与扬声器驱动
│   │   ├── wakeup.c/h          # ESP-SR 唤醒词检测任务
│   ├── tasks/
│   │   ├── app_tasks.c/h       # FreeRTOS 应用任务
│   └── wifi/
│       ├── wifi_manager.c/h    # WiFi STA 连接管理
├── managed_components/
│   └── espressif__esp-dsp/     # ESP-DSP（esp-sr 的传递依赖）
├── sdkconfig.defaults          # 默认配置覆盖
├── CMakeLists.txt              # 顶层 CMake
├── .vscode/                    # VSCode / ESP-IDF 插件配置
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

复制模板并填写你的 WiFi 和百度 API 信息：

```bash
cp main/secrets.h.example main/secrets.h
```

编辑 `main/secrets.h`，填入以下内容：

```c
#define WIFI_SSID "你的WiFi名称"
#define WIFI_PASSWORD "你的WiFi密码"
#define BAIDU_API_KEY "你的百度API Key"
#define BAIDU_SECRET_KEY "你的百度Secret Key"
#define BAIDU_APP_ID "你的百度App ID"
```

### 3. 获取百度 API 密钥

1. 访问 [百度 AI 开放平台](https://ai.baidu.com/)
2. 注册账号并创建语音技术应用
3. 在应用详情中获取 API Key、Secret Key 和 App ID
4. **开通千帆大模型服务**（用于文心对话）：
   - 访问 [百度千帆控制台](https://console.bce.baidu.com/qianfan/)
   - 开通服务并创建应用
   - 确保使用相同的 API Key 和 Secret Key

### 4. 编译与烧录

```bash
# 编译
idf.py build

# 烧录并监控串口输出（替换为你的端口号）
idf.py -p COM13 flash monitor
```

## 架构说明

### 数据流

```
麦克风 (I2S) ──→ [唤醒检测任务: ESP-SR AFE] ──→ 唤醒标志
                                                    │
                                                    ▼
                              [语音识别任务: 录制 ~3 秒音频]
                                                    │
                                                    ▼
                              [百度 STT API: 音频 → 文本]
                                                    │
                                                    ▼
                                              日志输出

[TTS 任务: 首次运行] ──→ 百度 TTS API ──→ "你好，我是ESP32语音助手" ──→ 扬声器 (I2S)

[语音对话流程]
用户文本输入 ──→ [文心大模型 API] ──→ 回复文本 ──→ [百度 TTS] ──→ 扬声器播放
```

### FreeRTOS 任务

| 任务 | 功能 |
|------|------|
| `task_main_loop` | 等待 WiFi 连接，获取百度访问令牌 |
| `task_wakeup_detection` | 持续运行 ESP-SR AFE 管道，检测唤醒词 |
| `task_speech_recognition` | 唤醒后录制音频并发送至百度 STT |
| `task_speech_synthesis` | 首次运行时播放欢迎语 |

### 模块说明

| 模块 | 职责 |
|------|------|
| `wifi/wifi_manager` | WiFi STA 连接与状态管理 |
| `audio/i2s_audio` | I2S 麦克风录音与扬声器播放 |
| `audio/wakeup` | ESP-SR 唤醒词检测（WakeNet + AFE） |
| `api/baidu_api` | 百度 OAuth2 认证、STT、TTS 接口封装 |
| `api/ernie_api` | 文心大模型对话接口（多轮对话） |
| `tasks/app_tasks` | 应用层 FreeRTOS 任务实现 |

## 百度 API 详情

| 接口 | 端点 | 参数 |
|------|------|------|
| OAuth2 Token | `aip.baidubce.com/oauth/2.0/token` | client_credentials |
| 语音识别 (STT) | `vop.baidu.com/server_api` | dev_pid=1537（中文普通话） |
| 语音合成 (TTS) | `tsn.baidu.com/text2audio` | 中文，MP3 (aue=4)，spd=5，pit=5，vol=5 |
| 文心大模型 | `aip.baidubce.com/rpc/2.0/ai_custom/v1/wenxinworkshop/chat/completions` | access_token |

## 使用示例

### 串口测试命令

连接串口（波特率 115200）后可使用以下命令：

```
token              # 获取/刷新百度 API Token（必须先执行）
你好世界           # TTS->STT 往返测试：文本 -> 语音 -> 识别
chat 你好          # 与文心大模型对话，自动 TTS 播放回复
clear              # 清空文心对话历史
quit               # 退出串口测试模式
```

### 完整对话流程

```c
// 用户文本 -> 文心大模型 -> TTS 播放
char response[2048];
char *reply = ernie_chat("你好", response, sizeof(response));
if (reply) {
    // 文心返回：你好！有什么我可以帮助你的吗？
    char *stt_result = baidu_text_to_speech_to_text(reply);  // TTS播放
    free(stt_result);
}

// 清空对话历史，开始新对话
ernie_clear_history();
```

## 已知限制

- 语音识别任务固定录制约 3 秒音频，无 VAD 端点检测
- 语音合成任务仅在首次运行时播放固定欢迎语
- 文心大模型对话需要百度千帆服务支持
- API 调用失败时无重试机制
- 内存有限，对话历史最多保留 10 轮

## 故障排除

| 问题 | 排查方法 |
|------|----------|
| WiFi 连接失败 | 检查 SSID/密码，确认信号强度 |
| 唤醒词无响应 | 检查麦克风 I2S 接线，确认 ESP-SR 库已安装 |
| 语音识别失败 | 验证百度 API 密钥，检查网络连接和音频格式 |
| 扬声器无声 | 检查 I2S 接线（BCLK/WS/DOUT），确认供电 |
| 编译失败 | 运行 `idf.py fullclean` 后重新编译 |

## 依赖

| 组件 | 版本 | 来源 |
|------|------|------|
| ESP-IDF | 5.2.5 | 本地安装 |
| esp-sr | 1.9.5 | Espressif 组件注册表 |
| esp-dsp | 1.4.12 | esp-sr 传递依赖 |

## 许可证

本项目基于 ESP-IDF 框架开发，遵循 Espressif 相关开源协议。
