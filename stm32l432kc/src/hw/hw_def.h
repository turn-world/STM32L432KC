/*
 * hw_def.h
 *
 *  Created on: Dec 6, 2020
 *      Author: baram
 *
 *      하드웨어가 공통으로 쓸 관련된 정의들
 */

#ifndef SRC_HW_HW_DEF_H_
#define SRC_HW_HW_DEF_H_

#include "def.h"
#include "bsp.h"

#define _USE_HW_LED
#define      HW_LED_MAX_CH          1

#define _USE_HW_UART
#define      HW_UART_MAX_CH         1

#define _USE_HW_CLI
#define      HW_CLI_CMD_LIST_MAX    16
#define      HW_CLI_CMD_NAME_MAX    16
#define      HW_CLI_LINE_HIS_MAX    4
#define      HW_CLI_LINE_BUF_MAX    64

#define	_USE_HW_BUTTON
#define		 HW_BUTTON_MAX_CH		1

#define	_USE_HW_GPIO
#define		 HW_GPIO_MAX_CH			    0

#define _PIN_GPIO_SDCARD_DETECT		0

#define _USE_HW_CAN
#define      HW_CAN_FD              0
#define      HW_CAN_MAX_CH          1
#define      HW_CAN_MSG_RX_BUF_MAX  32

#define _USE_HW_ADC
#define      HW_ADC_MAX_CH          2

#endif /* SRC_HW_HW_DEF_H_ */
