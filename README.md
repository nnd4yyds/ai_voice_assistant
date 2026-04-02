# FreeRTOS测试工程 - ESP32-S3

这是一个ESP32-S3的FreeRTOS测试工程。

## 功能

- 演示FreeRTOS任务创建
- 队列通信示例
- 互斥锁使用示例
- 周期性任务示例

## VSCode准备工作

### 1. 安装ESP-IDF扩展

在VSCode中安装 `ESP-IDF` 扩展：
1. 打开VSCode
2. 按 `Ctrl+Shift+X` 打开扩展面板
3. 搜索并安装 `Espressif IDF`

### 2. 安装ESP-IDF工具链

1. 按 `Ctrl+Shift+P` 打开命令面板
2. 输入 `ESP-IDF: Configure ESP-IDF Extension`
3. 选择 `EXPRESS` 安装方式
4. 选择ESP-IDF版本 (推荐 v5.3)
5. 等待安装完成

### 3. 配置工程

1. 打开工程文件夹
2. 修改 `.vscode/settings.json` 中的路径：
   - `idf.espIdfPath`: ESP-IDF安装路径
   - `idf.pythonInstallPath`: Python路径
   - `idf.port`: 串口端口号

### 4. 编译和烧录

1. 按 `Ctrl+Shift+P` 打开命令面板
2. 输入 `ESP-IDF: Build your project` 编译
3. 输入 `ESP-IDF: Flash your project` 烧录
4. 输入 `ESP-IDF: Monitor your device` 监视

## 工程结构

```
PROJECT/
├── main/
│   ├── main.c          # 主程序
│   └── CMakeLists.txt  # 组件配置
├── .vscode/
│   ├── settings.json   # VSCode设置
│   ├── c_cpp_properties.json  # C/C++配置
│   └── launch.json     # 调试配置
└── CMakeLists.txt      # 工程配置
```
