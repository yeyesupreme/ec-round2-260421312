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
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>   /* T0 自检信息打印用；底层经 syscalls.c 的 _write 转到 __io_putchar */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* 引脚映射：PC13 板载蓝灯（灌电流接法，低电平点亮）；PA0 为上拉输入，接地即按下 */
#define LED_PORT            GPIOC
#define LED_PIN             GPIO_PIN_13
#define KEY_PORT            GPIOA
#define KEY_PIN             GPIO_PIN_0

#define LED_ON()            HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET)
#define LED_OFF()           HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET)

#define APP_NAME            "ec-round2-260421312"  /* 用于 T0 自检打印的工程名 */

#define KEY_DEBOUNCE_MS     20U     /* 按键消抖：电平需稳定 20ms 才认账 */
#define LED_BLINK_HALF_MS   500U    /* 翻转半周期 500ms → 整周期 1s → 1Hz */

#define UART_TX_TIMEOUT_MS  10U     /* 单字节发送超时，防止串口异常时卡死 */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
static uint8_t  g_blink_enable = 1U;   /* LED 闪烁使能：1=1Hz 闪烁，0=常灭（按键翻转） */
static uint32_t g_led_tick     = 0U;   /* 上一次 LED 翻转时刻 */
static uint32_t g_key_tick     = 0U;   /* 按键电平最后一次变化时刻 */
static uint8_t  g_key_level    = 1U;   /* 消抖后的稳定电平：1=释放 0=按下 */
static uint8_t  g_key_latched  = 0U;   /* 本次按下已处理，避免长按连发 */
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
  /* USER CODE BEGIN 2 */
  T0_PrintBanner();               /* T0：开机打印自检信息（工程名/时钟/复位原因） */
  __HAL_RCC_CLEAR_RESET_FLAGS();  /* 读完即清，保证下次复位原因不被旧标志污染 */
  LED_ON();                       /* 上电从"亮"起，之后每 500ms 翻转 → 1Hz */
  g_led_tick = HAL_GetTick();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
   
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

    uint32_t now     = HAL_GetTick();
    uint8_t  key_raw = (HAL_GPIO_ReadPin(KEY_PORT, KEY_PIN) == GPIO_PIN_RESET) ? 0U : 1U;

    /* ---------- 1. 按键：20ms 消抖 + 单次触发，翻转 LED 闪烁状态 ---------- */
    if (key_raw != g_key_level)
    {
      g_key_tick  = now;        /* 电平刚变化，重新开始计时 */
      g_key_level = key_raw;
    }

    if ((now - g_key_tick) >= KEY_DEBOUNCE_MS)   /* 电平已稳定，判定有效 */
    {
      if (g_key_level == 0U)                    /* 稳定按下 */
      {
        if (g_key_latched == 0U)
        {
          g_key_latched  = 1U;
          g_blink_enable = (uint8_t)(g_blink_enable == 0U ? 1U : 0U);
          if (g_blink_enable == 0U) LED_OFF();  /* 关闪烁立刻灭灯，不等下一拍 */
          printf("[KEY] %s\r\n", g_blink_enable ? "LED -> 1Hz 闪烁" : "LED -> 常灭");
        }
      }
      else                                      /* 稳定释放，允许下一次触发 */
      {
        g_key_latched = 0U;
      }
    }

    /* ---------- 2. LED：使能时 1Hz 非阻塞翻转 ---------- */
    if ((g_blink_enable != 0U) && ((now - g_led_tick) >= LED_BLINK_HALF_MS))
    {
      g_led_tick = now;
      HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
    }

    /* USER CODE END 3 */
  }
  /* USER CODE END 3 */
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
