/*
 * tim.h
 *
 *  Created on: May 14, 2026
 *      Author: young
 */

#ifndef SRC_COMMON_HW_INCLUDE_TIM_H_
#define SRC_COMMON_HW_INCLUDE_TIM_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_TIM

#define TIM_MAX_CH  HW_USE_TIM_MAX_CH

typedef void (*tim_update_cb_t)(uint8_t ch, void *arg);

bool timInit(void);                                                // Initialize timer table
TIM_HandleTypeDef *timGetHandle(uint8_t ch);                       // Get HAL timer handle

bool timStart(uint8_t ch);                                         // Start timer without interrupt
bool timStop(uint8_t ch);                                          // Stop timer without interrupt

bool timStartIT(uint8_t ch);                                       // Start timer interrupt
bool timStopIT(uint8_t ch);                                        // Stop timer interrupt

bool timSetPeriodUs(uint8_t ch, uint32_t period_us);               // Set timer period in us
bool timSetTickHz(uint8_t ch, uint32_t tick_hz);                   // Set timer tick frequency
uint32_t timGetTickHz(uint8_t ch);                                 // Get timer tick frequency

bool timAttachUpdateCallback(uint8_t ch, tim_update_cb_t cb, void *arg); // Attach update callback
void timPeriodElapsedCallback(TIM_HandleTypeDef *htim);            // Dispatch HAL timer callback
bool timDetachUpdateCallback(uint8_t ch);                         // Detach update callback

#endif

#ifdef __cplusplus
}
#endif

#endif

