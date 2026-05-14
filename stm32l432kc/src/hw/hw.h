/*
 * hw.h
 *
 *  Created on: Dec 6, 2020
 *      Author: baram
 */

#ifndef SRC_HW_HW_H_
#define SRC_HW_HW_H_


#include "hw_def.h"


#include "led.h"
#include "uart.h"
#include "cli.h"
#include "gpio.h"
#include "can.h"
#include "tim.h"
#include "tim_gpio.h"
#include "tim_pwm.h"
#include "DM542/dm542.h"
#include "tim_pwm.h"

void hwInit(void);


#endif /* SRC_HW_HW_H_ */
