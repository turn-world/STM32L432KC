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

  //ledInit();

  uartInit();
  gpioInit();
  canInit();
  spiInit();
  lcdInit();
  rtcInit();
  fatfsInit();
  adcInit();
}
