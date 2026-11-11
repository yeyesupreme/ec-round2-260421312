# AI 使用记录

## 2026-11-10 环境搭建

- 工具链：VS Code + STM32 VS Code Extension（自动安装 gcc / CMake / Ninja）+ STM32CubeMX
- AI 辅助：扩展选择、CubeMX 六步配置（SYS→Serial Wire、RCC→HSE 晶振、HCLK 72MHz、USART1 异步、PC13 输出、PA0 上拉输入）、Toolchain 选 CMake
- 人工验证：Debug 编译通过，生成 .elf（FLASH 5372 B / RAM 1696 B）

## 2026-11-11 LED 闪烁（T0）

- 需求：PC13 板载蓝灯 1Hz 闪烁，且不阻塞主循环
- AI 辅助：时间戳轮询框架（HAL_GetTick 差值判断），避免使用 HAL_Delay
- 人工理解：
  - HAL_Delay 是阻塞等待，等待期间按键/串口/插补全部停摆
  - HAL_GetTick 每圈取时间戳做差，不阻塞其他逻辑
  - PC13 为灌电流接法，低电平点亮，故 RESET 才是"亮"
- 人工验证：烧录实机，蓝灯 1Hz 稳定闪烁；commit `e74b4dc`
