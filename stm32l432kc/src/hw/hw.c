/*
 * hw.c
 *
 *  Created on: Dec 6, 2020
 *      Author: baram
 */


#include "hw.h"


void hwInit(void)
{
  bspInit();
#ifdef _USE_HW_CLI
  cliInit();
#endif

  ledInit();

#ifdef _USE_HW_UART
  uartInit();
#endif

  gpioInit();
  canInit();
  spiInit();
  rtcInit();

#ifdef _USE_HW_FATFS
  fatfsInit();
  dataLoggerInit();
#endif

  adcInit();
  appsInit(APPS_SIGNAL1_ADC_CH, APPS_SIGNAL2_ADC_CH, _DEF_CAN1);
}
