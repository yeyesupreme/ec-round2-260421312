# AI 使用记录

> 依据《CVIR 电控二轮考核任务书》附件三：AI 使用记录规范。评审组通过 `git log --grep="^AI:"` 与本文逐条对照。

## 一、使用的工具

| 工具 | 版本 | 使用时段 |
|---|---|---|
| WorkBuddy（DeepSeek-V4.1-Flash） | — | 2026-11-10 起 |
| STM32CubeMX | 6.18.1 | 工程生成与重新生成 |
| STM32 VS Code Extension | 3.11.0 | 构建、烧录 |

## 二、使用记录

| # | 日期 | 涉及文件 / 模块 | 用途 | 是否采用 | 验证方式 | commit |
|---|---|---|---|---|---|---|
| 1 | 2026-11-10 | `ec-round2-260421312.ioc` | CubeMX 六步配置建议（SYS→Serial Wire、RCC→HSE、HCLK 72 MHz、USART1、PC13 输出、PA0 上拉输入），Toolchain 选 CMake | 采用 | Debug 编译通过，生成 elf（FLASH 5372 B / RAM 1696 B） | `4205dc6` |
| 2 | 2026-11-10 | `Core/Src/main.c` | 时间戳轮询闪烁框架（`HAL_GetTick` 差值），替代阻塞式 `HAL_Delay` | 采用 | 实机 1 Hz 稳定闪烁 | `e74d3ec` |
| 3 | 2026-11-11 | `Core/Src/main.c` | 按键三档切换（1 Hz / 5 Hz / 熄灭）：20 ms 电平稳定消抖、`g_key_latched` 单次触发、`Mode_Enter()` 重置相位 | **部分采用**：消抖与单次触发逻辑保留，档位切换已删除（理由见"未采用的部分"） | Debug 编译通过（FLASH 5820 B） | — |
| 4 | 2026-11-11 | `Core/Src/main.c` | 修正 `main()` 末尾缺失的收尾大括号（原为编译错误 `expected declaration or statement at end of input`） | 采用 | `ninja` 编译通过 | — |
| 5 | 2026-11-11 | `Core/Src/main.c` | T0 自检打印：工程名、SYSCLK/HCLK/PCLK1/PCLK2、复位原因（`RCC_FLAG_*` 顺序判定）、编译时间 | 采用 | 编译零警告；烧录校验通过；**串口输出待接 USB-TTL 后实机确认** | — |
| 6 | 2026-11-11 | `Core/Src/main.c` | `__io_putchar()` 重定向（覆盖 `syscalls.c` 中的 weak 声明），使 printf 走 USART1 | 采用 | 链接通过；Flash 11592 B / RAM 2096 B（Debug） | — |
| 7 | 2026-11-11 | `CMakeLists.txt` | C 标准 11→17、追加 `-Wall -Wextra`、Release 追加 `-O2`（均不改 `cmake/gcc-arm-none-eabi.cmake`） | 采用 | Debug 与 Release 双预设均构建成功，零警告（Release FLASH 8960 B） | — |
| 8 | 2026-11-11 | `README.md`、`COST.md` | README 结构与采购清单模板 | 采用 | 人工核对与任务书 5.1／5.3 条款一致 | — |
| 9 | 2026-11-13 | `Core/Src/main.c` | T1 单路舵机：`Servo_t` 结构体、参数宏段、角度→脉宽线性映射、每 20 ms 限速推进；删除按键与蓝灯闪烁逻辑 | 采用 | Debug 零警告（FLASH 16108 B）；烧录校验通过 | `abc88de` |
| 10 | 2026-11-13 | `.ioc`、`Core/Src/tim.c`、`Core/Src/main.c` | 迁移到 TIM3 四通道（PA6/PA7/PB0/PB1），由 CubeMX 脚本模式（`config load` + `project generate`）重新生成；主循环改为四路统一角度 | 采用 | Debug/Release 双预设零警告（FLASH 16396 B / 12216 B）；烧录校验通过；**四路实机待接满舵机后验证** | 本次提交 |

## 三、未采用的部分

| 方案 | 放弃原因 |
|---|---|
| 三档闪烁（1 Hz → 5 Hz → 熄灭）作为 T0 交付 | T0 验收判据仅为"LED 以 1 Hz 闪烁、按键能翻转 LED 状态"。三档属于 T6 状态机的范畴，在 3 周工期下先简化，避免关键路径被非评分点占用；相关消抖代码保留。 |
| 用 `HAL_Delay()` 实现闪烁与延时 | 阻塞式等待会让按键扫描、后续插补（T2 要求 ≥200 Hz）与 USB 上报（T5a 要求 20–50 Hz）全部停摆。 |
| 在 `cmake/gcc-arm-none-eabi.cmake` 中追加编译选项 | 任务书 2.4 禁止手改该文件（CubeMX 重新生成时会被覆盖），改在根 `CMakeLists.txt` 追加。 |
| 在 `Core/Src/syscalls.c` 中实现 `__io_putchar` | 该文件由 CubeMX 生成，重新生成时会被覆盖；改为写在 `main.c` 的 USER CODE 4 区。 |
| 四路 PWM 采用 TIM2 全重映射（PA15/PB3/PB10/PB11） | 该方案占用 JTAG 调试脚（需关闭 JTAG）、占掉 I2C2 总线，且四个信号脚分散在左右两排排针，插错一位会顶到机械限位。TIM2 为 32 位定时器，留给后续编码器/输入捕获更划算。 |
| 用软件计时判断舵机堵转 | SG90 无电流反馈，堵转时脉宽输出与正常状态无差异，软件无法判定。需增加采样电阻 + ADC 或 INA219 电流传感器，硬件未到位，该项暂缓。 |

## 四、声明

本人能够解释本仓库中每一处与 AI 相关的代码，包括其原理、边界条件与失效表现。当前已掌握的关键点：

1. `HAL_GetTick()` 与 `now - last` 的无符号差值写法为何能正确处理计数器回绕（`uint32_t` 减法在模 2³² 下差值仍正确）；
2. `RCC_FLAG_PORRST` 必须先于 `RCC_FLAG_PINRST` 判定的原因（F1 上电时 NRST 引脚被拉低，两标志常同时置位，否则上电会被误报为按键复位）；
3. `__io_putchar` 为何定义在 `main.c` 的 USER CODE 4 区而非 `syscalls.c`；
4. 按键消抖为何采用"电平稳定 20 ms"而非固定周期采样，以及 `g_key_latched` 单次触发锁的作用（长按只触发一次，松手稳定后才解锁）；
5. PC13 蓝灯为灌电流接法，故 `GPIO_PIN_RESET` 才是点亮；
6. 1 Hz 闪烁的翻转半周期是 500 ms——频率是周期的倒数，不能把半周期当周期；
7. 为何"脉宽 µs 在数值上等于 CCR"：`PSC=71` 使定时器计数频率为 72 MHz / 72 = 1 MHz，一个计数即 1 µs，故比较值 1500 直接对应 1500 µs 高电平，这也是"分辨率优于 1 µs"的来源；
8. 角度→脉宽的线性映射式为 `pulse = 500 + deg/180 × 2000`，`+0.5f` 用于四舍五入，避免浮点截断带来的系统性偏差；
9. 限速推进（每 20 ms 最多变 25 µs）的三个作用：抑制过冲、降低四路同时动作的电流冲击、减少塑料齿磨损；其前提是指令速度低于舵机机械最大速度；
10. CubeMX 6.18.1 在本工程中重新生成 `main.c` 后会丢失 `main()` 最后的收尾大括号，每次生成后都必须检查，否则报 `expected declaration or statement at end of input`（已重复出现三次）。
