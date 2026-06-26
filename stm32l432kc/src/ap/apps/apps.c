/*
 * apps.c
 */

#include "apps.h"

#ifdef _USE_HW_CLI
#include "cli.h"
#endif


#define APPS_TIMED_FAULT_MASK  (APPS_STATUS_ADC_READ_FAULT      | \
                                APPS_STATUS_SIGNAL1_RANGE_FAULT | \
                                APPS_STATUS_SIGNAL2_RANGE_FAULT | \
                                APPS_STATUS_DIFF_FAULT)

#ifdef _USE_HW_CLI
#define APPS_CLI_UPDATE_PERIOD_MS       10U
#define APPS_CLI_PRINT_PERIOD_MS        100U
#endif


typedef struct
{
  uint16_t raw_min;
  uint16_t raw_max;
} apps_calibration_t;

typedef struct
{
  uint16_t raw[APPS_SIGNAL_COUNT];
  uint16_t voltage_mv[APPS_SIGNAL_COUNT];
  uint16_t percent_per_mille[APPS_SIGNAL_COUNT];
  uint16_t difference_per_mille;
  uint16_t pedal_per_mille;
  int16_t  command_sent;
  uint32_t status;
  bool     adc_ok;
  bool     valid;
  bool     can_tx_ok;
} apps_result_t;

typedef struct
{
  uint32_t started_ms;
  uint32_t elapsed_ms;
  uint32_t pending_status;
  uint32_t latched_status;
  bool     timer_active;
  bool     latched;
} apps_fault_t;


static apps_calibration_t apps_cal[APPS_SIGNAL_COUNT];
static apps_result_t apps_result;
static apps_fault_t apps_fault;
static bool apps_initialized = false;
static bool apps_calibrated = false;

#ifdef _USE_HW_CLI
static bool apps_command_registered = false;

static bool appsRegisterCli(void);
static void cliApps(cli_args_t *args);
#endif


static bool appsSignalIsValid(uint8_t signal)
{
  return signal < APPS_SIGNAL_COUNT;
}

static uint16_t appsAbsDiffU16(uint16_t a, uint16_t b)
{
  return (a > b) ? (uint16_t)(a - b) : (uint16_t)(b - a);
}

static uint16_t appsClampPerMille(int32_t value)
{
  if (value < 0)
  {
    return 0U;
  }
  if (value > (int32_t)APPS_PER_MILLE_MAX)
  {
    return APPS_PER_MILLE_MAX;
  }

  return (uint16_t)value;
}

static bool appsCalibrationIsValid(void)
{
  for (uint8_t i = 0; i < APPS_SIGNAL_COUNT; i++)
  {
    if (apps_cal[i].raw_min == apps_cal[i].raw_max)
    {
      return false;
    }
  }

  return true;
}

static void appsLoadDefaultCalibration(void)
{
  apps_cal[APPS_SIGNAL1].raw_min = APPS_SIGNAL1_RAW_MIN_DEFAULT;
  apps_cal[APPS_SIGNAL1].raw_max = APPS_SIGNAL1_RAW_MAX_DEFAULT;
  apps_cal[APPS_SIGNAL2].raw_min = APPS_SIGNAL2_RAW_MIN_DEFAULT;
  apps_cal[APPS_SIGNAL2].raw_max = APPS_SIGNAL2_RAW_MAX_DEFAULT;

  apps_calibrated = appsCalibrationIsValid();
}

static void appsResetFaultState(void)
{
  memset(&apps_fault, 0, sizeof(apps_fault));
}

static uint16_t appsScaleRaw(uint8_t signal, uint16_t raw, bool *p_range_ok)
{
  const apps_calibration_t *p_cal = &apps_cal[signal];
  int32_t raw_min = p_cal->raw_min;
  int32_t raw_max = p_cal->raw_max;
  int32_t raw_low = (raw_min < raw_max) ? raw_min : raw_max;
  int32_t raw_high = (raw_min > raw_max) ? raw_min : raw_max;
  int32_t span = raw_max - raw_min;
  int32_t scaled;

  *p_range_ok = true;

  if ((int32_t)raw < (raw_low - (int32_t)APPS_RAW_LOW_MARGIN))
  {
    *p_range_ok = false;
  }
  if ((int32_t)raw > (raw_high + (int32_t)APPS_RAW_HIGH_MARGIN))
  {
    *p_range_ok = false;
  }

  scaled = (((int32_t)raw - raw_min) * (int32_t)APPS_PER_MILLE_MAX) / span;

  return appsClampPerMille(scaled);
}

static void appsUpdateFaultTimer(uint32_t instant_status)
{
  uint32_t timed_fault = instant_status & APPS_TIMED_FAULT_MASK;

  if (timed_fault == APPS_STATUS_OK)
  {
    apps_fault.timer_active = false;
    apps_fault.started_ms = 0U;
    apps_fault.elapsed_ms = 0U;
    apps_fault.pending_status = APPS_STATUS_OK;
  }
  else
  {
    uint32_t now_ms = millis();

    if ((apps_fault.timer_active != true) ||
        (apps_fault.pending_status != timed_fault))
    {
      apps_fault.timer_active = true;
      apps_fault.started_ms = now_ms;
      apps_fault.elapsed_ms = 0U;
      apps_fault.pending_status = timed_fault;
    }
    else
    {
      apps_fault.elapsed_ms = now_ms - apps_fault.started_ms;

      if (apps_fault.elapsed_ms >= APPS_FAULT_CONFIRM_MS)
      {
        apps_fault.latched = true;
        apps_fault.latched_status |= timed_fault;
      }
    }
  }

  apps_result.status = instant_status | apps_fault.latched_status;
  if (apps_fault.latched == true)
  {
    apps_result.status |= APPS_STATUS_FAULT_LATCHED;
  }
}

static void appsSetRawResult(uint16_t raw_signal1, uint16_t raw_signal2)
{
  apps_result.raw[APPS_SIGNAL1] = raw_signal1;
  apps_result.raw[APPS_SIGNAL2] = raw_signal2;
  apps_result.voltage_mv[APPS_SIGNAL1] =
      (uint16_t)adcConvMillivolts(APPS_SIGNAL1_ADC_CH, raw_signal1);
  apps_result.voltage_mv[APPS_SIGNAL2] =
      (uint16_t)adcConvMillivolts(APPS_SIGNAL2_ADC_CH, raw_signal2);
}

static void appsPrepareEvaluation(void)
{
  apps_result.percent_per_mille[APPS_SIGNAL1] = 0U;
  apps_result.percent_per_mille[APPS_SIGNAL2] = 0U;
  apps_result.difference_per_mille = 0U;
  apps_result.pedal_per_mille = 0U;
  apps_result.command_sent = 0;
  apps_result.adc_ok = true;
  apps_result.valid = false;
  apps_result.can_tx_ok = false;
}

static bool appsEvaluateRaw(uint16_t raw_signal1, uint16_t raw_signal2)
{
  bool range_ok[APPS_SIGNAL_COUNT] = {false, false};
  uint32_t instant_status = APPS_STATUS_OK;

  appsSetRawResult(raw_signal1, raw_signal2);
  appsPrepareEvaluation();

  if ((apps_initialized != true) || (apps_calibrated != true))
  {
    appsUpdateFaultTimer(APPS_STATUS_NOT_CONFIGURED);
    return false;
  }

  apps_result.percent_per_mille[APPS_SIGNAL1] =
      appsScaleRaw(APPS_SIGNAL1, raw_signal1, &range_ok[APPS_SIGNAL1]);
  apps_result.percent_per_mille[APPS_SIGNAL2] =
      appsScaleRaw(APPS_SIGNAL2, raw_signal2, &range_ok[APPS_SIGNAL2]);

  if (range_ok[APPS_SIGNAL1] != true)
  {
    instant_status |= APPS_STATUS_SIGNAL1_RANGE_FAULT;
  }
  if (range_ok[APPS_SIGNAL2] != true)
  {
    instant_status |= APPS_STATUS_SIGNAL2_RANGE_FAULT;
  }

  apps_result.difference_per_mille =
      appsAbsDiffU16(apps_result.percent_per_mille[APPS_SIGNAL1],
                     apps_result.percent_per_mille[APPS_SIGNAL2]);

  if (apps_result.difference_per_mille >= APPS_MAX_DIFF_PER_MILLE)
  {
    instant_status |= APPS_STATUS_DIFF_FAULT;
  }

  appsUpdateFaultTimer(instant_status);

  if ((instant_status == APPS_STATUS_OK) &&
      (apps_fault.latched != true))
  {
    apps_result.pedal_per_mille =
        (uint16_t)(((uint32_t)apps_result.percent_per_mille[APPS_SIGNAL1] +
                    (uint32_t)apps_result.percent_per_mille[APPS_SIGNAL2]) /
                   APPS_SIGNAL_COUNT);
    apps_result.valid = true;
  }

  return true;
}


bool appsInit(void)
{
  memset(&apps_result, 0, sizeof(apps_result));
  appsResetFaultState();
  appsLoadDefaultCalibration();

  apps_initialized = true;
  apps_result.status =
      (apps_calibrated == true) ? APPS_STATUS_OK : APPS_STATUS_NOT_CONFIGURED;

#ifdef _USE_HW_CLI
  (void)appsRegisterCli();
#endif

  return true;
}

bool appsSetConfig(uint16_t signal1_raw_min,
                   uint16_t signal1_raw_max,
                   uint16_t signal2_raw_min,
                   uint16_t signal2_raw_max)
{
  apps_cal[APPS_SIGNAL1].raw_min = signal1_raw_min;
  apps_cal[APPS_SIGNAL1].raw_max = signal1_raw_max;
  apps_cal[APPS_SIGNAL2].raw_min = signal2_raw_min;
  apps_cal[APPS_SIGNAL2].raw_max = signal2_raw_max;

  apps_calibrated = appsCalibrationIsValid();
  appsClearFault();

  return apps_calibrated;
}

void appsClearFault(void)
{
  appsResetFaultState();
  apps_result.valid = false;
  apps_result.command_sent = 0;
  apps_result.can_tx_ok = false;
  apps_result.status =
      (apps_calibrated == true) ? APPS_STATUS_OK : APPS_STATUS_NOT_CONFIGURED;
}

bool appsUpdate(void)
{
  int32_t raw_signal1;
  int32_t raw_signal2;

  if (apps_initialized != true)
  {
    return false;
  }

  if (adcUpdate() != true)
  {
    uint32_t status = APPS_STATUS_ADC_READ_FAULT;

    if (apps_calibrated != true)
    {
      status |= APPS_STATUS_NOT_CONFIGURED;
    }

    apps_result.adc_ok = false;
    apps_result.valid = false;
    apps_result.command_sent = 0;
    apps_result.can_tx_ok = false;
    appsUpdateFaultTimer(status);

    return false;
  }

  raw_signal1 = adcRead(APPS_SIGNAL1_ADC_CH);
  raw_signal2 = adcRead(APPS_SIGNAL2_ADC_CH);

  if ((raw_signal1 < 0) || (raw_signal1 > UINT16_MAX) ||
      (raw_signal2 < 0) || (raw_signal2 > UINT16_MAX))
  {
    apps_result.adc_ok = false;
    apps_result.valid = false;
    appsUpdateFaultTimer(APPS_STATUS_ADC_READ_FAULT);
    return false;
  }

  return appsEvaluateRaw((uint16_t)raw_signal1, (uint16_t)raw_signal2);
}

bool appsUpdateRaw(uint16_t raw_signal1, uint16_t raw_signal2)
{
  if (apps_initialized != true)
  {
    return false;
  }

  return appsEvaluateRaw(raw_signal1, raw_signal2);
}

bool appsSendCommand(int16_t prepared_command)
{
  int16_t command_to_send;

  if (apps_initialized != true)
  {
    return false;
  }

  command_to_send = (apps_result.valid == true) ? prepared_command : 0;
  apps_result.command_sent = command_to_send;
  apps_result.can_tx_ok = bamocarCanSendTorqueCmd(APPS_CAN_CH,
                                                  command_to_send);

  if (apps_result.can_tx_ok != true)
  {
    apps_result.status |= APPS_STATUS_CAN_TX_FAULT;
    return false;
  }

  apps_result.status &= ~APPS_STATUS_CAN_TX_FAULT;
  return true;
}

bool appsRun(int16_t prepared_command)
{
  bool adc_result = appsUpdate();
  bool can_result = appsSendCommand(prepared_command);

  return adc_result && can_result;
}

bool appsIsInitialized(void)
{
  return apps_initialized;
}

bool appsIsConfigured(void)
{
  return apps_calibrated;
}

bool appsIsValid(void)
{
  return apps_result.valid;
}

bool appsIsAdcOk(void)
{
  return apps_result.adc_ok;
}

bool appsIsCanTxOk(void)
{
  return apps_result.can_tx_ok;
}

bool appsIsFaultLatched(void)
{
  return apps_fault.latched;
}

uint8_t appsGetAdcChannel(uint8_t signal)
{
  if (signal == APPS_SIGNAL1)
  {
    return APPS_SIGNAL1_ADC_CH;
  }
  if (signal == APPS_SIGNAL2)
  {
    return APPS_SIGNAL2_ADC_CH;
  }

  return 0U;
}

uint8_t appsGetCanChannel(void)
{
  return APPS_CAN_CH;
}

uint16_t appsGetRawMin(uint8_t signal)
{
  return appsSignalIsValid(signal) ? apps_cal[signal].raw_min : 0U;
}

uint16_t appsGetRawMax(uint8_t signal)
{
  return appsSignalIsValid(signal) ? apps_cal[signal].raw_max : 0U;
}

uint16_t appsGetRaw(uint8_t signal)
{
  return appsSignalIsValid(signal) ? apps_result.raw[signal] : 0U;
}

uint16_t appsGetVoltageMv(uint8_t signal)
{
  return appsSignalIsValid(signal) ? apps_result.voltage_mv[signal] : 0U;
}

uint16_t appsGetPercentPerMille(uint8_t signal)
{
  return appsSignalIsValid(signal)
             ? apps_result.percent_per_mille[signal]
             : 0U;
}

uint16_t appsGetDifferencePerMille(void)
{
  return apps_result.difference_per_mille;
}

uint16_t appsGetPedalPerMille(void)
{
  return apps_result.pedal_per_mille;
}

uint16_t appsGetMaxDifferencePerMille(void)
{
  return APPS_MAX_DIFF_PER_MILLE;
}

uint32_t appsGetFaultConfirmMs(void)
{
  return APPS_FAULT_CONFIRM_MS;
}

uint32_t appsGetFaultElapsedMs(void)
{
  return apps_fault.elapsed_ms;
}

uint32_t appsGetStatus(void)
{
  return apps_result.status;
}

int16_t appsGetCommandSent(void)
{
  return apps_result.command_sent;
}


#ifdef _USE_HW_CLI

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
  cliPrintf("raw margin : -%u/+%u\n",
            APPS_RAW_LOW_MARGIN,
            APPS_RAW_HIGH_MARGIN);
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

static bool appsRegisterCli(void)
{
  if (apps_command_registered == true)
  {
    return true;
  }

  apps_command_registered = cliAdd("apps", cliApps);

  return apps_command_registered;
}

#endif
