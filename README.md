# ec-round2-260421312 · CVIR 电控二轮考核

四自由度舵机机械臂的 STM32 控制固件。本仓库按《CVIR 机甲大师 2026 电控组二轮考核任务书 V1.0》组织，
提交物包括源码、`.ioc` 工程描述、CMake 构建预设与 AI 使用记录。

## 1 环境与版本

| 项 | 版本 / 说明 |
|---|---|
| MCU | STM32F103C8T6（Cortex-M3，64 KB Flash / 20 KB RAM，LQFP48） |
| 开发板 | 通用 F103C8T6 最小系统板（"Blue Pill"），板载 PC13 LED |
| CubeMX | 6.18.1 |
| 固件包 | STM32Cube FW_F1 V1.8.7 |
| 编译器 | arm-none-eabi-gcc 14.3.1（STM32CubeCLT 随 VS Code 扩展安装） |
| CMake | 4.4.0 |
| Ninja | 1.13.2 |
| C 标准 | C17（`CMAKE_C_STANDARD 17`，见根 `CMakeLists.txt`） |
| 编译警告 | `-Wall -Wextra`，当前构建零警告 |
| 调试器 | ST-LINK/V2（SWD，Connect Under Reset） |

> 工具链绝对路径写在 `CMakePresets.json` 之外的 `cmake/gcc-arm-none-eabi.cmake` 中由 CubeMX 生成；
> 若本机未把 `arm-none-eabi-gcc` 加入 PATH，可通过环境变量 `CUBE_BUNDLE_PATH` 或设置 `PATH` 解决，
> 不得把本机绝对路径硬编码进预设（任务书 3.3）。

## 2 构建与烧录

````bash
# 调试构建（-O0 -g3）
cmake --preset Debug
cmake --build --preset Debug

# 发布构建（-O2），评审用的干净构建入口
cmake --preset Release
cmake --build --preset Release
````

产物：`build/<preset>/ec-round2-260421312.elf`

烧录（ST-LINK + SWD）：

````bash
STM32_Programmer_CLI.exe -c port=SWD mode=UR reset=HWrst \
  -d build/Debug/ec-round2-260421312.elf -v -rst
````

或在 VS Code 中按 STM32 扩展的 Build / Debug 按钮，或直接 `F5`。

## 3 接线表

### 3.1 当前已实现（T0）

| 信号 | MCU 引脚 | 外设 | 说明 |
|---|---|---|---|
| 板载蓝灯 | PC13 | GPIO 输出 PP | 灌电流接法，**低电平点亮** |
| 按键 | PA0 | GPIO 输入，内部上拉 | 接地即按下 |
| 调试串口 TX | PA9 | USART1_TX | 115200 8N1 |
| 调试串口 RX | PA10 | USART1_RX | 115200 8N1 |
| SWD | PA13 / PA14 | SWDIO / SWCLK | 下载与调试 |

### 3.2 规划中（T1–T5a，随进度更新）

| 信号 | MCU 引脚 | 外设 | 说明 |
|---|---|---|---|
| 底座 / 肩 / 肘 / 夹爪 PWM | PA6 / PA7 / PB0 / PB1 | TIM3 CH1–CH4 | 50 Hz，1 µs 分辨率 |
| MPU6050 SCL / SDA | PB6 / PB7 | I2C1 | 400 kHz |
| USB D- / D+ | PA11 / PA12 | USB Device FS | CDC，VID/PID = 0x0483/0x5740 |

## 4 已实现功能（T0 板级自检）

1. 上电经 USART1 打印自检信息：工程名、芯片型号、SYSCLK/HCLK/PCLK1/PCLK2、复位原因、编译时间；
2. 板载 PC13 蓝灯以 1 Hz 闪烁（500 ms 翻转半周期，非阻塞时间戳轮询，未使用 `HAL_Delay`）；
3. 按键（PA0 接地）翻转 LED 闪烁状态：闪烁 ↔ 常灭，串口同步打印事件；
4. 复位原因读取后立即 `__HAL_RCC_CLEAR_RESET_FLAGS()`，避免旧标志污染下一次判断。

## 5 协议实现说明

通讯协议见任务书附件一（V1.0）。实现进度：

| 模块 | 状态 | 文件 |
|---|---|---|
| CRC8（多项式 0x07） | 待实现 | — |
| 帧编解码（0xA5 帧头 + 类型 + 长度 + 载荷 + CRC） | 待实现 | — |
| 0x10 状态上报（19 字节） | 待实现 | — |
| 0x20 / 0x21 / 0x22 / 0x23 / 0x24 下行指令 | 待实现 | — |
| USB CDC 传输层 | 待实现 | — |

设计约定（与任务书 4.2 一致）：**协议解析代码只写一份**，CDC 与 Custom HID 各自只实现
"发送一帧 / 轮询收一帧"的传输层接口，不复制解析逻辑。

## 6 AI 使用记录

见仓库根目录 [AI.MD](AI.MD)。所有涉及 AI 辅助的提交在 commit message 末尾带 `AI:` 与 `Verified:` 行。

## 7 演示视频

待录制（验收方式另行通知后补充链接）。

## 8 采购与成本

见 `COST.md`（采购清单与实际花销，含试错与返工支出）。
