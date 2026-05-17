/*
 * dm542.c
 *
 *  Created on: May 13, 2026
 *      Author: young
 */

#include "DM542/dm542.h"
#include "gpio.h"
#include "cli.h"

#ifdef _USE_DM542

#define _PIN_GPIO_DM542_PUL             0
#define _PIN_GPIO_DM542_DIR             1
#define _PIN_GPIO_DM542_ENA             2

#define DM542_STEP_PULSE_WIDTH_US       5
#define DM542_DIR_SETUP_DELAY_US        5
#define DM542_CLI_DEFAULT_DELAY_US      1000

dm542_t dm542_driver;

static uint32_t dm542AbsStep(int32_t step);
static int32_t  dm542MmToStep(dm542_t *p_driver, float mm);

#ifdef _USE_HW_CLI
static void     cliDm542(cli_args_t *args);
static bool     is_cli_init = false;
#endif

bool dm542Init(void)
{
  gpioPinWrite(_PIN_GPIO_DM542_PUL, false);
  dm542SetDir(&dm542_driver, false);
  dm542Disable(&dm542_driver);

#ifdef _USE_HW_CLI
  if (is_cli_init == false)
  {
    cliAdd("dm542", cliDm542);
    is_cli_init = true;
  }
#endif

  return true;
}

void dm542Enable(dm542_t *p_driver)
{
  if (p_driver == NULL)
  {
    return;
  }

  if (p_driver->ena_inv == true)
  {
    gpioPinWrite(_PIN_GPIO_DM542_ENA, false);
  }
  else
  {
    gpioPinWrite(_PIN_GPIO_DM542_ENA, true);
  }
}

void dm542Disable(dm542_t *p_driver)
{
  if (p_driver == NULL)
  {
    return;
  }

  if (p_driver->ena_inv == true)
  {
    gpioPinWrite(_PIN_GPIO_DM542_ENA, true);
  }
  else
  {
    gpioPinWrite(_PIN_GPIO_DM542_ENA, false);
  }
}

void dm542SetDir(dm542_t *p_driver, bool dir)
{
  if (p_driver == NULL)
  {
    return;
  }

  if (p_driver->dir_inv == true)
  {
    gpioPinWrite(_PIN_GPIO_DM542_DIR, !dir);
  }
  else
  {
    gpioPinWrite(_PIN_GPIO_DM542_DIR, dir);
  }
}

void dm542Step(dm542_t *p_driver)
{
  if (p_driver == NULL)
  {
    return;
  }

  gpioPinWrite(_PIN_GPIO_DM542_PUL, true);
  delayUs(DM542_STEP_PULSE_WIDTH_US);
  gpioPinWrite(_PIN_GPIO_DM542_PUL, false);
  delayUs(DM542_STEP_PULSE_WIDTH_US);
}

void dm542MoveStep(dm542_t *p_driver, int32_t step, uint32_t pulse_delay_us)
{
  uint32_t step_count;

  if (p_driver == NULL || step == 0)
  {
    return;
  }

  dm542SetDir(p_driver, step > 0);
  delayUs(DM542_DIR_SETUP_DELAY_US);
  step_count = dm542AbsStep(step);

  for (uint32_t i = 0; i < step_count; i++)
  {
    dm542Step(p_driver);
    delayUs(pulse_delay_us);
  }
}

void dm542MoveMm(dm542_t *p_driver, float mm, uint32_t pulse_delay_us)
{
  if (p_driver == NULL)
  {
    return;
  }

  dm542MoveStep(p_driver, dm542MmToStep(p_driver, mm), pulse_delay_us);
}

static uint32_t dm542AbsStep(int32_t step)
{
  if (step < 0)
  {
    return (uint32_t)(-(step + 1)) + 1U;
  }

  return (uint32_t)step;
}

static int32_t dm542MmToStep(dm542_t *p_driver, float mm)
{
  float step_f;

  if (p_driver == NULL || p_driver->lead_mm <= 0.0f)
  {
    return 0;
  }

  step_f = mm * (float)p_driver->pulse_per_rev / p_driver->lead_mm;

  if (step_f >= 0.0f)
  {
    return (int32_t)(step_f + 0.5f);
  }

  return (int32_t)(step_f - 0.5f);
}

#ifdef _USE_HW_CLI
static void cliDm542(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 1 && args->isStr(0, "info") == true)
  {
    cliPrintf("pulse_per_rev : %d\n", dm542_driver.pulse_per_rev);
    cliPrintf("lead_mm       : %d.%03d\n",
              (int32_t)dm542_driver.lead_mm,
              (int32_t)((dm542_driver.lead_mm - (float)((int32_t)dm542_driver.lead_mm)) * 1000.0f));
    cliPrintf("dir_inv       : %d\n", dm542_driver.dir_inv);
    cliPrintf("ena_inv       : %d\n", dm542_driver.ena_inv);
    cliPrintf("enable policy : manual\n");
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "enable") == true)
  {
    dm542Enable(&dm542_driver);
    cliPrintf("dm542 enable\n");
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "disable") == true)
  {
    dm542Disable(&dm542_driver);
    cliPrintf("dm542 disable\n");
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "dir") == true)
  {
    bool dir;

    dir = args->getData(1) ? true : false;
    dm542SetDir(&dm542_driver, dir);
    cliPrintf("dm542 dir : %d\n", dir);
    ret = true;
  }

  if (args->argc >= 1 && args->argc <= 3 && args->isStr(0, "step") == true)
  {
    int32_t step = 1;
    uint32_t pulse_delay_us = DM542_CLI_DEFAULT_DELAY_US;

    if (args->argc >= 2)
    {
      step = args->getData(1);
    }

    if (args->argc >= 3)
    {
      pulse_delay_us = args->getData(2);
    }

    if (step == 1 && args->argc == 1)
    {
      dm542Step(&dm542_driver);
    }
    else
    {
      dm542MoveStep(&dm542_driver, step, pulse_delay_us);
    }

    cliPrintf("dm542 step : %d\n", step);
    ret = true;
  }

  if (args->argc >= 2 && args->argc <= 3 && args->isStr(0, "move_step") == true)
  {
    int32_t step;
    uint32_t pulse_delay_us = DM542_CLI_DEFAULT_DELAY_US;

    step = args->getData(1);
    if (args->argc >= 3)
    {
      pulse_delay_us = args->getData(2);
    }

    dm542MoveStep(&dm542_driver, step, pulse_delay_us);
    cliPrintf("dm542 move_step : %d\n", step);
    ret = true;
  }

  if (args->argc >= 2 && args->argc <= 3 && args->isStr(0, "move_mm") == true)
  {
    float mm;
    uint32_t pulse_delay_us = DM542_CLI_DEFAULT_DELAY_US;

    mm = args->getFloat(1);
    if (args->argc >= 3)
    {
      pulse_delay_us = args->getData(2);
    }

    dm542MoveMm(&dm542_driver, mm, pulse_delay_us);
    cliPrintf("dm542 move_mm\n");
    ret = true;
  }

  if (args->argc == 5 && args->isStr(0, "set") == true)
  {
    dm542_driver.pulse_per_rev = args->getData(1);
    dm542_driver.lead_mm       = args->getFloat(2);
    dm542_driver.dir_inv       = args->getData(3) ? true : false;
    dm542_driver.ena_inv       = args->getData(4) ? true : false;

    cliPrintf("dm542 set\n");
    ret = true;
  }

  if (ret != true)
  {
    cliPrintf("dm542 info\n");
    cliPrintf("dm542 enable\n");
    cliPrintf("dm542 disable\n");
    cliPrintf("dm542 dir 0:1\n");
    cliPrintf("dm542 step [count] [delay_us]\n");
    cliPrintf("dm542 move_step step [delay_us]\n");
    cliPrintf("dm542 move_mm mm [delay_us]\n");
    cliPrintf("dm542 set pulse_per_rev lead_mm dir_inv ena_inv\n");
  }
}
#endif

#endif
