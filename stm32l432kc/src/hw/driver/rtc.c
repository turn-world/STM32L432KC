/*
 * rtc.c
 *
 *  Created on: Jun 24, 2026
 *      Author: young
 */

#include "rtc.h"
#include "cli.h"

#ifdef _USE_HW_RTC


static RTC_HandleTypeDef hrtc;
static bool is_init = false;

#ifdef _USE_HW_CLI
static void cliRtc(cli_args_t *args);
#endif



bool rtcInit(void)
{
  bool ret = true;


  __HAL_RCC_GPIOC_CLK_ENABLE();


  hrtc.Instance = RTC;
  hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
  hrtc.Init.AsynchPrediv = 127;
  hrtc.Init.SynchPrediv = 255;
  hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
  hrtc.Init.OutPutRemap = RTC_OUTPUT_REMAP_NONE;
  hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;

  if (HAL_RTC_Init(&hrtc) != HAL_OK)
  {
    Error_Handler();
  }

  is_init = ret;

#ifdef _USE_HW_CLI
  cliAdd("rtc", cliRtc);
#endif

  return ret;
}

uint32_t rtcBackupRegRead(uint32_t index)
{
  return HAL_RTCEx_BKUPRead(&hrtc, index);
}

void rtcBackupRegWrite(uint32_t index, uint32_t data)
{
  HAL_RTCEx_BKUPWrite(&hrtc, index, data);
}




void HAL_RTC_MspInit(RTC_HandleTypeDef* rtcHandle)
{

  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};
  if(rtcHandle->Instance==RTC)
  {
  /* USER CODE BEGIN RTC_MspInit 0 */

  /* USER CODE END RTC_MspInit 0 */

  /** Initializes the peripherals clock
  */
    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_RTC;
    PeriphClkInit.RTCClockSelection = RCC_RTCCLKSOURCE_LSI;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
    {
      Error_Handler();
    }

    /* RTC clock enable */
    __HAL_RCC_RTC_ENABLE();
  /* USER CODE BEGIN RTC_MspInit 1 */

  /* USER CODE END RTC_MspInit 1 */
  }
}

void HAL_RTC_MspDeInit(RTC_HandleTypeDef* rtcHandle)
{

  if(rtcHandle->Instance==RTC)
  {
  /* USER CODE BEGIN RTC_MspDeInit 0 */

  /* USER CODE END RTC_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_RTC_DISABLE();
  /* USER CODE BEGIN RTC_MspDeInit 1 */

  /* USER CODE END RTC_MspDeInit 1 */
  }
}

#ifdef _USE_HW_CLI

static bool rtcCliIsValidBackupIndex(uint32_t index)
{
  return index < RTC_BKP_NUMBER;
}

static void cliRtc(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 1 && args->isStr(0, "info") == true)
  {
    cliPrintf("rtc init       : %d\n", is_init);
    cliPrintf("backup reg cnt : %lu\n", (unsigned long)RTC_BKP_NUMBER);
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "read") == true)
  {
    uint32_t index;

    index = (uint32_t)args->getData(1);

    if (rtcCliIsValidBackupIndex(index) == true)
    {
      cliPrintf("rtc bkp[%lu] : 0x%08lX\n",
                (unsigned long)index,
                (unsigned long)rtcBackupRegRead(index));
    }
    else
    {
      cliPrintf("rtc read index fail\n");
    }

    ret = true;
  }

  if (args->argc == 3 && args->isStr(0, "write") == true)
  {
    uint32_t index;
    uint32_t data;

    index = (uint32_t)args->getData(1);
    data = (uint32_t)args->getData(2);

    if (rtcCliIsValidBackupIndex(index) == true)
    {
      rtcBackupRegWrite(index, data);
      cliPrintf("rtc bkp[%lu] <- 0x%08lX\n",
                (unsigned long)index,
                (unsigned long)data);
    }
    else
    {
      cliPrintf("rtc write index fail\n");
    }

    ret = true;
  }

  if (args->argc >= 1 && args->argc <= 2 && args->isStr(0, "dump") == true)
  {
    uint32_t count = RTC_BKP_NUMBER;

    if (args->argc == 2)
    {
      count = (uint32_t)args->getData(1);
      if (count > RTC_BKP_NUMBER)
      {
        count = RTC_BKP_NUMBER;
      }
    }

    for (uint32_t i = 0; i < count; i++)
    {
      cliPrintf("rtc bkp[%02lu] : 0x%08lX\n",
                (unsigned long)i,
                (unsigned long)rtcBackupRegRead(i));
    }

    ret = true;
  }

  if (ret != true)
  {
    cliPrintf("rtc info\n");
    cliPrintf("rtc read index[0~%lu]\n", (unsigned long)(RTC_BKP_NUMBER - 1U));
    cliPrintf("rtc write index[0~%lu] data\n", (unsigned long)(RTC_BKP_NUMBER - 1U));
    cliPrintf("rtc dump [count]\n");
  }
}

#endif

#endif
