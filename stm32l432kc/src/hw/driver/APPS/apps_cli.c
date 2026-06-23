/*
 * apps_cli.c
 */

#include "APPS/apps_cli.h"
#include "hw_def.h"


#ifdef _USE_HW_CLI

#include "bsp.h"
#include "cli.h"


#define APPS_CLI_UPDATE_PERIOD_MS       10U
#define APPS_CLI_PRINT_PERIOD_MS        100U


static bool apps_cli_initialized = false;


static uint16_t appsCliGetU16(cli_args_t *args, uint8_t index)
{
  int32_t value = args->getData(index);

  if (value < 0)
  {
    return 0U;
  }
  if (value > UINT16_MAX)
  {
    return UINT16_MAX;
  }

  return (uint16_t)value;
}

static void appsCliPrintPercent(const char *p_name, uint16_t per_mille)
{
  cliPrintf("%s : %u.%u%%\n",
            p_name,
            per_mille / 10U,
            per_mille % 10U);
}

static void appsCliPrintStatus(uint32_t status)
{
  cliPrintf("status : 0x%08lX\n", status);

  if (status == APPS_STATUS_OK)
  {
    cliPrintf("  OK\n");
  }
  if ((status & APPS_STATUS_NOT_CONFIGURED) != 0U)
  {
    cliPrintf("  NOT_CONFIGURED\n");
  }
  if ((status & APPS_STATUS_ADC_READ_FAULT) != 0U)
  {
    cliPrintf("  ADC_READ_FAULT\n");
  }
  if ((status & APPS_STATUS_SIGNAL1_RANGE_FAULT) != 0U)
  {
    cliPrintf("  SIGNAL1_RANGE_FAULT\n");
  }
  if ((status & APPS_STATUS_SIGNAL2_RANGE_FAULT) != 0U)
  {
    cliPrintf("  SIGNAL2_RANGE_FAULT\n");
  }
  if ((status & APPS_STATUS_DIFF_FAULT) != 0U)
  {
    cliPrintf("  DIFF_FAULT\n");
  }
  if ((status & APPS_STATUS_FAULT_LATCHED) != 0U)
  {
    cliPrintf("  FAULT_LATCHED\n");
  }
  if ((status & APPS_STATUS_CAN_TX_FAULT) != 0U)
  {
    cliPrintf("  CAN_TX_FAULT\n");
  }
}

static void appsCliPrintConfig(void)
{
  cliPrintf("initialized : %s\n",
            appsIsInitialized() ? "true" : "false");
  cliPrintf("configured  : %s\n",
            appsIsConfigured() ? "true" : "false");
  cliPrintf("signal1 adc/raw : ch%u, %u..%u\n",
            appsGetAdcChannel(APPS_SIGNAL1),
            appsGetRawMin(APPS_SIGNAL1),
            appsGetRawMax(APPS_SIGNAL1));
  cliPrintf("signal2 adc/raw : ch%u, %u..%u\n",
            appsGetAdcChannel(APPS_SIGNAL2),
            appsGetRawMin(APPS_SIGNAL2),
            appsGetRawMax(APPS_SIGNAL2));
  cliPrintf("can channel : %u\n", appsGetCanChannel());
  cliPrintf("max difference : %u.%u%%\n",
            appsGetMaxDifferencePerMille() / 10U,
            appsGetMaxDifferencePerMille() % 10U);
  cliPrintf("fault confirm : %lums\n", appsGetFaultConfirmMs());
}

static void appsCliPrintSignal(uint8_t signal)
{
  uint16_t voltage_mv = appsGetVoltageMv(signal);

  cliPrintf("signal%u : raw=%u, %u.%03uV\n",
            signal + 1U,
            appsGetRaw(signal),
            voltage_mv / 1000U,
            voltage_mv % 1000U);
}

static void appsCliPrintResult(void)
{
  appsCliPrintSignal(APPS_SIGNAL1);
  appsCliPrintSignal(APPS_SIGNAL2);
  appsCliPrintPercent("signal1",
                      appsGetPercentPerMille(APPS_SIGNAL1));
  appsCliPrintPercent("signal2",
                      appsGetPercentPerMille(APPS_SIGNAL2));
  appsCliPrintPercent("difference",
                      appsGetDifferencePerMille());
  appsCliPrintPercent("pedal", appsGetPedalPerMille());
  cliPrintf("valid : %s\n", appsIsValid() ? "true" : "false");
  cliPrintf("fault timer : %lums / %lums\n",
            appsGetFaultElapsedMs(),
            appsGetFaultConfirmMs());
  appsCliPrintStatus(appsGetStatus());
}

static void cliApps(cli_args_t *args)
{
  bool handled = false;

  if ((args->argc == 1) && args->isStr(0, "info"))
  {
    (void)appsUpdate();
    appsCliPrintResult();
    handled = true;
  }

  if ((args->argc == 1) && args->isStr(0, "show"))
  {
    uint32_t print_elapsed_ms = APPS_CLI_PRINT_PERIOD_MS;

    while (cliKeepLoop())
    {
      (void)appsUpdate();

      if (print_elapsed_ms >= APPS_CLI_PRINT_PERIOD_MS)
      {
        appsCliPrintResult();
        cliPrintf("\n");
        print_elapsed_ms = 0U;
      }

      delay(APPS_CLI_UPDATE_PERIOD_MS);
      print_elapsed_ms += APPS_CLI_UPDATE_PERIOD_MS;
    }

    handled = true;
  }

  if ((args->argc == 3) && args->isStr(0, "test"))
  {
    (void)appsUpdateRaw(appsCliGetU16(args, 1),
                        appsCliGetU16(args, 2));
    appsCliPrintResult();
    handled = true;
  }

  if ((args->argc == 1) && args->isStr(0, "config"))
  {
    appsCliPrintConfig();
    handled = true;
  }

  if ((args->argc == 5) && args->isStr(0, "config"))
  {
    (void)appsSetConfig(appsCliGetU16(args, 1),
                        appsCliGetU16(args, 2),
                        appsCliGetU16(args, 3),
                        appsCliGetU16(args, 4));
    appsCliPrintConfig();
    handled = true;
  }

  if ((args->argc == 1) && args->isStr(0, "clear"))
  {
    appsClearFault();
    cliPrintf("apps fault clear\n");
    handled = true;
  }

  if (handled != true)
  {
    cliPrintf("apps info\n");
    cliPrintf("apps show\n");
    cliPrintf("apps test raw_signal1 raw_signal2\n");
    cliPrintf("apps config\n");
    cliPrintf("apps config signal1_min signal1_max signal2_min signal2_max\n");
    cliPrintf("apps clear\n");
  }
}

bool appsCliInit(void)
{
  if (apps_cli_initialized == true)
  {
    return true;
  }

  apps_cli_initialized = cliAdd("apps", cliApps);

  return apps_cli_initialized;
}

#else

bool appsCliInit(void)
{
  return true;
}

#endif
