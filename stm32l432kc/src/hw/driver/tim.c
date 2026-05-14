/*
 * tim.c
 *
 *  Created on: May 14, 2026
 *      Author: young
 */

#include "tim.h"

#ifdef _USE_HW_CLI
#include "cli.h"
#endif

typedef struct
{
  TIM_HandleTypeDef handle;
  TIM_TypeDef      *instance;

  uint32_t          prescaler;
  uint32_t          period;
  uint32_t          auto_reload_preload;
  uint32_t          clk_hz;
  uint32_t          tick_hz;

  IRQn_Type irq_num;

  tim_update_cb_t update_cb;
  void *update_arg;
} tim_tbl_t;

static tim_tbl_t tim_tbl[TIM_MAX_CH] =
{
  {
    .instance             = TIM6,
    .prescaler            = 79,         // 80MHz 기준 1us tick
    .period               = 999,        // 기본 1000us
    .auto_reload_preload  = TIM_AUTORELOAD_PRELOAD_DISABLE,
    .clk_hz               = 80000000,
    .tick_hz              = 1000000,
    .irq_num              = TIM6_DAC_IRQn,
    .update_cb            = NULL,
    .update_arg           = NULL,
  },
};

#ifdef _USE_HW_CLI
static void cliTim(cli_args_t *args);
#endif

static bool timUsToTicks(uint8_t ch, uint32_t period_us, uint32_t *p_ticks)
{
  uint64_t ticks;

  if(ch >= TIM_MAX_CH || period_us == 0 || p_ticks == NULL)
  {
    return false;
  }

  ticks = ((uint64_t)period_us * tim_tbl[ch].tick_hz + 999999ULL) / 1000000ULL;

  if(ticks == 0 || ticks > 0x10000ULL)
  {
    return false;
  }

  *p_ticks = (uint32_t)ticks;

  return true;
}

bool timInit(void)
{

  TIM_MasterConfigTypeDef sMasterConfig = {0};

  for(uint8_t i = 0; i < TIM_MAX_CH; i++)
  {
    tim_tbl[i].handle.Instance                = tim_tbl[i].instance;
    tim_tbl[i].handle.Init.Prescaler          = tim_tbl[i].prescaler;
    tim_tbl[i].handle.Init.CounterMode        = TIM_COUNTERMODE_UP;
    tim_tbl[i].handle.Init.Period             = tim_tbl[i].period;
    tim_tbl[i].handle.Init.AutoReloadPreload  = tim_tbl[i].auto_reload_preload;

    if(HAL_TIM_Base_Init(&tim_tbl[i].handle) != HAL_OK)
    {
      return false;
    }

    sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
    sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;

    if(HAL_TIMEx_MasterConfigSynchronization(&tim_tbl[i].handle, &sMasterConfig) != HAL_OK)
    {
      return false;
    }
  }
#ifdef _USE_HW_CLI
  cliAdd("tim", cliTim);
#endif

  return true;
}

TIM_HandleTypeDef *timGetHandle(uint8_t ch)
{
  if(ch >= TIM_MAX_CH)
  {
    return NULL;
  }

  return &tim_tbl[ch].handle;
}

bool timStart(uint8_t ch)
{
  if(ch >= TIM_MAX_CH)
  {
    return false;
  }

  __HAL_TIM_SET_COUNTER(&tim_tbl[ch].handle, 0);

  return HAL_TIM_Base_Start(&tim_tbl[ch].handle) == HAL_OK;
}

bool timStop(uint8_t ch)
{
  if(ch >= TIM_MAX_CH)
  {
    return false;
  }

  return HAL_TIM_Base_Stop(&tim_tbl[ch].handle) == HAL_OK;
}

bool timStartIT(uint8_t ch)
{
  if(ch >= TIM_MAX_CH)
  {
    return false;
  }

  __HAL_TIM_SET_COUNTER(&tim_tbl[ch].handle, 0);
  __HAL_TIM_CLEAR_FLAG(&tim_tbl[ch].handle, TIM_FLAG_UPDATE);

  return HAL_TIM_Base_Start_IT(&tim_tbl[ch].handle) == HAL_OK;
}

bool timStopIT(uint8_t ch)
{
  if(ch >= TIM_MAX_CH)
  {
    return false;
  }

  return HAL_TIM_Base_Stop_IT(&tim_tbl[ch].handle) == HAL_OK;
}

bool timSetPeriodUs(uint8_t ch, uint32_t period_us)
{
  TIM_HandleTypeDef *p_tim;
  uint32_t ticks;

  if(timUsToTicks(ch, period_us, &ticks) != true)
  {
    return false;
  }

  p_tim = &tim_tbl[ch].handle;

  tim_tbl[ch].period = ticks - 1;
  p_tim->Init.Period = ticks - 1;

  __HAL_TIM_SET_AUTORELOAD(p_tim, ticks - 1);
  __HAL_TIM_SET_COUNTER(p_tim, 0);

  return true;
}

bool timSetTickHz(uint8_t ch, uint32_t tick_hz)
{
  uint32_t div;
  uint32_t prescaler;

  if(ch >= TIM_MAX_CH || tick_hz == 0)
  {
    return false;
  }

  if(tick_hz > tim_tbl[ch].clk_hz)
  {
    return false;
  }

  div = (tim_tbl[ch].clk_hz + (tick_hz / 2U)) / tick_hz;

  if(div == 0)
  {
    return false;
  }

  prescaler = div - 1U;

  if(prescaler > 0xFFFFU)
  {
    return false;
  }

  tim_tbl[ch].prescaler = prescaler;
  tim_tbl[ch].tick_hz = tim_tbl[ch].clk_hz / (prescaler + 1U);
  tim_tbl[ch].handle.Init.Prescaler = prescaler;

  __HAL_TIM_SET_PRESCALER(&tim_tbl[ch].handle, prescaler);
  HAL_TIM_GenerateEvent(&tim_tbl[ch].handle, TIM_EVENTSOURCE_UPDATE);

  return true;
}

uint32_t timGetTickHz(uint8_t ch)
{
  if(ch >= TIM_MAX_CH)
  {
    return 0;
  }

  return tim_tbl[ch].tick_hz;
}

bool timAttachUpdateCallback(uint8_t ch, tim_update_cb_t cb, void *arg)
{
  if(ch >= TIM_MAX_CH)
  {
    return false;
  }

  tim_tbl[ch].update_cb = cb;
  tim_tbl[ch].update_arg = arg;

  return true;
}

void timPeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  for(uint8_t i = 0; i < TIM_MAX_CH; i++)
  {
    if(htim == &tim_tbl[i].handle)
    {
      if(tim_tbl[i].update_cb != NULL)
      {
        tim_tbl[i].update_cb(i, tim_tbl[i].update_arg);
      }
      return;
    }
  }
}

bool timDetachUpdateCallback(uint8_t ch)
{
  if(ch >= TIM_MAX_CH)
  {
    return false;
  }

  tim_tbl[ch].update_cb = NULL;
  tim_tbl[ch].update_arg = NULL;

  return true;
}

void HAL_TIM_Base_MspInit(TIM_HandleTypeDef* tim_baseHandle)
{

  if(tim_baseHandle->Instance==TIM6)
  {
  /* USER CODE BEGIN TIM6_MspInit 0 */

  /* USER CODE END TIM6_MspInit 0 */
    /* TIM6 clock enable */
    __HAL_RCC_TIM6_CLK_ENABLE();
  /* USER CODE BEGIN TIM6_MspInit 1 */

  /* USER CODE END TIM6_MspInit 1 */
  }
}

void HAL_TIM_Base_MspDeInit(TIM_HandleTypeDef* tim_baseHandle)
{

  if(tim_baseHandle->Instance==TIM6)
  {
  /* USER CODE BEGIN TIM6_MspDeInit 0 */

  /* USER CODE END TIM6_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_TIM6_CLK_DISABLE();
  /* USER CODE BEGIN TIM6_MspDeInit 1 */

  /* USER CODE END TIM6_MspDeInit 1 */
  }
}

#ifdef _USE_HW_CLI
static void cliTim(cli_args_t *args)
{
  bool ret = false;

  if(args->argc == 1 && args->isStr(0, "info") == true)
  {
    for(uint8_t i = 0; i < TIM_MAX_CH; i++)
    {
      TIM_HandleTypeDef *p_tim = timGetHandle(i);

      cliPrintf("ch %d\n", i);
      cliPrintf("  TICK: %dHz\n", (int)timGetTickHz(i));
      cliPrintf("  PSC : %d\n", (int)p_tim->Instance->PSC);
      cliPrintf("  ARR : %d\n", (int)__HAL_TIM_GET_AUTORELOAD(p_tim));
      cliPrintf("  CNT : %d\n", (int)__HAL_TIM_GET_COUNTER(p_tim));
      cliPrintf("  RUN : %d\n", (p_tim->Instance->CR1 & TIM_CR1_CEN) ? 1 : 0);
    }

    ret = true;
  }

  if(args->argc == 3 && args->isStr(0, "period") == true)
  {
    uint8_t ch = (uint8_t)args->getData(1);
    uint32_t period_us = (uint32_t)args->getData(2);

    ret = timSetPeriodUs(ch, period_us);
    cliPrintf("tim period %d %d : %s\n", ch, (int)period_us, ret ? "ok" : "fail");
  }

  if(args->argc == 3 && args->isStr(0, "tick") == true)
  {
    uint8_t ch = (uint8_t)args->getData(1);
    uint32_t tick_hz = (uint32_t)args->getData(2);
    bool result;

    result = timSetTickHz(ch, tick_hz);
    cliPrintf("tim tick %d %d : %s\n", ch, (int)tick_hz, result ? "ok" : "fail");

    ret = true;
  }

  if(args->argc == 2 && args->isStr(0, "start") == true)
  {
    uint8_t ch = (uint8_t)args->getData(1);

    ret = timStart(ch);
    cliPrintf("tim start %d : %s\n", ch, ret ? "ok" : "fail");
  }

  if(args->argc == 2 && args->isStr(0, "stop") == true)
  {
    uint8_t ch = (uint8_t)args->getData(1);

    ret = timStop(ch);
    cliPrintf("tim stop %d : %s\n", ch, ret ? "ok" : "fail");
  }

  if(args->argc == 2 && args->isStr(0, "start_it") == true)
  {
    uint8_t ch = (uint8_t)args->getData(1);

    ret = timStartIT(ch);
    cliPrintf("tim start_it %d : %s\n", ch, ret ? "ok" : "fail");
  }

  if(args->argc == 2 && args->isStr(0, "stop_it") == true)
  {
    uint8_t ch = (uint8_t)args->getData(1);

    ret = timStopIT(ch);
    cliPrintf("tim stop_it %d : %s\n", ch, ret ? "ok" : "fail");
  }

  if(ret != true)
  {
    cliPrintf("tim info\n");
    cliPrintf("tim tick ch[0~%d] tick_hz\n", TIM_MAX_CH - 1);
    cliPrintf("tim period ch[0~%d] period_us\n", TIM_MAX_CH - 1);
    cliPrintf("tim start ch[0~%d]\n", TIM_MAX_CH - 1);
    cliPrintf("tim stop ch[0~%d]\n", TIM_MAX_CH - 1);
    cliPrintf("tim start_it ch[0~%d]\n", TIM_MAX_CH - 1);
    cliPrintf("tim stop_it ch[0~%d]\n", TIM_MAX_CH - 1);
  }
}
#endif

