/*
 * tim_gpio.c
 *
 *  Created on: May 14, 2026
 *      Author: young
 */


#include "tim_gpio.h"
#include "tim.h"
#include "gpio.h"

#ifdef _USE_HW_CLI
#include "cli.h"
#endif

#ifdef _USE_TIM_GPIO

typedef struct
{
  bool is_open;
  bool is_busy;
  bool is_active_phase;
  bool is_continuous;

  uint8_t tim_ch;
  uint8_t gpio_ch;

  uint32_t period_us;
  uint32_t active_us;
  uint32_t inactive_us;
  uint32_t count;
} tim_gpio_tbl_t;

static tim_gpio_tbl_t tim_gpio_tbl[TIM_GPIO_MAX_CH];

static void timGpioUpdateCallback(uint8_t tim_ch, void *arg);

#ifdef _USE_HW_CLI
static void cliTimGpio(cli_args_t *args);
#endif

bool timGpioInit(void)
{
  for(uint8_t i = 0; i < TIM_GPIO_MAX_CH; i++)
  {
    tim_gpio_tbl[i].is_open = false;
    tim_gpio_tbl[i].is_busy = false;
  }

#ifdef _USE_HW_CLI
  cliAdd("tim_gpio", cliTimGpio);
#endif

  return true;
}

bool timGpioOpen(uint8_t ch, uint8_t tim_ch, uint8_t gpio_ch)
{
  if(ch >= TIM_GPIO_MAX_CH)
  {
    return false;
  }

  if(tim_ch >= TIM_MAX_CH || gpio_ch >= GPIO_MAX_CH)
  {
    return false;
  }

  tim_gpio_tbl[ch].tim_ch = tim_ch;
  tim_gpio_tbl[ch].gpio_ch = gpio_ch;
  tim_gpio_tbl[ch].is_open = true;
  tim_gpio_tbl[ch].is_busy = false;

  return timAttachUpdateCallback(tim_ch, timGpioUpdateCallback, &tim_gpio_tbl[ch]);
}

bool timGpioStart(uint8_t ch, tim_gpio_output_t output_type,
                  uint32_t period_us, uint32_t active_us, uint32_t count)
{
  tim_gpio_tbl_t *p_tbl;

  if(ch >= TIM_GPIO_MAX_CH)
  {
    return false;
  }

  p_tbl = &tim_gpio_tbl[ch];

  if(p_tbl->is_open != true)
  {
    return false;
  }

  if(active_us == 0 || period_us <= active_us)
  {
    return false;
  }

  if(output_type != TIM_GPIO_OUTPUT_PP && output_type != TIM_GPIO_OUTPUT_OD)
  {
    return false;
  }

  timStopIT(p_tbl->tim_ch);

  if(output_type == TIM_GPIO_OUTPUT_OD)
  {
    if(gpioPinMode(p_tbl->gpio_ch, _DEF_OUTPUT_OPEN_DRAIN) != true)
    {
      return false;
    }
  }
  else
  {
    if(gpioPinMode(p_tbl->gpio_ch, _DEF_OUTPUT) != true)
    {
      return false;
    }
  }

  p_tbl->period_us        = period_us;
  p_tbl->active_us        = active_us;
  p_tbl->inactive_us      = period_us - active_us;
  p_tbl->count            = count;
  p_tbl->is_continuous    = count == 0;
  p_tbl->is_active_phase  = true;
  p_tbl->is_busy          = true;

  gpioPinWrite(p_tbl->gpio_ch, true);
  timSetPeriodUs(p_tbl->tim_ch, active_us);

  return timStartIT(p_tbl->tim_ch);
}

bool timGpioStop(uint8_t ch)
{
  tim_gpio_tbl_t *p_tbl;

  if(ch >= TIM_GPIO_MAX_CH)
  {
    return false;
  }

  p_tbl = &tim_gpio_tbl[ch];

  if(p_tbl->is_open != true)
  {
    return false;
  }

  timStopIT(p_tbl->tim_ch);
  gpioPinWrite(p_tbl->gpio_ch, false);

  p_tbl->is_busy = false;
  p_tbl->is_active_phase = false;

  return true;
}

bool timGpioIsBusy(uint8_t ch)
{
  if(ch >= TIM_GPIO_MAX_CH)
  {
    return false;
  }

  return tim_gpio_tbl[ch].is_busy;
}

static void timGpioUpdateCallback(uint8_t tim_ch, void *arg)
{
  tim_gpio_tbl_t *p_tbl = (tim_gpio_tbl_t *)arg;

  if(p_tbl == NULL)
  {
    return;
  }

  if(p_tbl->is_busy != true)
  {
    return;
  }

  if(p_tbl->tim_ch != tim_ch)
  {
    return;
  }

  if(p_tbl->is_active_phase == true)
  {
    gpioPinWrite(p_tbl->gpio_ch, false);

    if(p_tbl->is_continuous != true)
    {
      if(p_tbl->count > 0)
      {
        p_tbl->count--;
      }

      if(p_tbl->count == 0)
      {
        timGpioStop((uint8_t)(p_tbl - &tim_gpio_tbl[0]));
        return;
      }
    }

    p_tbl->is_active_phase = false;
    timSetPeriodUs(p_tbl->tim_ch, p_tbl->inactive_us);
  }
  else
  {
    gpioPinWrite(p_tbl->gpio_ch, true);

    p_tbl->is_active_phase = true;
    timSetPeriodUs(p_tbl->tim_ch, p_tbl->active_us);
  }
}

#ifdef _USE_HW_CLI
static void cliTimGpio(cli_args_t *args)
{
  bool ret = false;

  if(args->argc == 1 && args->isStr(0, "info") == true)
  {
    for(uint8_t i = 0; i < TIM_GPIO_MAX_CH; i++)
    {
      cliPrintf("ch %d\n", i);
      cliPrintf("  open   : %d\n", tim_gpio_tbl[i].is_open);
      cliPrintf("  busy   : %d\n", tim_gpio_tbl[i].is_busy);
      cliPrintf("  tim_ch : %d\n", tim_gpio_tbl[i].tim_ch);
      cliPrintf("  gpio_ch: %d\n", tim_gpio_tbl[i].gpio_ch);
      cliPrintf("  tick   : %dHz\n", (int)timGetTickHz(tim_gpio_tbl[i].tim_ch));
      cliPrintf("  period : %dus\n", (int)tim_gpio_tbl[i].period_us);
      cliPrintf("  active : %dus\n", (int)tim_gpio_tbl[i].active_us);
      cliPrintf("  count  : %d\n", (int)tim_gpio_tbl[i].count);
    }

    ret = true;
  }

  if(args->argc == 4 && args->isStr(0, "open") == true)
  {
    uint8_t ch = (uint8_t)args->getData(1);
    uint8_t tim_ch = (uint8_t)args->getData(2);
    uint8_t gpio_ch = (uint8_t)args->getData(3);
    bool result;

    result = timGpioOpen(ch, tim_ch, gpio_ch);
    cliPrintf("tim_gpio open %d %d %d : %s\n", ch, tim_ch, gpio_ch, result ? "ok" : "fail");

    ret = true;
  }

  if(args->argc == 3 && args->isStr(0, "tick") == true)
  {
    uint8_t ch = (uint8_t)args->getData(1);
    uint32_t tick_hz = (uint32_t)args->getData(2);
    bool result = false;

    if(ch < TIM_GPIO_MAX_CH && tim_gpio_tbl[ch].is_open == true)
    {
      result = timSetTickHz(tim_gpio_tbl[ch].tim_ch, tick_hz);
    }

    cliPrintf("tim_gpio tick %d %d : %s\n", ch, (int)tick_hz, result ? "ok" : "fail");

    ret = true;
  }

  if(args->argc == 6 && args->isStr(0, "start") == true)
  {
    uint8_t ch = (uint8_t)args->getData(1);
    tim_gpio_output_t output_type;
    uint32_t period_us = (uint32_t)args->getData(3);
    uint32_t active_us = (uint32_t)args->getData(4);
    uint32_t count = (uint32_t)args->getData(5);
    bool result = false;

    if(args->isStr(2, "pp") == true)
    {
      output_type = TIM_GPIO_OUTPUT_PP;
      result = timGpioStart(ch, output_type, period_us, active_us, count);
    }
    else if(args->isStr(2, "od") == true)
    {
      output_type = TIM_GPIO_OUTPUT_OD;
      result = timGpioStart(ch, output_type, period_us, active_us, count);
    }

    cliPrintf("tim_gpio start %d : %s\n", ch, result ? "ok" : "fail");

    ret = true;
  }

  if(args->argc == 2 && args->isStr(0, "stop") == true)
  {
    uint8_t ch = (uint8_t)args->getData(1);
    bool result;

    result = timGpioStop(ch);
    cliPrintf("tim_gpio stop %d : %s\n", ch, result ? "ok" : "fail");

    ret = true;
  }

  if(ret != true)
  {
    cliPrintf("tim_gpio info\n");
    cliPrintf("tim_gpio open ch[0~%d] tim_ch gpio_ch\n", TIM_GPIO_MAX_CH - 1);
    cliPrintf("tim_gpio tick ch[0~%d] tick_hz\n", TIM_GPIO_MAX_CH - 1);
    cliPrintf("tim_gpio start ch[0~%d] pp:od period_us active_us count\n", TIM_GPIO_MAX_CH - 1);
    cliPrintf("tim_gpio stop ch[0~%d]\n", TIM_GPIO_MAX_CH - 1);
  }
}
#endif

#endif
