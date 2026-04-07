# ESP32-S3 语音助手

基于 ESP32-S3 的智能语音助手，支持本地唤醒词检测和智谱 GLM 大模型对话。

## 特性

- **本地唤醒词检测**：基于 ESP-SR 的 WakeNet 模型，支持 AEC/SE/VAD 音频前处理
- **智谱 GLM 对话**：集成智谱 AI GLM 大模型服务，支持多轮对话
- **WiFi 管理**：事件驱动 STA 连接，自动重连（最多 5 次）
- **FreeRTOS 多任务**：唤醒检测任务

## 硬件需求

### 开发板

- ESP32-S3（4MB Flash，DIO 模式，80MHz）

### 麦克风（I2S 输入）

**推荐模块：INMP441**

| 信号 | GPIO | 说明 |
|------|------|------|
| SCK (BCLK) | GPIO 26 | 与扬声器共用时钟 |
| WS (LRC) | GPIO 22 | 与扬声器共用字选择 |
| SD (DIN) | GPIO 34 | 数据输入 |

**注意**：INMP441 输出 32 位 I2S 数据（24 位有效），程序会自动转换为 16 位 PCM。

### 扬声器（I2S 输出）

**推荐模块：MAX98357A**

| 信号 | GPIO | 说明 |
|------|------|------|
| BCLK | GPIO 26 | 与麦克风共用时钟 |
| LRC (WS) | GPIO 22 | 与麦克风共用字选择 |
| DIN (DOUT) | GPIO 25 | 数据输出 |

### 接线参考

麦克风和扬声器共用 I2S 时钟线（BCLK 和 WS），这是常见的硬件设计方案，可以节省 GPIO 资源。

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
- 音频缓冲区：80000 字节（约 5 秒）

## 项目结构

```
├── main/
│   ├── main.c                  # 入口：初始化与任务创建
│   ├── secrets.h               # 凭据与引脚配置（已忽略）
│   ├── secrets.h.example       # secrets.h 模板
│   ├── CMakeLists.txt          # 组件注册与依赖
│   ├── idf_component.yml       # 组件清单（esp-sr）
│   ├── api/
│   │   ├── zhipu_api.c/h       # 智谱 GLM API 客户端（对话）
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

## 快速开始

### 1. 准备环境

```bash
# 设置 ESP-IDF 环境（以 v5.2.5 为例）
. $IDF_PATH/export.sh   # Linux/macOS
. $env:IDF_PATH/export.ps1  # Windows
```

### 2. 配置凭据

复制模板并填写你的 WiFi 信息：

```bash
cp main/secrets.h.example main/secrets.h
```

编辑 `main/secrets.h`，填入以下内容：

```c
#define WIFI_SSID "你的WiFi名称"
#define WIFI_PASSWORD "你的WiFi密码"
```

### 3. 编译与烧录

```bash
# 编译
idf.py build

# 烧录并监控串口输出（替换为你的端口号）
idf.py -p COM13 flash monitor
```

## 架构说明

### FreeRTOS 任务

| 任务 | 功能 |
|------|------|
| `task_main_loop` | 等待 WiFi 连接 |
| `task_wakeup_detection` | 持续运行 ESP-SR AFE 管道，检测唤醒词 |

### 模块说明

| 模块 | 职责 |
|------|------|
| `wifi/wifi_manager` | WiFi STA 连接与状态管理 |
| `audio/i2s_audio` | I2S 麦克风录音（32位→16位转换）与扬声器播放（16位） |
| `audio/wakeup` | ESP-SR 唤醒词检测（WakeNet + AFE） |
| `api/zhipu_api` | 智谱 GLM 大模型对话接口 |
| `api/baidu_api` | 百度语音识别与语音合成接口 |
| `tasks/app_tasks` | 应用层 FreeRTOS 任务实现 |

## 故障排除

| 问题 | 排查方法 |
|------|----------|
| WiFi 连接失败 | 检查 SSID/密码，确认信号强度 |
| 唤醒词无响应 | 检查麦克风 I2S 接线，确认 ESP-SR 库已安装 |
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

这种处理方式保留了原始音频的主要信息，同时兼容百度语音识别 API（要求 16 位 PCM）。

### I2S 引脚共享

本设计使用 INMP441（麦克风）和 MAX98357A（扬声器）共用 I2S 时钟线（BCLK 和 WS）：
- 两种模块都作为 I2S 从设备，主设备为 ESP32-S3
- 共享时钟可以减少 GPIO 使用，简化接线
- 麦克风和扬声器可以在不同的 I2S 通道（I2S_NUM_0 和 I2S_NUM_1）独立工作
