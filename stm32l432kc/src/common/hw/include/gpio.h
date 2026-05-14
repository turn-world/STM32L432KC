/*
 * gpio.h
 *
 *  Created on: Aug 7, 2025
 *      Author: young
 */

#ifndef SRC_COMMON_HW_INCLUDE_GPIO_H_
#define SRC_COMMON_HW_INCLUDE_GPIO_H_

#include "hw_def.h"

#ifdef _USE_HW_GPIO
#define GPIO_MAX_CH			HW_GPIO_MAX_CH

bool gpioInit(void);
bool gpioPinMode(uint8_t ch, uint8_t mode);
void gpioPinWrite(uint8_t ch, bool value);
bool gpioPinRead(uint8_t ch);
void gpioPinToggle(uint8_t ch);
void gpioPinToggleCount(uint8_t ch, uint32_t count, uint32_t delay_ms);
void gpioPinPulseCount(uint8_t ch, uint32_t count, uint32_t delay_ms);

#endif

#endif /* SRC_COMMON_HW_INCLUDE_GPIO_H_ */
