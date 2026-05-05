/*
 * ap.c
 *
 *  Created on: Dec 6, 2020
 *      Author: baram
 */


#include "ap.h"




void apInit(void)
{
  cliOpen(_DEF_UART1, 57600);

  /*테스트용*/
  spiBegin(_DEF_SPI1);

  uint8_t tx[2];

  spiDmaTxTransfer(_DEF_SPI1, tx, 2, 100);


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

    cliMain();
  }
}

