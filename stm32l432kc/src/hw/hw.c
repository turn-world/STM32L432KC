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

  cliInit();
  ledInit();
  uartInit();
  gpioInit();
  canInit();
  adcInit();
  appsInit(APPS_SIGNAL1_ADC, APPS_SIGNAL2_ADC, _DEF_CAN1);
  appsCliInit();

  /*
   * Enable this block when CAN1 should start automatically at boot.
   *
   * canOpen(_DEF_CAN1,
   *         CAN_NORMAL,
   *         CAN_CLASSIC,
   *         CAN_500K,
   *         CAN_500K);
   */
}
