/*
 * tim_gpio.h
 *
 *  Created on: May 14, 2026
 *      Author: young
 */

#ifndef SRC_COMMON_HW_INCLUDE_TIM_GPIO_H_
#define SRC_COMMON_HW_INCLUDE_TIM_GPIO_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_TIM_GPIO

#define TIM_GPIO_MAX_CH  HW_TIM_GPIO_MAX_CH

typedef enum
{
  TIM_GPIO_OUTPUT_PP = 0,
  TIM_GPIO_OUTPUT_OD,
} tim_gpio_output_t;

bool timGpioInit(void);                                                          // Initialize TIM-GPIO module
bool timGpioOpen(uint8_t ch, uint8_t tim_ch, uint8_t gpio_ch);                   // Bind timer and GPIO channel
bool timGpioStart(uint8_t ch, tim_gpio_output_t output_type,
                  uint32_t period_us, uint32_t active_us, uint32_t count);       // Start GPIO pulse output
bool timGpioStop(uint8_t ch);                                                    // Stop GPIO pulse output
bool timGpioIsBusy(uint8_t ch);                                                  // Check output busy state

#endif

#ifdef __cplusplus
}
#endif

#endif

