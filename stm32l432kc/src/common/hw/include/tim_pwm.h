/*
 * tim_pwm.h
 *
 *  Created on: May 14, 2026
 *      Author: young
 */

#ifndef SRC_COMMON_HW_INCLUDE_TIM_PWM_H_
#define SRC_COMMON_HW_INCLUDE_TIM_PWM_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_TIM_PWM

#define TIM_PWM_MAX_CH  HW_TIM_PWM_MAX_CH

typedef enum
{
  TIM_PWM_ACTIVE_HIGH = 0,
  TIM_PWM_ACTIVE_LOW,
} tim_pwm_active_t;


#endif

bool timPwmInit(void);
bool timPwmOpen(uint8_t ch);
bool timPwmGpio(uint8_t ch);

bool timPwmIsOpen();
bool timPwmIsBusy();

bool timPwmStart();
bool timPwmStop();

bool timPwmSetGpioMode();
bool timPwmSetCfg();

#ifdef __cplusplus
}
#endif


#endif /* SRC_COMMON_HW_INCLUDE_TIM_PWM_H_ */
