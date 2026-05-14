/*
 * tim_pwm.c
 *
 *  Created on: May 14, 2026
 *      Author: young
 */

#include "tim_pwm.h"

#ifdef _USE_TIM_PWM

#define _USE_TIM1

#ifdef _USE_HW_CLI
#include "cli.h"
#endif

#ifdef _USE_TIM1

static TIM_HandleTypeDef htim16;

#endif

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
  bool ret = true;

  for(uint8_t i = 0; i < TIM_PWM_MAX_CH; i++)
  {
    tim_pwm_tbl[i].is_open 			= false;
    tim_pwm_tbl[i].is_busy 			= false;
    tim_pwm_tbl[i].is_count_mode 		= false;
    tim_pwm_tbl[i].remain_count 		= 0;
  }

#ifdef _USE_HW_CLI
  cliAdd("tim_pwm", cliTimPwm);
#endif

  return ret;
}

bool timPwmOpen(uint8_t ch)
{
  bool ret = true;
  TIM_HandleTypeDef *r_tim;

  if(ch >= TIM_PWM_MAX_CH) return false;

  r_tim = tim_pwm_tbl[ch].p_tim;

  if( r_tim->Instance != NULL ) return ret;

  switch(ch)
  {
    case _DEF_TIM1:
	TIM_OC_InitTypeDef sConfigOC = {0};
	TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};
	__HAL_RCC_TIM16_CLK_ENABLE();

	r_tim->Instance                  = TIM16;
	r_tim->Init.Prescaler            = pwm_cfg[ch].prescaler;
	r_tim->Init.CounterMode          = TIM_COUNTERMODE_UP;
	r_tim->Init.Period               = pwm_cfg[ch].period;
	r_tim->Init.ClockDivision        = TIM_CLOCKDIVISION_DIV1;
	r_tim->Init.RepetitionCounter    = 0;
	r_tim->Init.AutoReloadPreload    = TIM_AUTORELOAD_PRELOAD_DISABLE;

	if(HAL_TIM_Base_Init(r_tim) != HAL_OK)
	{
	    return false;
	}

	if(HAL_TIM_PWM_Init(r_tim) != HAL_OK)
	{
		  return false;
	}

	sConfigOC.OCMode              = TIM_OCMODE_PWM1;
	sConfigOC.Pulse               = pwm_cfg[ch].pulse;
	sConfigOC.OCPolarity          = TIM_OCPOLARITY_HIGH;
	sConfigOC.OCNPolarity         = TIM_OCNPOLARITY_HIGH;
	sConfigOC.OCFastMode          = TIM_OCFAST_DISABLE;
	sConfigOC.OCIdleState         = TIM_OCIDLESTATE_RESET;
	sConfigOC.OCNIdleState        = TIM_OCNIDLESTATE_RESET;

	if(HAL_TIM_PWM_ConfigChannel(r_tim, &sConfigOC, tim_pwm_tbl[ch].tim_channel) != HAL_OK)
	{
	  return false;
	}

	if(timPwmConfigChannel(p_tbl) != true)
	{
	  return false;
	}

	sBreakDeadTimeConfig.OffStateRunMode 		= TIM_OSSR_DISABLE;
	sBreakDeadTimeConfig.OffStateIDLEMode 		= TIM_OSSI_DISABLE;
	sBreakDeadTimeConfig.LockLevel 			= TIM_LOCKLEVEL_OFF;
	sBreakDeadTimeConfig.DeadTime 			= 0;
	sBreakDeadTimeConfig.BreakState 		= TIM_BREAK_DISABLE;
	sBreakDeadTimeConfig.BreakPolarity 		= TIM_BREAKPOLARITY_HIGH;
	sBreakDeadTimeConfig.AutomaticOutput 		= TIM_AUTOMATICOUTPUT_DISABLE;

	if(HAL_TIMEx_ConfigBreakDeadTime(r_tim, &sBreakDeadTimeConfig) != HAL_OK)
	{
	  return false;
	}
	break;
  }

  return ret;
}

bool timPwmGpio(uint8_t ch)
{
  bool ret = true;

  if(ch >= TIM_PWM_MAX_CH) return false;

  switch(ch)
  {
    case _DEF_PWM1:
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitStruct.Pin 			= GPIO_PIN_6;
    GPIO_InitStruct.Mode 			= GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Pull 			= GPIO_NOPULL;
    GPIO_InitStruct.Speed 			= GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate 			= GPIO_AF14_TIM16;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    break;
  }

  return ret;
}



#endif
