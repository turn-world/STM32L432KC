/*
 * tim_pwm.c
 *
 *  Created on: May 14, 2026
 *      Author: young
 */

#include "pwm.h"

#ifdef _USE_TIM_PWM

#ifdef _USE_HW_CLI
#include "cli.h"
#endif

static TIM_HandleTypeDef htim16;

typedef struct
{
  TIM_HandleTypeDef *p_tim;
  uint32_t tim_channel;

  bool is_open;
  bool is_busy;
  bool is_count_mode;
  uint32_t remain_count;
} tim_pwm_tbl_t;

typedef struct
{
  uint32_t prescaler;
  uint32_t period;
  uint32_t pulse;
} tim_pwm_cfg_t;

static tim_pwm_tbl_t tim_pwm_tbl[TIM_PWM_MAX_CH] =
{
  {
    .p_tim          = &htim16,
    .tim_channel    = TIM_CHANNEL_1,

    .is_open        = false,
    .is_busy        = false,
    .is_count_mode  = false,
    .remain_count   = 0,
  },
};

static tim_pwm_cfg_t pwm_cfg[TIM_PWM_MAX_CH] =
{
  { 79, 999, 5 },
};

#ifdef _USE_HW_CLI
static void cliTimPwm(cli_args_t *args);
#endif

bool timPwmInit(void)
{
  for(uint8_t i = 0; i < TIM_PWM_MAX_CH; i++)
  {
    tim_pwm_tbl[i].is_open       = false;
    tim_pwm_tbl[i].is_busy       = false;
    tim_pwm_tbl[i].is_count_mode = false;
    tim_pwm_tbl[i].remain_count  = 0;

    timPwmOpen(i);
  }

#ifdef _USE_HW_CLI
  cliAdd("tim_pwm", cliTimPwm);
#endif

  return true;
}

bool timPwmOpen(uint8_t ch)
{
  TIM_HandleTypeDef *p_tim;
  TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};

  if(ch >= TIM_PWM_MAX_CH)
  {
    return false;
  }

  p_tim = tim_pwm_tbl[ch].p_tim;

  if(p_tim->Instance == NULL)
  {
    switch(ch)
    {
      case _DEF_PWM1:
        __HAL_RCC_TIM16_CLK_ENABLE();

        p_tim->Instance               = TIM16;
        p_tim->Init.Prescaler         = pwm_cfg[ch].prescaler;
        p_tim->Init.CounterMode       = TIM_COUNTERMODE_UP;
        p_tim->Init.Period            = pwm_cfg[ch].period;
        p_tim->Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
        p_tim->Init.RepetitionCounter = 0;
        p_tim->Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

        if(HAL_TIM_Base_Init(p_tim) != HAL_OK)
        {
          p_tim->Instance = NULL;
          return false;
        }

        if(HAL_TIM_PWM_Init(p_tim) != HAL_OK)
        {
          p_tim->Instance = NULL;
          return false;
        }

        sBreakDeadTimeConfig.OffStateRunMode  = TIM_OSSR_DISABLE;
        sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
        sBreakDeadTimeConfig.LockLevel        = TIM_LOCKLEVEL_OFF;
        sBreakDeadTimeConfig.DeadTime         = 0;
        sBreakDeadTimeConfig.BreakState       = TIM_BREAK_DISABLE;
        sBreakDeadTimeConfig.BreakPolarity    = TIM_BREAKPOLARITY_HIGH;
        sBreakDeadTimeConfig.AutomaticOutput  = TIM_AUTOMATICOUTPUT_DISABLE;

        if(HAL_TIMEx_ConfigBreakDeadTime(p_tim, &sBreakDeadTimeConfig) != HAL_OK)
        {
          p_tim->Instance = NULL;
          return false;
        }
        break;
    }
  }

  tim_pwm_tbl[ch].is_open = true;

  return true;
}

bool timPwmIsOpen(uint8_t ch)
{
  if(ch >= TIM_PWM_MAX_CH)  return false;

  return tim_pwm_tbl[ch].is_open;
}

bool timPwmIsBusy(uint8_t ch)
{
  if(ch >= TIM_PWM_MAX_CH)  return false;

  return tim_pwm_tbl[ch].is_busy;
}

bool timPwmStart(uint8_t ch)
{
  bool ret = true;
  TIM_HandleTypeDef *p_tim;

  if(ch >= TIM_PWM_MAX_CH)  return false;
  if(tim_pwm_tbl[ch].is_open != true) return false;

  p_tim = tim_pwm_tbl[ch].p_tim;
  __HAL_TIM_SET_COUNTER(p_tim, 0);

  if(HAL_TIM_PWM_Start(p_tim, tim_pwm_tbl[ch].tim_channel) != HAL_OK) return false;

  tim_pwm_tbl[ch].is_busy = true;
  return ret;
}

bool timPwmStop(uint8_t ch)
{
  bool ret = true;
  TIM_HandleTypeDef *p_tim;

  if(ch >= TIM_PWM_MAX_CH) return false;
  if(tim_pwm_tbl[ch].is_open != true) return false;

  p_tim = tim_pwm_tbl[ch].p_tim;

  if(HAL_TIM_PWM_Stop(p_tim, tim_pwm_tbl[ch].tim_channel) != HAL_OK) return false;

  tim_pwm_tbl[ch].is_busy = false;

  return ret;
}

bool timPwmSetGpioMode(uint8_t ch, uint32_t mode)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  if(ch >= TIM_PWM_MAX_CH) return false;

  switch(ch)
  {
    case _DEF_PWM1:

      GPIO_InitStruct.Pin       = GPIO_PIN_6;
      GPIO_InitStruct.Mode      = mode;
      GPIO_InitStruct.Pull      = GPIO_NOPULL;
      GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_LOW;
      GPIO_InitStruct.Alternate = GPIO_AF14_TIM16;

      HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
      break;
  }

  return true;
}

bool timPwmSetPrescaler(uint8_t ch, uint32_t prescaler)
{
  TIM_HandleTypeDef *p_tim;

  if(ch >= TIM_PWM_MAX_CH)  return false;
  if(prescaler > 0xFFFFU) return false;

  p_tim = tim_pwm_tbl[ch].p_tim;

  if(p_tim->Instance == NULL) return false;

  p_tim->Init.Prescaler = prescaler;
  __HAL_TIM_SET_PRESCALER(p_tim, prescaler);
  HAL_TIM_GenerateEvent(p_tim, TIM_EVENTSOURCE_UPDATE);

  return true;
}

bool timPwmSetPeriod(uint8_t ch,uint32_t period)
{
  TIM_HandleTypeDef *p_tim;

  if(ch >= TIM_PWM_MAX_CH) return false;
  if(period > 0xFFFFU)  return false;

  p_tim = tim_pwm_tbl[ch].p_tim;

  if(p_tim->Instance == NULL) return false;

  p_tim->Init.Period = period;
  __HAL_TIM_SET_AUTORELOAD(p_tim, period);
  __HAL_TIM_SET_COUNTER(p_tim, 0);
  HAL_TIM_GenerateEvent(p_tim, TIM_EVENTSOURCE_UPDATE);

  return true;
}

bool timPwmSetPulse(uint8_t ch,uint32_t pulse)
{
  TIM_HandleTypeDef *p_tim;
  TIM_OC_InitTypeDef sConfigOC = {0};

  if(ch >= TIM_PWM_MAX_CH)  return false;

  p_tim = tim_pwm_tbl[ch].p_tim;

  if(p_tim->Instance == NULL) return false;
switch(ch)
{
  case _DEF_PWM1:
  sConfigOC.OCMode       = TIM_OCMODE_PWM1;
  sConfigOC.Pulse        = pulse;
  sConfigOC.OCPolarity   = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity  = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode   = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState  = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;

  if(HAL_TIM_PWM_ConfigChannel(p_tim, &sConfigOC, tim_pwm_tbl[ch].tim_channel) != HAL_OK)
  {
    return false;
  }

  __HAL_TIM_SET_COMPARE(p_tim, tim_pwm_tbl[ch].tim_channel, pulse);
}

  return true;
}

#ifdef _USE_HW_CLI
static void cliTimPwm(cli_args_t *args)
{
  bool ret = false;

  if(args->argc == 1 && args->isStr(0, "info") == true)
  {
    for(uint8_t i = 0; i < TIM_PWM_MAX_CH; i++)
    {
      TIM_HandleTypeDef *p_tim = tim_pwm_tbl[i].p_tim;

      cliPrintf("ch %d\n", i);
      cliPrintf("  open : %d\n", tim_pwm_tbl[i].is_open);
      cliPrintf("  busy : %d\n", tim_pwm_tbl[i].is_busy);
      cliPrintf("  psc  : %d\n", (int)pwm_cfg[i].prescaler);
      cliPrintf("  arr  : %d\n", (int)pwm_cfg[i].period);
      cliPrintf("  pulse: %d\n", (int)pwm_cfg[i].pulse);

      if(p_tim->Instance != NULL)
      {
        cliPrintf("  run  : %d\n", (p_tim->Instance->CR1 & TIM_CR1_CEN) ? 1 : 0);
      }
    }

    ret = true;
  }

  if(args->argc == 3 && args->isStr(0, "start") == true)
  {
    uint8_t ch = (uint8_t)args->getData(1);
    uint32_t run_ms = (uint32_t)args->getData(2);
    bool result;

    result = timPwmStart(ch);

    if(result == true)
    {
      delay(run_ms);
      result = timPwmStop(ch);
    }

    cliPrintf("tim_pwm start %d %dms : %s\n", ch, (int)run_ms, result ? "ok" : "fail");

    ret = true;
  }

  if(args->argc == 3 && args->isStr(0, "start_us") == true)
  {
    uint8_t ch = (uint8_t)args->getData(1);
    uint32_t run_us = (uint32_t)args->getData(2);
    bool result;

    result = timPwmStart(ch);

    if(result == true)
    {
      delayUs(run_us);
      result = timPwmStop(ch);
    }

    cliPrintf("tim_pwm start_us %d %dus : %s\n", ch, (int)run_us, result ? "ok" : "fail");

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

  if(args->argc == 6 && args->isStr(0, "set") == true)
  {
    uint8_t ch = (uint8_t)args->getData(1);
    uint32_t mode;
    pwm_cfg[ch].prescaler = (uint32_t)args->getData(3);
    pwm_cfg[ch].period = (uint32_t)args->getData(4);
    pwm_cfg[ch].pulse = (uint32_t)args->getData(5);
    bool result;

    if(args->isStr(2, "od") == true)
    {
      mode = GPIO_MODE_AF_OD;
    }
    else
    {
      mode = GPIO_MODE_AF_PP;
    }

    result = timPwmSetGpioMode(ch, mode);
    result &= timPwmSetPrescaler(ch, pwm_cfg[ch].prescaler);
    result &= timPwmSetPeriod(ch, pwm_cfg[ch].period);
    result &= timPwmSetPulse(ch, pwm_cfg[ch].pulse);

    cliPrintf("tim_pwm set %d : %s\n", ch, result ? "ok" : "fail");

    ret = true;
  }

  if(ret != true)
  {
    cliPrintf("tim_pwm info\n");
    cliPrintf("tim_pwm open ch[0~%d]\n", TIM_PWM_MAX_CH - 1);
    cliPrintf("tim_pwm set ch[0~%d] pp:od prescaler period pulse\n", TIM_PWM_MAX_CH - 1);
    cliPrintf("tim_pwm start ch[0~%d] run_ms\n", TIM_PWM_MAX_CH - 1);
    cliPrintf("tim_pwm start_us ch[0~%d] run_us\n", TIM_PWM_MAX_CH - 1);
    cliPrintf("tim_pwm stop ch[0~%d]\n", TIM_PWM_MAX_CH - 1);
  }
}
#endif

#endif
