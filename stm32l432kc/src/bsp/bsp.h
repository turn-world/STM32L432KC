/*
 * bsp.h
 *
 *  Created on: Dec 6, 2020
 *      Author: baram
 */

#ifndef SRC_BSP_BSP_H_
#define SRC_BSP_BSP_H_


#include "def.h"
#include "stm32l4xx_hal.h"

#define _USE_LOG_PRINT    1

#if _USE_LOG_PRINT
#define logPrintf(fmt, ...)     printf(fmt, ##__VA_ARGS__)
#else
#define logPrintf(fmt, ...)
#endif


void bspInit(void);
bool delayUsInit(void);  // us delay counter init
void delayUs(uint32_t us); // us delay
uint32_t micros(void);  // us counter

void delay(uint32_t ms); // ms단위로 지연 시키는 함수
uint32_t millis(void); 	// ms단위로 바뀌는 카운트 값

void Error_Handler(void);


#endif /* SRC_BSP_BSP_H_ */
