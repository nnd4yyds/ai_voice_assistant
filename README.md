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
| `audio/i2s_audio` | I2S 麦克风录音与扬声器播放 |
| `audio/wakeup` | ESP-SR 唤醒词检测（WakeNet + AFE） |
| `api/zhipu_api` | 智谱 GLM 大模型对话接口 |
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
