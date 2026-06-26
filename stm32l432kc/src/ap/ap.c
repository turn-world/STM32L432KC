/*
 * ap.c
 *
 *  Created on: Dec 6, 2020
 *      Author: baram
 */


#include "ap.h"
#include "apps/apps.h"
#include "telemetry/telemetry.h"




void apInit(void)
{
#ifdef _USE_HW_CLI
  cliOpen(_DEF_UART1, 57600);
#endif

  appsInit();

#ifdef _USE_HW_FATFS
  telemetryInit();
#endif
}

void apMain(void)
{
  uint32_t pre_time;

  pre_time = millis();

  while(1)
  {
    if (millis()-pre_time >= 500)
    {
      pre_time = millis();
      ledToggle(_DEF_LED1);
    }
#ifdef _USE_HW_CLI
    cliMain();
#endif
#ifdef _USE_HW_FATFS
    telemetryUpdate();
#endif
  }
}

