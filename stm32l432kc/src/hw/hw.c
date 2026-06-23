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
  adcInit();
  appsInit(APPS_SIGNAL1_ADC, APPS_SIGNAL2_ADC, _DEF_CAN1);

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
