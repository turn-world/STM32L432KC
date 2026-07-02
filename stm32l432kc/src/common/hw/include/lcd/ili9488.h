/*
 * ili9488.h
 *
 *  Created on: Jun 25, 2026
 *      Author: young
 */

#ifndef SRC_COMMON_HW_INCLUDE_LCD_ILI9488_H_
#define SRC_COMMON_HW_INCLUDE_LCD_ILI9488_H_

#include "hw_def.h"

#ifdef _USE_HW_ILI9488

#include "lcd.h"
#include "ili9488_reg.h"

bool ili9488Init(void);
bool ili9488Reset(void);
bool ili9488InitDriver(lcd_driver_t *p_driver);
void ili9488SetWindow(int32_t x, int32_t y, int32_t x_end, int32_t y_end);
void ili9488SetRotation(uint8_t rotation);
uint16_t ili9488GetWidth(void);
uint16_t ili9488GetHeight(void);
bool ili9488SetCallBack(void (*p_func)(void));
bool ili9488SendBuffer(uint8_t *p_data, uint32_t length, uint32_t timeout_ms);

#endif

#endif /* SRC_COMMON_HW_INCLUDE_LCD_ILI9488_H_ */
