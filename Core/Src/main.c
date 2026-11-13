/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>   /* T0 自检信息打印用；底层经 syscalls.c 的 _write 转到 __io_putchar */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/**
  * @brief 一路舵机的参数与运行状态
  * @note  把"哪路定时器 / 哪个通道 / 行程范围 / 当前与目标脉宽"打包，
  *        扩到四路时只是数组长度不同，控制代码一行都不用改。
  */
typedef struct
{
  TIM_HandleTypeDef *htim;          /* 所属定时器句柄 */
  uint32_t           channel;       /* 通道号：TIM_CHANNEL_x */
  uint16_t           pulse_min;     /* 下限脉宽 µs */
  uint16_t           pulse_max;     /* 上限脉宽 µs */
  uint16_t           pulse_target;  /* 目标脉宽 µs */
  uint16_t           pulse_now;     /* 当前已输出脉宽 µs */
} Servo_t;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* ---------- 工程标识 ---------- */
#define APP_NAME            "ec-round2-260421312"   /* T0 自检打印用 */
#define UART_TX_TIMEOUT_MS  10U                     /* 单字节发送超时，防止串口异常时卡死 */

/* ---------- T1 舵机参数：所有可调量集中在此段，答辩时指这里说 ---------- */
#define SERVO_PWM_FREQ_HZ      50U      /* 目标 50Hz → 周期 20ms（CubeMX: PSC=71, ARR=19999） */
#define SERVO_PULSE_MIN_US     500U     /* 行程下限脉宽 */
#define SERVO_PULSE_MAX_US     2500U    /* 行程上限脉宽 */
#define SERVO_PULSE_MID_US     1500U    /* 中位脉宽 */
#define SERVO_ANGLE_MIN_DEG    0.0f     /* 与 MIN_US 对应的角度 */
#define SERVO_ANGLE_MAX_DEG    180.0f   /* 与 MAX_US 对应的角度 */
#define SERVO_TICK_MS          20U      /* 刷新节拍，与 PWM 周期一致 */
#define SERVO_SLEW_US_PER_TICK 25U      /* 每拍脉宽最大变化量：限速，抑制过冲与啸叫 */
#define SERVO_DEMO_STEP_MS     1000U    /* 自测时每个位置停留时间 */

/* 板载蓝灯（PC13，灌电流接法，低电平点亮）：T1 阶段只熄灭，不再参与逻辑 */
#define LED_PORT            GPIOC
#define LED_PIN             GPIO_PIN_13
#define LED_OFF()           HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET)

/* 当前接了几路。CubeMX 侧目前只配了 TIM2_CH1，故为 1；扩到四路时改这里并补表项 */
#define SERVO_COUNT         1U

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* 舵机表：当前 1 路（TIM2_CH1 → PA15）。明天换 TIM3 四路时，只改这张表 */
static Servo_t g_servos[SERVO_COUNT] =
{
  { &htim2, TIM_CHANNEL_1, SERVO_PULSE_MIN_US, SERVO_PULSE_MAX_US,
    SERVO_PULSE_MID_US, SERVO_PULSE_MID_US },
};

static uint32_t g_servo_tick = 0U;   /* 上一次脉宽推进时刻 */
static uint32_t g_demo_tick  = 0U;   /* 自测换位时刻 */
static uint8_t  g_demo_step  = 0U;   /* 自测位置序号 */

/* 自测序列：90° / 135° / 45°，按 0~180° 线性映射正好是 1500 / 2000 / 1000 µs */
static const float g_demo_angle[3] = { 90.0f, 135.0f, 45.0f };
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/**
  * @brief 读取并解释本次复位原因
  * @note  F1 系列上电时 POR 与 NRST 标志常同时置位，故先判 POR，
  *        否则上电会被误报为"按键复位"。读完后由调用者清除标志。
  */
static const char *Reset_Cause_Str(void)
{
  if (__HAL_RCC_GET_FLAG(RCC_FLAG_PORRST)   != 0U) return "上电复位(POR)";
  if (__HAL_RCC_GET_FLAG(RCC_FLAG_PINRST)   != 0U) return "NRST 引脚复位(复位键/下载器)";
  if (__HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST)   != 0U) return "软件复位(SFTRST)";
  if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST)  != 0U) return "独立看门狗(IWDG)";
  if (__HAL_RCC_GET_FLAG(RCC_FLAG_WWDGRST)  != 0U) return "窗口看门狗(WWDG)";
  if (__HAL_RCC_GET_FLAG(RCC_FLAG_LPWRRST)  != 0U) return "低功耗复位(LPWR)";
  return "未知(标志已被清除)";
}

/**
  * @brief T0 自检信息：工程名、时钟频率、复位原因
  */
static void T0_PrintBanner(void)
{
  uint32_t sysclk = HAL_RCC_GetSysClockFreq();
  uint32_t hclk   = HAL_RCC_GetHCLKFreq();
  uint32_t pclk1  = HAL_RCC_GetPCLK1Freq();
  uint32_t pclk2  = HAL_RCC_GetPCLK2Freq();

  printf("\r\n========== T0 板级自检 ==========\r\n");
  printf("工程名   : %s\r\n", APP_NAME);
  printf("芯片     : STM32F103x8 (Cortex-M3)\r\n");
  printf("系统时钟 : %lu Hz (%lu MHz)\r\n",
         (unsigned long)sysclk, (unsigned long)(sysclk / 1000000U));
  printf("HCLK     : %lu Hz\r\n", (unsigned long)hclk);
  printf("PCLK1    : %lu Hz (APB1)\r\n", (unsigned long)pclk1);
  printf("PCLK2    : %lu Hz (APB2)\r\n", (unsigned long)pclk2);
  printf("复位原因 : %s\r\n", Reset_Cause_Str());
  printf("编译时间 : %s %s\r\n", __DATE__, __TIME__);
  printf("================================\r\n");
}

/**
  * @brief 把脉宽写进比较寄存器（真正输出到引脚的动作）
  * @note  "µs 直接等于 CCR" 的原因：定时器计数频率被配成 1MHz
  *        （72MHz / (PSC+1=72) = 1MHz），1 个计数就是 1µs，
  *        所以 CCR = 1500 即输出 1500µs 高电平。这是"分辨率优于 1µs"的来源。
  */
static void Servo_ApplyPulse(Servo_t *s, uint16_t pulse_us)
{
  __HAL_TIM_SET_COMPARE(s->htim, s->channel, pulse_us);
}

/**
  * @brief 角度 → 脉宽 线性映射（含角度钳位）
  * @param deg 目标角度，超出 [0°, 180°] 时被钳位
  * @retval 对应的脉宽 µs
  */
static uint16_t Servo_AngleToPulse(const Servo_t *s, float deg)
{
  float ratio;

  if (deg < SERVO_ANGLE_MIN_DEG) deg = SERVO_ANGLE_MIN_DEG;
  if (deg > SERVO_ANGLE_MAX_DEG) deg = SERVO_ANGLE_MAX_DEG;

  ratio = (deg - SERVO_ANGLE_MIN_DEG) / (SERVO_ANGLE_MAX_DEG - SERVO_ANGLE_MIN_DEG);

  /* +0.5f 是四舍五入，避免浮点截断带来最多 1µs 的系统性偏差 */
  return (uint16_t)((float)s->pulse_min + ratio * (float)(s->pulse_max - s->pulse_min) + 0.5f);
}

/**
  * @brief 设定目标脉宽：先钳位到行程范围，再只记录不输出
  * @note  真正的输出由 Servo_Task() 每 20ms 推进一次，
  *        这样大角度跳变也是"分步走过去"，不会瞬间给出阶跃指令。
  */
static void Servo_SetPulse(Servo_t *s, uint16_t pulse_us)
{
  if (pulse_us < s->pulse_min) pulse_us = s->pulse_min;
  if (pulse_us > s->pulse_max) pulse_us = s->pulse_max;

  s->pulse_target = pulse_us;
}

/**
  * @brief 设定目标角度（内部换算成脉宽）
  */
static void Servo_SetAngle(Servo_t *s, float deg)
{
  Servo_SetPulse(s, Servo_AngleToPulse(s, deg));
}

/**
  * @brief 每 SERVO_TICK_MS 调用一次：把当前脉宽向目标推进一步
  * @note  限速（SLEW）有三个作用：
  *        1) 抑制过冲——指令速度低于舵机机械最大速度，舵机始终"追得上"；
  *        2) 减少电流冲击——四路同时大角度动作时不会把电源瞬间拉垮；
  *        3) 降低塑料齿被猛拽的磨损。
  */
static void Servo_Task(void)
{
  uint8_t i;

  for (i = 0U; i < SERVO_COUNT; i++)
  {
    Servo_t *s = &g_servos[i];

    if (s->pulse_now < s->pulse_target)
    {
      uint16_t step = (uint16_t)(s->pulse_target - s->pulse_now);
      s->pulse_now += (step > SERVO_SLEW_US_PER_TICK) ? SERVO_SLEW_US_PER_TICK : step;
      Servo_ApplyPulse(s, s->pulse_now);
    }
    else if (s->pulse_now > s->pulse_target)
    {
      uint16_t step = (uint16_t)(s->pulse_now - s->pulse_target);
      s->pulse_now -= (step > SERVO_SLEW_US_PER_TICK) ? SERVO_SLEW_US_PER_TICK : step;
      Servo_ApplyPulse(s, s->pulse_now);
    }
    else
    {
      /* 已到位：不重复写寄存器，避免无意义的空转 */
    }
  }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  MX_TIM2_Init();
  /* USER CODE BEGIN 2 */
  LED_OFF();                                              /* 蓝灯常灭：T1 阶段不再闪烁 */
  HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);               /* 启动 TIM2_CH1 PWM 输出 */
  Servo_ApplyPulse(&g_servos[0], SERVO_PULSE_MID_US);     /* 上电即中位 1.5ms，舵机不甩 */
  g_servo_tick = HAL_GetTick();
  g_demo_tick  = g_servo_tick;
  T0_PrintBanner();               /* T0：开机打印自检信息（工程名/时钟/复位原因） */
  __HAL_RCC_CLEAR_RESET_FLAGS();  /* 读完即清，保证下次复位原因不被旧标志污染 */
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
   
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

    uint32_t now = HAL_GetTick();

    /* ---------- 1. 每 20ms 推进一次脉宽（限速，抑制过冲） ---------- */
    if ((now - g_servo_tick) >= SERVO_TICK_MS)
    {
      g_servo_tick = now;
      Servo_Task();
    }

    /* ---------- 2. 自测：每 1s 换一个目标位置，1.5ms → 2.0ms → 1.0ms 循环 ---------- */
    if ((now - g_demo_tick) >= SERVO_DEMO_STEP_MS)
    {
      g_demo_tick = now;
      g_demo_step = (uint8_t)((g_demo_step + 1U) % 3U);
      Servo_SetAngle(&g_servos[0], g_demo_angle[g_demo_step]);
      printf("[T1] 目标角度 %d deg -> 脉宽 %lu us\r\n",
             (int)g_demo_angle[g_demo_step],
             (unsigned long)Servo_AngleToPulse(&g_servos[0], g_demo_angle[g_demo_step]));
    }

    /* USER CODE END 3 */
  }
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/**
  * @brief printf 的最底层输出：逐字节写 USART1
  * @note  syscalls.c 中的 _write() 会调用本函数（原声明为 weak），
  *        因此这里实现即可让 printf 走串口，无需改动生成文件。
  *        __io_putchar 与 _write 的签名必须与 syscalls.c 一致。
  */
int __io_putchar(int ch)
{
  uint8_t byte = (uint8_t)ch;
  (void)HAL_UART_Transmit(&huart1, &byte, 1U, UART_TX_TIMEOUT_MS);
  return ch;
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
