/*
 * led.c
 *
 *  Created on: May 6, 2026
 *      Author: TEMP
 */


#include "led.h"
#include "cli.h"


#ifdef _USE_HW_LED

typedef struct
{
  GPIO_TypeDef *port;		// port GPIOA, GPIOB, GPIOC 등
  uint16_t      pin;		// GPIO_PIN_x
  GPIO_PinState on_state;	// Active HIGH
  GPIO_PinState off_state;	// Active LOW
} led_tbl_t;


led_tbl_t led_tbl[LED_MAX_CH] =
    {
    		{GPIOB, GPIO_PIN_3, GPIO_PIN_SET, GPIO_PIN_RESET},
    };


#ifdef _USE_HW_CLI
static void cliLed(cli_args_t *args);
#endif


bool ledInit(void)
{
  bool ret = true;
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOB_CLK_ENABLE();	// GPIO 포트들을 쓸 수 있도록 하는 함수


  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

  for (int i=0; i<LED_MAX_CH; i++)
  {
    GPIO_InitStruct.Pin = led_tbl[i].pin;
    HAL_GPIO_Init(led_tbl[i].port, &GPIO_InitStruct);

    ledOff(i);
  }

#ifdef _USE_HW_CLI
  cliAdd("led", cliLed);
#endif

  return ret;
}

void ledOn(uint8_t ch)
{
  if (ch >= LED_MAX_CH) return;

  HAL_GPIO_WritePin(led_tbl[ch].port, led_tbl[ch].pin, led_tbl[ch].on_state);
}

void ledOff(uint8_t ch)
{
  if (ch >= LED_MAX_CH) return;

  HAL_GPIO_WritePin(led_tbl[ch].port, led_tbl[ch].pin, led_tbl[ch].off_state);
}

void ledToggle(uint8_t ch)
{
  if (ch >= LED_MAX_CH) return;

  HAL_GPIO_TogglePin(led_tbl[ch].port, led_tbl[ch].pin);
}





#ifdef _USE_HW_CLI

void cliLed(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 3 && args->isStr(0, "toggle") == true)
  {
    uint8_t  led_ch;
    uint32_t toggle_time;
    uint32_t pre_time;

    led_ch      = (uint8_t)args->getData(1);
    toggle_time = (uint32_t)args->getData(2);

    if (led_ch > 0)
    {
      led_ch--;
    }

    pre_time = millis();
    while(cliKeepLoop())
    {
      if (millis()-pre_time >= toggle_time)
      {
        pre_time = millis();
        ledToggle(led_ch);
      }
    }

    ret = true;
  }


  if (ret != true)
  {
    cliPrintf("led toggle ch[1~%d] time_ms\n", LED_MAX_CH);
  }
}


#endif


#endif
