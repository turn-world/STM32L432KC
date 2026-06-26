/*
 * adc.c
 *
 * Simple ADC driver
 */

#include "adc.h"
#include "cli.h"

#ifdef _USE_HW_ADC

#define ADC_POLL_TIMEOUT_MS          1U
#define ADC_12BIT_FULL_SCALE         4095U
#define ADC_REFERENCE_MV             3300U
#define ADC_REFERENCE_VOLTAGE        3.3f


typedef struct
{
  bool     is_open;
  uint16_t data;
  uint32_t channel;
} adc_t;


#ifdef _USE_HW_CLI
static void cliAdc(cli_args_t *args);
static void cliAdcPrintChannel(uint8_t ch);
#endif

static bool adcOpen(void);
static bool adcReadChannel(uint8_t ch, uint16_t *p_data);


static ADC_HandleTypeDef hadc1;

static adc_t adc_tbl[ADC_MAX_CH] =
{
  {false, 0, ADC_CHANNEL_5},  /* ADC_CH_0: PA0 / ADC1_IN5 */
  {false, 0, ADC_CHANNEL_6},  /* ADC_CH_1: PA1 / ADC1_IN6 */
};


bool adcInit(void)
{
  bool ret;

  for (int i = 0; i < ADC_MAX_CH; i++)
  {
    adc_tbl[i].is_open = false;
    adc_tbl[i].data = 0;
  }

  ret = adcOpen();

#ifdef _USE_HW_CLI
  cliAdd("adc", cliAdc);
#endif

  return ret;
}

static bool adcOpen(void)
{
  bool ret = true;

  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler        = ADC_CLOCK_ASYNC_DIV1;
  hadc1.Init.Resolution            = ADC_RESOLUTION_12B;
  hadc1.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
  hadc1.Init.ScanConvMode          = ADC_SCAN_DISABLE;
  hadc1.Init.EOCSelection          = ADC_EOC_SINGLE_CONV;
  hadc1.Init.LowPowerAutoWait      = DISABLE;
  hadc1.Init.ContinuousConvMode    = DISABLE;
  hadc1.Init.NbrOfConversion       = 1;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.NbrOfDiscConversion   = 1;
  hadc1.Init.ExternalTrigConv      = ADC_SOFTWARE_START;
  hadc1.Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.Overrun               = ADC_OVR_DATA_OVERWRITTEN;
  hadc1.Init.OversamplingMode      = DISABLE;

  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    ret = false;
  }

  if (ret == true)
  {
    if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED) != HAL_OK)
    {
      ret = false;
    }
  }

  if (ret == true)
  {
    for (int i = 0; i < ADC_MAX_CH; i++)
    {
      adc_tbl[i].is_open = true;
    }
  }

  return ret;
}

bool adcIsInit(void)
{
  for (int i = 0; i < ADC_MAX_CH; i++)
  {
    if (adc_tbl[i].is_open != true)
    {
      return false;
    }
  }

  return true;
}

static bool adcReadChannel(uint8_t ch, uint16_t *p_data)
{
  ADC_ChannelConfTypeDef sConfig = {0};

  if ((ch >= ADC_MAX_CH) || (p_data == NULL) || (adc_tbl[ch].is_open != true))
  {
    return false;
  }

  sConfig.Channel      = adc_tbl[ch].channel;
  sConfig.Rank         = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_24CYCLES_5;
  sConfig.SingleDiff   = ADC_SINGLE_ENDED;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset       = 0;

  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    return false;
  }

  if (HAL_ADC_Start(&hadc1) != HAL_OK)
  {
    return false;
  }

  if (HAL_ADC_PollForConversion(&hadc1, ADC_POLL_TIMEOUT_MS) != HAL_OK)
  {
    (void)HAL_ADC_Stop(&hadc1);
    return false;
  }

  *p_data = (uint16_t)HAL_ADC_GetValue(&hadc1);

  if (HAL_ADC_Stop(&hadc1) != HAL_OK)
  {
    return false;
  }

  return true;
}

bool adcUpdate(void)
{
  uint16_t data[ADC_MAX_CH];

  if (adcIsInit() != true)
  {
    return false;
  }

  for (int i = 0; i < ADC_MAX_CH; i++)
  {
    if (adcReadChannel((uint8_t)i, &data[i]) != true)
    {
      return false;
    }
  }

  for (int i = 0; i < ADC_MAX_CH; i++)
  {
    adc_tbl[i].data = data[i];
  }

  return true;
}

void HAL_ADC_MspInit(ADC_HandleTypeDef *p_hadc)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  if (p_hadc->Instance == ADC1)
  {
    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
    PeriphClkInit.AdcClockSelection = RCC_ADCCLKSOURCE_PLLSAI1;
    PeriphClkInit.PLLSAI1.PLLSAI1Source = RCC_PLLSOURCE_MSI;
    PeriphClkInit.PLLSAI1.PLLSAI1M = 1;
    PeriphClkInit.PLLSAI1.PLLSAI1N = 16;
    PeriphClkInit.PLLSAI1.PLLSAI1P = RCC_PLLP_DIV7;
    PeriphClkInit.PLLSAI1.PLLSAI1Q = RCC_PLLQ_DIV2;
    PeriphClkInit.PLLSAI1.PLLSAI1R = RCC_PLLR_DIV2;
    PeriphClkInit.PLLSAI1.PLLSAI1ClockOut = RCC_PLLSAI1_ADC1CLK;

    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_RCC_ADC_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitStruct.Pin  = GPIO_PIN_0 | GPIO_PIN_1;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG_ADC_CONTROL;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
  }
}

void HAL_ADC_MspDeInit(ADC_HandleTypeDef *p_hadc)
{
  if (p_hadc->Instance == ADC1)
  {
    __HAL_RCC_ADC_CLK_DISABLE();

    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_0 | GPIO_PIN_1);
  }
}

int32_t adcRead(uint8_t ch)
{
  if ((ch >= ADC_MAX_CH) || (adc_tbl[ch].is_open != true))
  {
    return 0;
  }

  return adc_tbl[ch].data;
}

int32_t adcRead8(uint8_t ch)
{
  return adcRead(ch) >> 4;
}

int32_t adcRead10(uint8_t ch)
{
  return adcRead(ch) >> 2;
}

int32_t adcRead12(uint8_t ch)
{
  return adcRead(ch);
}

int32_t adcRead16(uint8_t ch)
{
  return adcRead(ch) << 4;
}

uint8_t adcGetRes(uint8_t ch)
{
  (void)ch;

  return 12;
}

float adcReadVoltage(uint8_t ch)
{
  return adcConvVoltage(ch, (uint32_t)adcRead(ch));
}

float adcConvVoltage(uint8_t ch, uint32_t adc_value)
{
  (void)ch;

  if (adc_value > ADC_12BIT_FULL_SCALE)
  {
    adc_value = ADC_12BIT_FULL_SCALE;
  }

  return ((float)adc_value * ADC_REFERENCE_VOLTAGE) /
         (float)ADC_12BIT_FULL_SCALE;
}

uint32_t adcReadMillivolts(uint8_t ch)
{
  return adcConvMillivolts(ch, (uint32_t)adcRead(ch));
}

uint32_t adcConvMillivolts(uint8_t ch, uint32_t adc_value)
{
  (void)ch;

  if (adc_value > ADC_12BIT_FULL_SCALE)
  {
    adc_value = ADC_12BIT_FULL_SCALE;
  }

  return ((adc_value * ADC_REFERENCE_MV) + (ADC_12BIT_FULL_SCALE / 2U)) /
         ADC_12BIT_FULL_SCALE;
}


#ifdef _USE_HW_CLI

static void cliAdcPrintChannel(uint8_t ch)
{
  uint32_t raw;
  uint32_t voltage_mv;

  raw = (uint32_t)adcRead(ch);
  voltage_mv = adcConvMillivolts(ch, raw);

  cliPrintf("%02d. raw=%4d, voltage=%d.%03dV (%dmV)\n",
            ch,
            (int)raw,
            (int)(voltage_mv / 1000U),
            (int)(voltage_mv % 1000U),
            (int)voltage_mv);
}

static void cliAdc(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 1 && args->isStr(0, "info") == true)
  {
    cliPrintf("adc init : %d\n", adcIsInit());
    cliPrintf("adc res  : %d\n", adcGetRes(0));

    if (adcUpdate() == true)
    {
      for (int i = 0; i < ADC_MAX_CH; i++)
      {
        cliAdcPrintChannel((uint8_t)i);
      }
    }
    else
    {
      cliPrintf("adc read fail\n");
    }

    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "show") == true)
  {
    while (cliKeepLoop())
    {
      if (adcUpdate() == true)
      {
        for (int i = 0; i < ADC_MAX_CH; i++)
        {
          cliAdcPrintChannel((uint8_t)i);
        }
      }
      else
      {
        cliPrintf("adc read fail\n");
      }

      delay(100);
    }

    ret = true;
  }

  if (ret != true)
  {
    cliPrintf("adc info\n");
    cliPrintf("adc show\n");
  }
}

#endif

#endif
