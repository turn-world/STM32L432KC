/*
 * tim_pwm.h
 *
 *  Created on: May 14, 2026
 *      Author: young
 */

#ifndef SRC_COMMON_HW_INCLUDE_PWM_H_
#define SRC_COMMON_HW_INCLUDE_PWM_H_

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

bool timPwmInit(void);                                                    // TIM PWM driver init
bool timPwmOpen(uint8_t ch);                                              // Open selected PWM channel

bool timPwmIsOpen(uint8_t ch);                                            // Check channel open state
bool timPwmIsBusy(uint8_t ch);                                            // Check PWM output running state

bool timPwmStart(uint8_t ch);                                             // Start PWM output
bool timPwmStop(uint8_t ch);                                              // Stop PWM output

bool timPwmSetGpioMode(uint8_t ch, uint32_t mode);                        // Configure PWM GPIO alternate function
bool timPwmSetPrescaler(uint8_t ch, uint32_t prescaler);                  // Apply channel prescaler
bool timPwmSetPeriod(uint8_t ch, uint32_t period);                        // Apply channel period
bool timPwmSetPulse(uint8_t ch, uint32_t pulse);                          // Apply channel pulse width

#endif

#ifdef __cplusplus
}
#endif


#endif /* SRC_COMMON_HW_INCLUDE_PWM_H_ */
