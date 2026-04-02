# ESP32-S3 语音助手项目

这是一个基于ESP32-S3的语音助手项目，支持百度语音识别和语音合成。

## 硬件推荐

### 1. 麦克风模块推荐
- **INMP441**：数字麦克风，I2S接口，高灵敏度，低噪声
- **SPH0645LM4H**：数字麦克风，I2S接口，低功耗
- **MSM261S4030H0R**：模拟麦克风，需要ADC或外接ADC

### 2. 扬声器模块推荐
- **MAX98357A**：I2S数字音频放大器，3W输出，直接驱动扬声器
- **PCM5102A**：I2S DAC模块，高音质，需要外接放大器
- **UDA1334A**：I2S DAC模块，低功耗

### 3. 一体化模块推荐
- **ESP32-S3-DevKitC-1**：官方开发板，需要外接音频模块
- **ESP32-S3-WROOM-1**：模组，需要自己设计音频电路
- **M5Stack Core2**：集成扬声器和麦克风，但需要确认I2S接口

## 硬件连接

### INMP441 麦克风连接
```
INMP441    ESP32-S3
VDD   ->   3.3V
GND   ->   GND
SCK   ->   GPIO41 (I2S_MIC_BCK_IO)
WS    ->   GPIO42 (I2S_MIC_WS_IO)
SD    ->   GPIO2  (I2S_MIC_DI_IO)
L/R   ->   GND (左声道) 或 VDD (右声道)
```

### MAX98357A 扬声器连接
```
MAX98357A  ESP32-S3
VIN   ->   5V
GND   ->   GND
BCLK  ->   GPIO17 (I2S_SPK_BCK_IO)
LRC   ->   GPIO18 (I2S_SPK_WS_IO)
DIN   ->   GPIO8  (I2S_SPK_DO_IO)
GAIN  ->   悬空或接电阻设置增益
```

## 百度AI配置

### 1. 注册百度AI开放平台
- 访问：https://ai.baidu.com/
- 注册账号并创建应用

### 2. 获取API密钥
- 在控制台创建语音识别应用
- 获取API Key和Secret Key
- 获取App ID

### 3. 配置代码
在main.c中修改以下配置：
```c
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define BAIDU_API_KEY "YOUR_API_KEY"
#define BAIDU_SECRET_KEY "YOUR_SECRET_KEY"
#define BAIDU_APP_ID "YOUR_APP_ID"
```

## 编译和烧录

### 1. 设置ESP-IDF环境
```bash
# Windows
export IDF_PATH="C:/esp/esp-idf"
. $IDF_PATH/export.ps1

# Linux/Mac
export IDF_PATH="/path/to/esp-idf"
source $IDF_PATH/export.sh
```

### 2. 安装ESP-SR库
```bash
# 在项目目录下执行
idf.py add-dependency "espressif/esp-sr"
```

### 3. 配置项目
```bash
idf.py menuconfig
```

### 4. 编译项目
```bash
idf.py build
```

### 5. 烧录和监控
```bash
idf.py -p COM3 flash monitor
```

## 功能说明

### 1. 语音识别
- 从麦克风录制音频
- 发送到百度语音识别API
- 获取识别结果

### 2. 语音合成
- 将文本发送到百度语音合成API
- 获取音频数据
- 通过扬声器播放

### 3. 唤醒词检测
- 使用ESP-SR库实现本地唤醒词检测
- 默认唤醒词："Hi ESP"
- 低功耗，实时检测
- 支持自定义唤醒词

### 4. 主逻辑
- 唤醒词检测
- 语音识别
- 结果处理
- 语音合成
- 播放回复

## 注意事项

1. **音频格式**：使用16kHz采样率，16位深度，单声道PCM格式
2. **网络连接**：需要稳定的WiFi连接
3. **API限制**：百度AI有调用次数限制，请注意使用量
4. **电源供应**：扬声器模块可能需要外部电源
5. **引脚配置**：根据实际硬件调整I2S引脚定义

## 扩展功能

### 1. 唤醒词检测（已实现）
使用ESP-SR库实现本地唤醒词检测：
- 低功耗，实时检测
- 支持自定义唤醒词
- 高精度识别

### 2. 自然语言处理
可以添加本地NLP处理，如：
- 关键词提取
- 意图识别
- 对话管理

### 3. 多语言支持
百度语音API支持多种语言：
- 中文
- 英文
- 粤语
- 四川话

## 故障排除

### 1. WiFi连接失败
- 检查SSID和密码
- 确认WiFi信号强度
- 检查防火墙设置

### 2. 语音识别失败
- 检查百度API密钥
- 确认网络连接
- 检查音频格式

### 3. 音频播放异常
- 检查I2S连接
- 确认引脚配置
- 检查电源供应

### 4. 唤醒词检测失败
- 检查麦克风连接
- 确认ESP-SR库安装
- 调整检测灵敏度
- 检查环境噪声

## 项目结构

```
PROJECT/
├── main/
│   ├── main.c          # 主程序（包含语音助手功能）
│   ├── test_main.c     # 测试程序（仅WiFi和HTTP测试）
│   └── CMakeLists.txt  # 组件配置
├── .vscode/
│   ├── settings.json   # VSCode设置
│   ├── c_cpp_properties.json  # C/C++配置
│   └── launch.json     # 调试配置
├── CMakeLists.txt      # 工程配置
├── README.md          # 项目说明
└── dependencies.lock  # 依赖锁定文件
```

## 测试版本说明

### 1. 测试程序功能
- WiFi连接测试
- HTTP客户端测试
- 不包含音频和唤醒词检测功能
- 适合验证基本网络功能

### 2. 切换测试程序
修改main/CMakeLists.txt文件，将test_main.c添加到源文件列表：
```cmake
idf_component_register(SRCS "test_main.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_wifi esp_event nvs_flash esp_netif esp_http_client)
```

### 3. 测试步骤
1. 修改WiFi配置
2. 编译烧录
3. 查看串口输出
4. 验证HTTP请求是否成功

## 下一步计划

1. 购买硬件模块（INMP441 + MAX98357A）
2. 注册百度AI开放平台账号
3. 配置WiFi和API密钥
4. 安装ESP-SR库（用于唤醒词检测）
5. 编译烧录测试
6. 根据实际效果优化代码

## 联系支持

如有问题，请检查：
1. ESP-IDF官方文档
2. 百度AI开发文档
3. 硬件模块数据手册