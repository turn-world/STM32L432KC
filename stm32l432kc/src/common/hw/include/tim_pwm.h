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

extern TIM_HandleTypeDef htim16;

typedef enum
{
  TIM_PWM_OUTPUT_PP = 0,
  TIM_PWM_OUTPUT_OD,
} tim_pwm_output_t;

typedef enum
{
  TIM_PWM_ACTIVE_HIGH = 0,
  TIM_PWM_ACTIVE_LOW,
} tim_pwm_active_t;

bool timPwmInit(void);                                                        // Initialize TIM PWM module
bool timPwmOpen(uint8_t ch, tim_pwm_output_t output_type,
                tim_pwm_active_t active_level);                               // Configure PWM output mode

bool timPwmSetPulse(uint8_t ch, uint32_t period_us, uint32_t active_us);       // Set PWM period and active width
bool timPwmStart(uint8_t ch);                                                  // Start continuous PWM output
bool timPwmStartCount(uint8_t ch, uint32_t count);                             // Start PWM and stop after count pulses

bool timPwmStop(uint8_t ch);                                                   // Stop PWM output
bool timPwmIsBusy(uint8_t ch);                                                 // Check PWM running state

uint32_t timPwmGetRemainCount(uint8_t ch);                                     // Get remaining pulse count
void timPwmCallback(TIM_HandleTypeDef *htim);                                  // Run from HAL PWM callback

void TIM1_UP_TIM16_IRQHandler(void);                                           // TIM16 interrupt handler
void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim);               // HAL PWM pulse finished callback

#endif

#ifdef __cplusplus
}
#endif


#endif /* SRC_COMMON_HW_INCLUDE_TIM_PWM_H_ */
