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

static void appsCliCommand(cli_args_t *args);
#endif


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

static void appsClearFaultTimer(void)
{
  memset(&apps_fault, 0, sizeof(apps_fault));
}

static void appsClearMeasurementResult(void)
{
  memset(apps_result.raw, 0, sizeof(apps_result.raw));
  memset(apps_result.voltage_mv, 0, sizeof(apps_result.voltage_mv));
  memset(apps_result.percent_per_mille, 0, sizeof(apps_result.percent_per_mille));

  apps_result.difference_per_mille = 0U;
  apps_result.pedal_per_mille = 0U;
  apps_result.command_sent = 0;
  apps_result.adc_ok = false;
  apps_result.valid = false;
  apps_result.can_tx_ok = false;
}

static uint16_t appsScaleRawToPerMille(uint8_t signal, uint16_t raw, bool *p_range_ok)
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

  if (scaled < 0)
  {
    return 0U;
  }
  if (scaled > (int32_t)APPS_PER_MILLE_MAX)
  {
    return APPS_PER_MILLE_MAX;
  }

  return (uint16_t)scaled;
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

static bool appsEvaluateRawPair(uint16_t raw_signal1, uint16_t raw_signal2)
{
  bool range_ok[APPS_SIGNAL_COUNT] = {false, false};
  uint32_t instant_status = APPS_STATUS_OK;

  apps_result.raw[APPS_SIGNAL1] = raw_signal1;
  apps_result.raw[APPS_SIGNAL2] = raw_signal2;
  apps_result.voltage_mv[APPS_SIGNAL1] =
      (uint16_t)adcConvMillivolts(APPS_SIGNAL1_ADC_CH, raw_signal1);
  apps_result.voltage_mv[APPS_SIGNAL2] =
      (uint16_t)adcConvMillivolts(APPS_SIGNAL2_ADC_CH, raw_signal2);
  apps_result.percent_per_mille[APPS_SIGNAL1] = 0U;
  apps_result.percent_per_mille[APPS_SIGNAL2] = 0U;
  apps_result.difference_per_mille = 0U;
  apps_result.pedal_per_mille = 0U;
  apps_result.command_sent = 0;
  apps_result.adc_ok = true;
  apps_result.valid = false;
  apps_result.can_tx_ok = false;

  if ((apps_initialized != true) || (apps_calibrated != true))
  {
    appsUpdateFaultTimer(APPS_STATUS_NOT_CONFIGURED);
    return false;
  }

  apps_result.percent_per_mille[APPS_SIGNAL1] =
      appsScaleRawToPerMille(APPS_SIGNAL1, raw_signal1, &range_ok[APPS_SIGNAL1]);
  apps_result.percent_per_mille[APPS_SIGNAL2] =
      appsScaleRawToPerMille(APPS_SIGNAL2, raw_signal2, &range_ok[APPS_SIGNAL2]);

  if (range_ok[APPS_SIGNAL1] != true)
  {
    instant_status |= APPS_STATUS_SIGNAL1_RANGE_FAULT;
  }
  if (range_ok[APPS_SIGNAL2] != true)
  {
    instant_status |= APPS_STATUS_SIGNAL2_RANGE_FAULT;
  }

  if (apps_result.percent_per_mille[APPS_SIGNAL1] >
      apps_result.percent_per_mille[APPS_SIGNAL2])
  {
    apps_result.difference_per_mille =
        apps_result.percent_per_mille[APPS_SIGNAL1] -
        apps_result.percent_per_mille[APPS_SIGNAL2];
  }
  else
  {
    apps_result.difference_per_mille =
        apps_result.percent_per_mille[APPS_SIGNAL2] -
        apps_result.percent_per_mille[APPS_SIGNAL1];
  }

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
  appsClearFaultTimer();

  apps_cal[APPS_SIGNAL1].raw_min = APPS_SIGNAL1_RAW_MIN_DEFAULT;
  apps_cal[APPS_SIGNAL1].raw_max = APPS_SIGNAL1_RAW_MAX_DEFAULT;
  apps_cal[APPS_SIGNAL2].raw_min = APPS_SIGNAL2_RAW_MIN_DEFAULT;
  apps_cal[APPS_SIGNAL2].raw_max = APPS_SIGNAL2_RAW_MAX_DEFAULT;
  apps_calibrated = appsCalibrationIsValid();

  apps_initialized = true;
  apps_result.status =
      (apps_calibrated == true) ? APPS_STATUS_OK : APPS_STATUS_NOT_CONFIGURED;

#ifdef _USE_HW_CLI
  if (apps_command_registered != true)
  {
    apps_command_registered = cliAdd("apps", appsCliCommand);
  }
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
  appsClearFaultTimer();
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

  if (apps_initialized != true)     return false;

  if (adcUpdate() != true)
  {
    uint32_t status = APPS_STATUS_ADC_READ_FAULT;

    if (apps_calibrated != true)
    {
      status |= APPS_STATUS_NOT_CONFIGURED;
    }

    appsClearMeasurementResult();
    appsUpdateFaultTimer(status);

    return false;
  }

  raw_signal1 = adcRead(APPS_SIGNAL1_ADC_CH);
  raw_signal2 = adcRead(APPS_SIGNAL2_ADC_CH);

  if ((raw_signal1 < 0) || (raw_signal1 > UINT16_MAX) ||
      (raw_signal2 < 0) || (raw_signal2 > UINT16_MAX))
  {
    appsClearMeasurementResult();
    appsUpdateFaultTimer(APPS_STATUS_ADC_READ_FAULT);
    return false;
  }

  return appsEvaluateRawPair((uint16_t)raw_signal1, (uint16_t)raw_signal2);
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

#ifdef _USE_HW_CLI

static void appsCliCommand(cli_args_t *args)
{
  if ((args->argc == 1) && args->isStr(0, "info"))
  {
    uint32_t status;
    uint16_t signal1_voltage_mv;
    uint16_t signal2_voltage_mv;

    (void)appsUpdate();

    signal1_voltage_mv = apps_result.voltage_mv[APPS_SIGNAL1];
    signal2_voltage_mv = apps_result.voltage_mv[APPS_SIGNAL2];
    status = apps_result.status;

    cliPrintf("signal1 : raw=%u, %u.%03uV, %u.%u%%\n",
              apps_result.raw[APPS_SIGNAL1],
              signal1_voltage_mv / 1000U,
              signal1_voltage_mv % 1000U,
              apps_result.percent_per_mille[APPS_SIGNAL1] / 10U,
              apps_result.percent_per_mille[APPS_SIGNAL1] % 10U);
    cliPrintf("signal2 : raw=%u, %u.%03uV, %u.%u%%\n",
              apps_result.raw[APPS_SIGNAL2],
              signal2_voltage_mv / 1000U,
              signal2_voltage_mv % 1000U,
              apps_result.percent_per_mille[APPS_SIGNAL2] / 10U,
              apps_result.percent_per_mille[APPS_SIGNAL2] % 10U);
    cliPrintf("difference : %u.%u%%\n",
              apps_result.difference_per_mille / 10U,
              apps_result.difference_per_mille % 10U);
    cliPrintf("pedal : %u.%u%%\n",
              apps_result.pedal_per_mille / 10U,
              apps_result.pedal_per_mille % 10U);
    cliPrintf("valid : %s\n", apps_result.valid ? "true" : "false");
    cliPrintf("fault timer : %lums / %lums\n",
              (unsigned long)apps_fault.elapsed_ms,
              (unsigned long)APPS_FAULT_CONFIRM_MS);
    cliPrintf("status : 0x%08lX\n", (unsigned long)status);

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

    return;
  }

  if ((args->argc == 1) && args->isStr(0, "config"))
  {
    cliPrintf("initialized : %s\n",
              apps_initialized ? "true" : "false");
    cliPrintf("configured  : %s\n",
              apps_calibrated ? "true" : "false");
    cliPrintf("signal1 adc/raw : ch%u, %u..%u\n",
              APPS_SIGNAL1_ADC_CH,
              apps_cal[APPS_SIGNAL1].raw_min,
              apps_cal[APPS_SIGNAL1].raw_max);
    cliPrintf("signal2 adc/raw : ch%u, %u..%u\n",
              APPS_SIGNAL2_ADC_CH,
              apps_cal[APPS_SIGNAL2].raw_min,
              apps_cal[APPS_SIGNAL2].raw_max);
    cliPrintf("raw margin : -%u/+%u\n",
              APPS_RAW_LOW_MARGIN,
              APPS_RAW_HIGH_MARGIN);
    cliPrintf("can channel : %u\n", APPS_CAN_CH);
    cliPrintf("max difference : %u.%u%%\n",
              APPS_MAX_DIFF_PER_MILLE / 10U,
              APPS_MAX_DIFF_PER_MILLE % 10U);
    cliPrintf("fault confirm : %lums\n", (unsigned long)APPS_FAULT_CONFIRM_MS);
    return;
  }

  if ((args->argc == 5) && args->isStr(0, "config"))
  {
    uint16_t raw_config[4];

    for (uint8_t i = 0; i < 4U; i++)
    {
      int32_t value = args->getData((uint8_t)(i + 1U));

      if (value < 0)
      {
        raw_config[i] = 0U;
      }
      else if (value > UINT16_MAX)
      {
        raw_config[i] = UINT16_MAX;
      }
      else
      {
        raw_config[i] = (uint16_t)value;
      }
    }

    (void)appsSetConfig(raw_config[0],
                        raw_config[1],
                        raw_config[2],
                        raw_config[3]);
    cliPrintf("signal1 raw : %u..%u\n",
              apps_cal[APPS_SIGNAL1].raw_min,
              apps_cal[APPS_SIGNAL1].raw_max);
    cliPrintf("signal2 raw : %u..%u\n",
              apps_cal[APPS_SIGNAL2].raw_min,
              apps_cal[APPS_SIGNAL2].raw_max);
    cliPrintf("configured : %s\n", apps_calibrated ? "true" : "false");
    return;
  }

  if ((args->argc == 1) && args->isStr(0, "clear"))
  {
    appsClearFault();
    cliPrintf("apps fault clear\n");
    return;
  }

  cliPrintf("apps info\n");
  cliPrintf("apps config\n");
  cliPrintf("apps config signal1_min signal1_max signal2_min signal2_max\n");
  cliPrintf("apps clear\n");
}

#endif
