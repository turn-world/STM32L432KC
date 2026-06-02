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

<<<<<<< Updated upstream
  //canInit();
=======
  timPwmInit();
  // timInit();
  // timGpioInit();


  dm542Init();
  canInit();
>>>>>>> Stashed changes
}
