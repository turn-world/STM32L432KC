/*
 * ap.c
 *
 *  Created on: Dec 6, 2020
 *      Author: baram
 */


#include "ap.h"
#include "apps/apps.h"
#include "signal_scope/signal_scope.h"
#include "telemetry/telemetry.h"




void apInit(void)
{
#ifdef _USE_HW_CLI
  cliOpen(_DEF_UART1, 57600);
#endif

  appsInit();

  scopeInit();
  telemetryInit();

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

    /* scope start 명령으로 켜진 경우에만 CLI CSV 스트림을 출력한다. */
    scopeUpdate();

    telemetryUpdate();
  }
}

