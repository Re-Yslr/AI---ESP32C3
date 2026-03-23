# 机械AI仿生花 - Mechanical AI Bionic Flower

> 基于 xiaozhi-esp32 框架的毕业设计项目 | Graduation Design Project

## 项目简介

这是一个具有AI语音交互能力的机械仿生花项目。花朵可以根据AI对话状态自动开合，配合LED灯光效果，创造出富有生命力的交互体验。

**核心功能**：
- AI语音对话交互
- 花朵自动开合动画
- WS2812 RGB灯带氛围效果
- 3个物理按钮交互
- MCP协议智能设备控制

---

## 制作想法

### 设计理念

本项目试图探索"人机交互"的新形式——让AI不只是冰冷的屏幕对话，而是赋予它一个可以触摸、可以感知的物理实体。

**灵感来源**：
- 花朵的开合如同呼吸，具有自然的生命韵律
- AI"苏醒"时花朵绽放，"休眠"时花朵合拢
- 语音对话时花朵轻轻摇摆，仿佛在倾听和思考

### 工作流程

```
┌─────────────────────────────────────────────────────────┐
│                      状态流转                            │
├─────────────────────────────────────────────────────────┤
│                                                         │
│   [待命] ──唤醒词──> [聆听] ──识别完成──> [思考]        │
│     │                  │                  │             │
│   花朵合拢          花朵绽放            花朵摇摆         │
│   灯光常亮          灯光呼吸            灯光闪烁         │
│     │                  │                  │             │
│     └────────────────<┴────<──对话结束──<┘             │
│                                                         │
└─────────────────────────────────────────────────────────┘
```

---

## 目录结构

```
AI---ESP32C3/
│
├── main/
│   ├── boards/
│   │   ├── mechanical-flower/     # ← 本项目核心代码
│   │   │   ├── config.h           # 引脚配置
│   │   │   ├── config.json        # 板卡配置
│   │   │   ├── mechanical_flower_board.cc      # 板卡初始化
│   │   │   ├── mechanical_flower_controller.*  # 花朵控制器
│   │   │   ├── mechanical_flower_audio_codec.* # 音频编解码
│   │   │   ├── servo_controller.*              # 舵机控制
│   │   │   ├── flower_led_controller.*         # LED控制
│   │   │   └── flower_led.h       # LED驱动
│   │   │
│   │   └── common/                # 公共板卡依赖
│   │
│   ├── audio/                     # 音频处理模块
│   ├── display/                   # 显示模块
│   ├── led/                       # LED基础驱动
│   └── protocols/                 # 通信协议
│
├── stl/                           # ← 3D打印模型文件
│   ├── 花瓣.stl                   # 花瓣部件
│   ├── 树叶.stl                   # 叶子部件
│   ├── 花茎与花盆盖.stl           # 花茎和花盆盖
│   ├── 花盆内部.stl               # 花盆内部结构
│   ├── 齿轮.stl                   # 齿轮传动
│   ├── 齿条.stl                   # 齿条传动
│   ├── 连杆1.stl                  # 连杆组件1
│   └── 连杆2.stl                  # 连杆组件2
│
├── docs/                          # 项目文档
├── LICENSE                        # MIT协议 + 贡献者声明
└── CMakeLists.txt                 # 构建配置
```

---

## 代码路径说明

### 核心控制文件

| 文件 | 功能 |
|------|------|
| `main/boards/mechanical-flower/config.h` | GPIO引脚配置、舵机角度参数 |
| `main/boards/mechanical-flower/mechanical_flower_controller.cc` | 花朵动作控制核心逻辑 |
| `main/boards/mechanical-flower/servo_controller.cc` | SG90舵机PWM驱动 |
| `main/boards/mechanical-flower/flower_led_controller.cc` | WS2812灯带控制 |

### 关键功能

**1. 花朵动作控制** (`mechanical_flower_controller.cc`)
- `OpenFlower()` - 花朵绽放
- `CloseFlower()` - 花朵合拢
- `Wiggle()` - 花朵摇摆动画
- `WaterFlower()` - 浇水动作

**2. 舵机控制** (`servo_controller.cc`)
- 50Hz PWM信号生成
- 角度范围：0° ~ 48°
- 平滑移动动画

**3. LED控制** (`flower_led_controller.cc`)
- 颜色设置
- 呼吸效果
- 预设颜色方案

---

## 3D模型说明

### 模型文件位置
```
stl/
├── 花瓣.stl          # 3D打印 - 花瓣主体
├── 树叶.stl          # 3D打印 - 装饰叶子
├── 花茎与花盆盖.stl  # 3D打印 - 支撑结构
├── 花盆内部.stl      # 3D打印 - 电子元件安装座
├── 齿轮.stl         # 传动机构
├── 齿条.stl         # 传动机构
├── 连杆1.stl        # 花瓣驱动连杆
└── 连杆2.stl        # 花瓣驱动连杆
```

### 机械结构原理

```
                    ┌──────────┐
                    │  花瓣    │
                    └────┬─────┘
                         │ 连杆
              ┌──────────┴──────────┐
              │        齿轮         │
              └──────────┬──────────┘
                         │
              ┌──────────┴──────────┐
              │        齿条         │←── 舵机驱动
              └─────────────────────┘
                         │
              ┌──────────┴──────────┐
              │       花茎          │
              └─────────────────────┘
```

舵机通过齿轮齿条机构驱动连杆，连杆带动花瓣实现开合运动。

---

## 硬件连接

### 引脚分配表 (ESP32-C3)

| 模块 | ESP32引脚 | 模块引脚 | 说明 |
|------|----------|---------|------|
| **INMP441麦克风** | | | |
| | GPIO3 | BCLK/SCK | I2S位时钟 |
| | GPIO2 | LRCLK/WS | I2S左右声道时钟 |
| | GPIO10 | DIN/SD | I2S数据输入 |
| **MAX98357A功放** | | | |
| | GPIO3 | BCLK | I2S位时钟（共用） |
| | GPIO2 | LRC | I2S时钟（共用） |
| | GPIO13 | DIN | I2S数据输出 |
| **SG90舵机** | GPIO9 | 信号线 | PWM控制 (50Hz) |
| **WS2812灯带** | GPIO19 | DIN | 数据输入 |
| **按钮1** | GPIO0 | - | WiFi配网/蓝牙 |
| **按钮2** | GPIO1 | - | 打断对话 |
| **按钮3** | GPIO12 | - | 浇水功能 |

### 电源要求

- ESP32-C3: 3.3V
- INMP441: 3.3V（不可接5V）
- MAX98357A: 5V
- SG90舵机: 5V
- WS2812: 5V

---

## 编译与烧录

### 环境要求

- ESP-IDF v5.4+
- Python 3.8+
- Git

### 编译步骤

```bash
# 克隆仓库
git clone https://github.com/Re-Yslr/AI---ESP32C3.git
cd AI---ESP32C3

# 设置目标芯片
idf.py set-target esp32c3

# 编译
idf.py build

# 烧录
idf.py -p COM端口 flash monitor
```

### 配网

首次启动后，点击按钮1进入WiFi配网模式，或长按开启蓝牙配网。

---

## 技术栈

- **硬件**: ESP32-C3, INMP441, MAX98357A, SG90, WS2812
- **框架**: xiaozhi-esp32 (MIT License)
- **协议**: MCP (Model Context Protocol)
- **音频**: I2S全双工, OPUS编解码
- **AI后端**: 通义千问 / DeepSeek

---

## 致谢

本项目基于 [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) 开源框架开发。

- **Original Framework**: Shenzhen Xinzhi Future Technology Co., Ltd.
- **Project Contributor**: [Re-Yslr](https://github.com/Re-Yslr)

---

## License

MIT License - 详见 [LICENSE](LICENSE) 文件