/*
 * tim_pwm.c
 *
 *  Created on: May 14, 2026
 *      Author: young
 */

#include "tim_pwm.h"

#ifdef _USE_TIM_PWM

#ifdef _USE_HW_CLI
#include "cli.h"
#endif

TIM_HandleTypeDef htim16;

typedef struct
{
  TIM_HandleTypeDef *p_tim;
  uint32_t tim_channel;

  GPIO_TypeDef *port;
  uint32_t pin;
  uint32_t alternate;

  tim_pwm_output_t output_type;
  tim_pwm_active_t active_level;

  uint32_t period_us;
  uint32_t active_us;
  uint32_t remain_count;

  bool is_open;
  bool is_busy;
  bool is_count_mode;
} tim_pwm_tbl_t;

static tim_pwm_tbl_t tim_pwm_tbl[TIM_PWM_MAX_CH] =
{
  {
    .p_tim          = &htim16,
    .tim_channel    = TIM_CHANNEL_1,

    .port           = GPIOA,
    .pin            = GPIO_PIN_6,
    .alternate      = GPIO_AF14_TIM16,

    .output_type    = TIM_PWM_OUTPUT_PP,
    .active_level   = TIM_PWM_ACTIVE_HIGH,

    .period_us      = 1000,
    .active_us      = 5,
    .remain_count   = 0,

    .is_open        = false,
    .is_busy        = false,
    .is_count_mode  = false,
  },
};

#ifdef _USE_HW_CLI
static void cliTimPwm(cli_args_t *args);
#endif

static bool timPwmIsValidCh(uint8_t ch)
{
  return ch < TIM_PWM_MAX_CH;
}

static uint32_t timPwmGetPolarity(tim_pwm_active_t active_level)
{
  if(active_level == TIM_PWM_ACTIVE_LOW)
  {
    return TIM_OCPOLARITY_LOW;
  }

  return TIM_OCPOLARITY_HIGH;
}

static bool timPwmConfigGpio(tim_pwm_tbl_t *p_tbl)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  if(p_tbl == NULL)
  {
    return false;
  }

  __HAL_RCC_GPIOA_CLK_ENABLE();

  GPIO_InitStruct.Pin           = p_tbl->pin;
  GPIO_InitStruct.Pull          = GPIO_NOPULL;
  GPIO_InitStruct.Speed         = GPIO_SPEED_FREQ_HIGH;
  GPIO_InitStruct.Alternate     = p_tbl->alternate;

  if(p_tbl->output_type == TIM_PWM_OUTPUT_OD)
  {
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
  }
  else
  {
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  }

  HAL_GPIO_Init(p_tbl->port, &GPIO_InitStruct);

  return true;
}

static bool timPwmConfigChannel(tim_pwm_tbl_t *p_tbl)
{
  TIM_OC_InitTypeDef sConfigOC = {0};

  if(p_tbl == NULL)
  {
    return false;
  }

  sConfigOC.OCMode              = TIM_OCMODE_PWM1;
  sConfigOC.Pulse               = p_tbl->active_us;
  sConfigOC.OCPolarity          = timPwmGetPolarity(p_tbl->active_level);
  sConfigOC.OCNPolarity         = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode          = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState         = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState        = TIM_OCNIDLESTATE_RESET;

  if(HAL_TIM_PWM_ConfigChannel(p_tbl->p_tim, &sConfigOC, p_tbl->tim_channel) != HAL_OK)
  {
    return false;
  }

  return true;
}

static bool timPwmHwInit(tim_pwm_tbl_t *p_tbl)
{
  TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};

  if(p_tbl == NULL)
  {
    return false;
  }

  if(p_tbl->p_tim->Instance != NULL)
  {
    return true;
  }

  __HAL_RCC_TIM16_CLK_ENABLE();

  p_tbl->p_tim->Instance                  = TIM16;
  p_tbl->p_tim->Init.Prescaler            = 79;
  p_tbl->p_tim->Init.CounterMode          = TIM_COUNTERMODE_UP;
  p_tbl->p_tim->Init.Period               = p_tbl->period_us - 1;
  p_tbl->p_tim->Init.ClockDivision        = TIM_CLOCKDIVISION_DIV1;
  p_tbl->p_tim->Init.RepetitionCounter    = 0;
  p_tbl->p_tim->Init.AutoReloadPreload    = TIM_AUTORELOAD_PRELOAD_DISABLE;

  if(HAL_TIM_Base_Init(p_tbl->p_tim) != HAL_OK)
  {
    return false;
  }

  if(HAL_TIM_PWM_Init(p_tbl->p_tim) != HAL_OK)
  {
    return false;
  }

  if(timPwmConfigChannel(p_tbl) != true)
  {
    return false;
  }

  sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
  sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
  sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
  sBreakDeadTimeConfig.DeadTime = 0;
  sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
  sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
  sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;

  if(HAL_TIMEx_ConfigBreakDeadTime(p_tbl->p_tim, &sBreakDeadTimeConfig) != HAL_OK)
  {
    return false;
  }

  return timPwmConfigGpio(p_tbl);
}

bool timPwmInit(void)
{
  bool ret = true;

  for(uint8_t i = 0; i < TIM_PWM_MAX_CH; i++)
  {
    tim_pwm_tbl[i].is_open = false;
    tim_pwm_tbl[i].is_busy = false;
    tim_pwm_tbl[i].is_count_mode = false;
    tim_pwm_tbl[i].remain_count = 0;

    if(timPwmHwInit(&tim_pwm_tbl[i]) != true)
    {
      ret = false;
    }
  }

#ifdef _USE_HW_CLI
  cliAdd("tim_pwm", cliTimPwm);
#endif

  return ret;
}

bool timPwmOpen(uint8_t ch, tim_pwm_output_t output_type, tim_pwm_active_t active_level)
{
  tim_pwm_tbl_t *p_tbl;

  if(timPwmIsValidCh(ch) != true)
  {
    return false;
  }

  if(output_type != TIM_PWM_OUTPUT_PP && output_type != TIM_PWM_OUTPUT_OD)
  {
    return false;
  }

  if(active_level != TIM_PWM_ACTIVE_HIGH && active_level != TIM_PWM_ACTIVE_LOW)
  {
    return false;
  }

  p_tbl = &tim_pwm_tbl[ch];

  timPwmStop(ch);

  p_tbl->output_type = output_type;
  p_tbl->active_level = active_level;

  if(timPwmConfigGpio(p_tbl) != true)
  {
    return false;
  }

  if(timPwmConfigChannel(p_tbl) != true)
  {
    return false;
  }

  p_tbl->is_open = true;

  return true;
}

bool timPwmSetPulse(uint8_t ch, uint32_t period_us, uint32_t active_us)
{
  tim_pwm_tbl_t *p_tbl;

  if(timPwmIsValidCh(ch) != true)
  {
    return false;
  }

  if(period_us == 0 || active_us == 0 || active_us > period_us)
  {
    return false;
  }

  if(period_us > 0x10000U)
  {
    return false;
  }

  p_tbl = &tim_pwm_tbl[ch];

  p_tbl->period_us = period_us;
  p_tbl->active_us = active_us;
  p_tbl->p_tim->Init.Period = period_us - 1;

  __HAL_TIM_SET_AUTORELOAD(p_tbl->p_tim, period_us - 1);
  __HAL_TIM_SET_COMPARE(p_tbl->p_tim, p_tbl->tim_channel, active_us);
  __HAL_TIM_SET_COUNTER(p_tbl->p_tim, 0);

  return true;
}

bool timPwmStart(uint8_t ch)
{
  tim_pwm_tbl_t *p_tbl;

  if(timPwmIsValidCh(ch) != true)
  {
    return false;
  }

  p_tbl = &tim_pwm_tbl[ch];

  if(p_tbl->is_open != true)
  {
    return false;
  }

  p_tbl->is_busy = true;
  p_tbl->is_count_mode = false;
  p_tbl->remain_count = 0;

  __HAL_TIM_SET_COUNTER(p_tbl->p_tim, 0);

  return HAL_TIM_PWM_Start(p_tbl->p_tim, p_tbl->tim_channel) == HAL_OK;
}

bool timPwmStartCount(uint8_t ch, uint32_t count)
{
  tim_pwm_tbl_t *p_tbl;

  if(timPwmIsValidCh(ch) != true || count == 0)
  {
    return false;
  }

  p_tbl = &tim_pwm_tbl[ch];

  if(p_tbl->is_open != true)
  {
    return false;
  }

  p_tbl->remain_count = count;
  p_tbl->is_count_mode = true;
  p_tbl->is_busy = true;

  __HAL_TIM_SET_COUNTER(p_tbl->p_tim, 0);

  return HAL_TIM_PWM_Start_IT(p_tbl->p_tim, p_tbl->tim_channel) == HAL_OK;
}

bool timPwmStop(uint8_t ch)
{
  tim_pwm_tbl_t *p_tbl;

  if(timPwmIsValidCh(ch) != true)
  {
    return false;
  }

  p_tbl = &tim_pwm_tbl[ch];

  HAL_TIM_PWM_Stop_IT(p_tbl->p_tim, p_tbl->tim_channel);
  HAL_TIM_PWM_Stop(p_tbl->p_tim, p_tbl->tim_channel);

  p_tbl->is_busy = false;
  p_tbl->is_count_mode = false;
  p_tbl->remain_count = 0;

  return true;
}

bool timPwmIsBusy(uint8_t ch)
{
  if(timPwmIsValidCh(ch) != true)
  {
    return false;
  }

  return tim_pwm_tbl[ch].is_busy;
}

uint32_t timPwmGetRemainCount(uint8_t ch)
{
  if(timPwmIsValidCh(ch) != true)
  {
    return 0;
  }

  return tim_pwm_tbl[ch].remain_count;
}

void timPwmCallback(TIM_HandleTypeDef *htim)
{
  for(uint8_t i = 0; i < TIM_PWM_MAX_CH; i++)
  {
    tim_pwm_tbl_t *p_tbl = &tim_pwm_tbl[i];

    if(htim != p_tbl->p_tim)
    {
      continue;
    }

    if(p_tbl->is_busy != true || p_tbl->is_count_mode != true)
    {
      return;
    }

    if(p_tbl->remain_count > 0)
    {
      p_tbl->remain_count--;
    }

    if(p_tbl->remain_count == 0)
    {
      timPwmStop(i);
    }

    return;
  }
}

void TIM1_UP_TIM16_IRQHandler(void)
{
  HAL_TIM_IRQHandler(&htim16);
}

void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim)
{
  timPwmCallback(htim);
}

#ifdef _USE_HW_CLI
static void cliTimPwm(cli_args_t *args)
{
  bool ret = false;

  if(args->argc == 1 && args->isStr(0, "info") == true)
  {
    for(uint8_t i = 0; i < TIM_PWM_MAX_CH; i++)
    {
      tim_pwm_tbl_t *p_tbl = &tim_pwm_tbl[i];

      cliPrintf("ch %d\n", i);
      cliPrintf("  open   : %d\n", p_tbl->is_open);
      cliPrintf("  busy   : %d\n", p_tbl->is_busy);
      cliPrintf("  output : %s\n", p_tbl->output_type == TIM_PWM_OUTPUT_OD ? "od" : "pp");
      cliPrintf("  active : %s\n", p_tbl->active_level == TIM_PWM_ACTIVE_LOW ? "low" : "high");
      cliPrintf("  period : %dus\n", (int)p_tbl->period_us);
      cliPrintf("  pulse  : %dus\n", (int)p_tbl->active_us);
      cliPrintf("  remain : %d\n", (int)p_tbl->remain_count);
    }

    ret = true;
  }

  if(args->argc == 4 && args->isStr(0, "open") == true)
  {
    uint8_t ch = (uint8_t)args->getData(1);
    tim_pwm_output_t output_type = TIM_PWM_OUTPUT_PP;
    tim_pwm_active_t active_level = TIM_PWM_ACTIVE_HIGH;
    bool result = false;

    if(args->isStr(2, "od") == true)
    {
      output_type = TIM_PWM_OUTPUT_OD;
    }
    else if(args->isStr(2, "pp") == true)
    {
      output_type = TIM_PWM_OUTPUT_PP;
    }
    else
    {
      cliPrintf("tim_pwm open %d fail\n", ch);
      ret = true;
    }

    if(ret != true)
    {
      if(args->isStr(3, "low") == true)
      {
        active_level = TIM_PWM_ACTIVE_LOW;
      }
      else if(args->isStr(3, "high") == true)
      {
        active_level = TIM_PWM_ACTIVE_HIGH;
      }
      else
      {
        cliPrintf("tim_pwm open %d fail\n", ch);
        ret = true;
      }
    }

    if(ret != true)
    {
      result = timPwmOpen(ch, output_type, active_level);
      cliPrintf("tim_pwm open %d : %s\n", ch, result ? "ok" : "fail");
      ret = true;
    }
  }

  if(args->argc == 4 && args->isStr(0, "set") == true)
  {
    uint8_t ch = (uint8_t)args->getData(1);
    uint32_t period_us = (uint32_t)args->getData(2);
    uint32_t active_us = (uint32_t)args->getData(3);
    bool result;

    result = timPwmSetPulse(ch, period_us, active_us);
    cliPrintf("tim_pwm set %d : %s\n", ch, result ? "ok" : "fail");

    ret = true;
  }

  if(args->argc == 2 && args->isStr(0, "start") == true)
  {
    uint8_t ch = (uint8_t)args->getData(1);
    bool result;

    result = timPwmStart(ch);
    cliPrintf("tim_pwm start %d : %s\n", ch, result ? "ok" : "fail");

    ret = true;
  }

  if(args->argc == 3 && args->isStr(0, "count") == true)
  {
    uint8_t ch = (uint8_t)args->getData(1);
    uint32_t count = (uint32_t)args->getData(2);
    bool result;

    result = timPwmStartCount(ch, count);
    cliPrintf("tim_pwm count %d %d : %s\n", ch, (int)count, result ? "ok" : "fail");

    ret = true;
  }

  if(args->argc == 2 && args->isStr(0, "stop") == true)
  {
    uint8_t ch = (uint8_t)args->getData(1);
    bool result;

    result = timPwmStop(ch);
    cliPrintf("tim_pwm stop %d : %s\n", ch, result ? "ok" : "fail");

    ret = true;
  }

  if(ret != true)
  {
    cliPrintf("tim_pwm info\n");
    cliPrintf("tim_pwm open ch[0~%d] pp:od high:low\n", TIM_PWM_MAX_CH - 1);
    cliPrintf("tim_pwm set ch[0~%d] period_us active_us\n", TIM_PWM_MAX_CH - 1);
    cliPrintf("tim_pwm start ch[0~%d]\n", TIM_PWM_MAX_CH - 1);
    cliPrintf("tim_pwm count ch[0~%d] count\n", TIM_PWM_MAX_CH - 1);
    cliPrintf("tim_pwm stop ch[0~%d]\n", TIM_PWM_MAX_CH - 1);
  }
}
#endif

#endif
