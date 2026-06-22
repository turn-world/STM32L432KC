/*
 * adc.c
 *
 * APPS ADC driver
 */

#include "adc.h"


#ifdef _USE_HW_ADC

#include "cli.h"


#define NAME_DEF(x)                  x, #x
#define ADC_POLL_TIMEOUT_MS          1U
#define ADC_12BIT_FULL_SCALE         4095U
#define ADC_REFERENCE_VOLTAGE        3.3f


typedef struct
{
  ADC_HandleTypeDef *p_hadc;
  uint32_t           channel;
  uint32_t           rank;
  AdcPinName_t       pin_name;
  const char        *p_name;
  const char        *p_pin_info;
} adc_tbl_t;


#ifdef _USE_HW_CLI
static void cliAdc(cli_args_t *args);
static void cliAdcPrintChannel(uint8_t ch);
#endif


static ADC_HandleTypeDef hadc1;
static bool is_init = false;
static uint16_t adc_data_buf[ADC_MAX_CH];

static const adc_tbl_t adc_tbl[ADC_MAX_CH] =
{
  {&hadc1, ADC_CHANNEL_5, ADC_REGULAR_RANK_1, NAME_DEF(APPS_SIGNAL1_ADC),
   "A0 / PA0 / ADC1_IN5"},
  {&hadc1, ADC_CHANNEL_6, ADC_REGULAR_RANK_2, NAME_DEF(APPS_SIGNAL2_ADC),
   "A1 / PA1 / ADC1_IN6"},
};


bool adcInit(void)
{
  bool ret = true;
  ADC_ChannelConfTypeDef sConfig = {0};

  is_init = false;
  memset(adc_data_buf, 0, sizeof(adc_data_buf));

  for (int i = 0; i < ADC_MAX_CH; i++)
  {
    if (i != (int)adc_tbl[i].pin_name)
    {
      ret = false;
    }
  }

  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler        = ADC_CLOCK_ASYNC_DIV1;
  hadc1.Init.Resolution            = ADC_RESOLUTION_12B;
  hadc1.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
  hadc1.Init.ScanConvMode          = ADC_SCAN_ENABLE;
  hadc1.Init.EOCSelection          = ADC_EOC_SINGLE_CONV;
  hadc1.Init.LowPowerAutoWait      = DISABLE;
  hadc1.Init.ContinuousConvMode    = DISABLE;
  hadc1.Init.NbrOfConversion       = ADC_MAX_CH;
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

  /*
   * Keep the acquisition time short for the APPS control loop.
   * Increase this value if the external divider/filter has high impedance.
   */
  sConfig.SamplingTime = ADC_SAMPLETIME_24CYCLES_5;
  sConfig.SingleDiff    = ADC_SINGLE_ENDED;
  sConfig.OffsetNumber  = ADC_OFFSET_NONE;
  sConfig.Offset        = 0;

  if (ret == true)
  {
    for (int i = 0; i < ADC_MAX_CH; i++)
    {
      sConfig.Channel = adc_tbl[i].channel;
      sConfig.Rank    = adc_tbl[i].rank;

      if (HAL_ADC_ConfigChannel(adc_tbl[i].p_hadc, &sConfig) != HAL_OK)
      {
        ret = false;
        break;
      }
    }
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
    is_init = true;
  }

#ifdef _USE_HW_CLI
  cliAdd("adc", cliAdc);
#endif

  return ret;
}

bool adcIsInit(void)
{
  return is_init;
}

bool adcUpdate(void)
{
  uint16_t new_data[ADC_MAX_CH];

  if (is_init != true)
  {
    return false;
  }

  if (HAL_ADC_Start(&hadc1) != HAL_OK)
  {
    return false;
  }

  for (int i = 0; i < ADC_MAX_CH; i++)
  {
    if (HAL_ADC_PollForConversion(&hadc1, ADC_POLL_TIMEOUT_MS) != HAL_OK)
    {
      (void)HAL_ADC_Stop(&hadc1);
      return false;
    }

    new_data[i] = (uint16_t)HAL_ADC_GetValue(&hadc1);
  }

  if (HAL_ADC_Stop(&hadc1) != HAL_OK)
  {
    return false;
  }

  memcpy(adc_data_buf, new_data, sizeof(adc_data_buf));

  return true;
}

void HAL_ADC_MspInit(ADC_HandleTypeDef *p_hadc)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  if (p_hadc->Instance == ADC1)
  {
    __HAL_RCC_ADC_CONFIG(RCC_ADCCLKSOURCE_SYSCLK);
    __HAL_RCC_ADC_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /*
     * PA0 -> ADC1_IN5 -> APPS Signal1
     * PA1 -> ADC1_IN6 -> APPS Signal2
     */
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
    __HAL_RCC_ADC_FORCE_RESET();
    __HAL_RCC_ADC_RELEASE_RESET();
    __HAL_RCC_ADC_CLK_DISABLE();

    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_0 | GPIO_PIN_1);
  }
}

int32_t adcRead(uint8_t ch)
{
  if (ch >= ADC_MAX_CH)
  {
    return 0;
  }

  return adc_data_buf[ch];
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

  return ((adc_value * 3300U) + (ADC_12BIT_FULL_SCALE / 2U)) /
         ADC_12BIT_FULL_SCALE;
}

bool adcReadApps(uint16_t *p_signal1_raw, uint16_t *p_signal2_raw)
{
  if ((p_signal1_raw == NULL) || (p_signal2_raw == NULL))
  {
    return false;
  }

  if (adcUpdate() != true)
  {
    return false;
  }

  *p_signal1_raw = (uint16_t)adcRead(APPS_SIGNAL1_ADC);
  *p_signal2_raw = (uint16_t)adcRead(APPS_SIGNAL2_ADC);

  return true;
}


#ifdef _USE_HW_CLI

static void cliAdcPrintChannel(uint8_t ch)
{
  uint32_t raw;
  uint32_t voltage_mv;

  raw = (uint32_t)adcRead(ch);
  voltage_mv = adcConvMillivolts(ch, raw);

  cliPrintf("%02d. %-19s %-20s : raw=%4d, voltage=%d.%03dV (%dmV)\n",
            ch,
            adc_tbl[ch].p_pin_info,
            adc_tbl[ch].p_name,
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
    cliPrintf("adc init : %d\n", is_init);
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
