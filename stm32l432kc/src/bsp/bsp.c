/*
 * bsp.c
 *
 *  Created on: Dec 6, 2020
 *      Author: baram
 */


#include "bsp.h"
#include "uart.h"



void SystemClock_Config(void);

static bool is_delay_us_init = false;

static uint32_t delayUsGetTickPerUs(void)
{
  uint32_t tick_per_us;

  tick_per_us = SystemCoreClock / 1000000U;

  if(tick_per_us == 0)
  {
    tick_per_us = 1;
  }

  return tick_per_us;
}


void bspInit(void)
{
  HAL_Init();
  SystemClock_Config();
  delayUsInit();

  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
}

void delay(uint32_t ms)
{
#ifdef _USE_HW_RTOS
  if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
  {
    osDelay(ms);
  }
  else
  {
    HAL_Delay(ms);
  }
#else
  HAL_Delay(ms);
#endif
}

bool delayUsInit(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

  is_delay_us_init = (DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) ? true : false;

  return is_delay_us_init;
}

void delayUs(uint32_t us)
{
  uint32_t tick_per_us;

  if(us == 0)
  {
    return;
  }

  if(is_delay_us_init != true)
  {
    if(delayUsInit() != true)
    {
      return;
    }
  }

  tick_per_us = delayUsGetTickPerUs();

  while(us > 0)
  {
    uint32_t max_us;
    uint32_t wait_us;
    uint32_t wait_tick;
    uint32_t start_tick;

    max_us = 0xFFFFFFFFU / tick_per_us;
    wait_us = us;

    if(wait_us > max_us)
    {
      wait_us = max_us;
    }

    wait_tick = wait_us * tick_per_us;
    start_tick = DWT->CYCCNT;

    while((DWT->CYCCNT - start_tick) < wait_tick)
    {
    }

    us -= wait_us;
  }
}

uint32_t millis(void)
{
  return HAL_GetTick();
}

uint32_t micros(void)
{
  uint32_t tick_per_us;

  if(is_delay_us_init != true)
  {
    if(delayUsInit() != true)
    {
      return 0;
    }
  }

  tick_per_us = delayUsGetTickPerUs();

  return DWT->CYCCNT / tick_per_us;
}

int __io_putchar(int ch)
{
  //uartWrite(_DEF_UART2, (uint8_t *)&ch, 1);
  return 1;
}



void SystemClock_Config(void)
{
	RCC_OscInitTypeDef RCC_OscInitStruct = {0};
	  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

	  /** Configure the main internal regulator output voltage
	  */
	  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
	  {
	    Error_Handler();
	  }

	  /** Configure LSE Drive Capability
	  */
	  HAL_PWR_EnableBkUpAccess();
	  __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_LOW);

	  /** Initializes the RCC Oscillators according to the specified parameters
	  * in the RCC_OscInitTypeDef structure.
	  */
	  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSE|RCC_OSCILLATORTYPE_MSI;
	  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
	  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
	  RCC_OscInitStruct.MSICalibrationValue = 0;
	  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_6;
	  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
	  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
	  {
	    Error_Handler();
	  }

	  /** Initializes the CPU, AHB and APB buses clocks
	  */
	  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
	                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
	  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_MSI;
	  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
	  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
	  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

	  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
	  {
	    Error_Handler();
	  }

	  /** Enable MSI Auto calibration
	  */
	  HAL_RCCEx_EnableMSIPLLMode();
}


/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */

  /* USER CODE END Error_Handler_Debug */
}
