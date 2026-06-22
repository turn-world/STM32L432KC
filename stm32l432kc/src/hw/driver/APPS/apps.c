/*
 * apps.c
 */

#include "APPS/apps.h"
#include "APPS/bamocar_can.h"
#include "adc.h"
#include "bsp.h"

#include <string.h>


#define APPS_TIMED_FAULT_MASK  (APPS_STATUS_ADC_READ_FAULT      | \
                                APPS_STATUS_SIGNAL1_RANGE_FAULT | \
                                APPS_STATUS_SIGNAL2_RANGE_FAULT | \
                                APPS_STATUS_DIFF_FAULT)


typedef struct
{
  uint8_t  adc_ch;
  uint16_t raw_min;
  uint16_t raw_max;
  uint16_t low_margin;
  uint16_t high_margin;
} apps_input_config_t;

typedef struct
{
  apps_input_config_t input[APPS_SIGNAL_COUNT];
  uint8_t  can_ch;
  uint16_t max_diff_per_mille;
  uint32_t fault_confirm_ms;
  bool     configured;
} apps_config_t;

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


static apps_config_t apps_config;
static apps_result_t apps_result;
static apps_fault_t apps_fault;
static bool apps_initialized = false;


static bool appsSignalIsValid(uint8_t signal)  // Signal1/2 index 범위 검사
{
  return signal < APPS_SIGNAL_COUNT;
}

static uint16_t appsAbsDiffU16(uint16_t a, uint16_t b)  // 두 값의 절대 차이 계산
{
  return (a > b) ? (uint16_t)(a - b) : (uint16_t)(b - a);
}

static uint16_t appsClampPerMille(int32_t value)  // 비율을 0~1000으로 제한
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

static bool appsConfigIsValid(void)  // 센서 보정값 유효성 검사
{
  if (apps_config.configured != true)
  {
    return false;
  }

  for (uint8_t i = 0; i < APPS_SIGNAL_COUNT; i++)
  {
    if (apps_config.input[i].raw_min == apps_config.input[i].raw_max)
    {
      return false;
    }
  }

  return true;
}

static void appsResetFaultState(void)  // fault timer와 latch 상태 초기화
{
  memset(&apps_fault, 0, sizeof(apps_fault));
}

static uint16_t appsScaleRaw(uint16_t raw,
                             const apps_input_config_t *p_input,
                             bool *p_range_ok)  // raw를 0~1000 pedal 값으로 변환
{
  int32_t raw_min = p_input->raw_min;
  int32_t raw_max = p_input->raw_max;
  int32_t raw_low = (raw_min < raw_max) ? raw_min : raw_max;
  int32_t raw_high = (raw_min > raw_max) ? raw_min : raw_max;
  int32_t span = raw_max - raw_min;
  int32_t scaled;

  *p_range_ok = true;

  if ((int32_t)raw < (raw_low - (int32_t)p_input->low_margin))
  {
    *p_range_ok = false;
  }
  if ((int32_t)raw > (raw_high + (int32_t)p_input->high_margin))
  {
    *p_range_ok = false;
  }

  scaled = (((int32_t)raw - raw_min) * (int32_t)APPS_PER_MILLE_MAX) /
           span;

  return appsClampPerMille(scaled);
}

static void appsUpdateFaultTimer(uint32_t instant_status)  // fault 지속 시간 및 latch 처리
{
  uint32_t timed_fault = instant_status & APPS_TIMED_FAULT_MASK;

  if (timed_fault == APPS_STATUS_OK)
  {
    apps_fault.timer_active       = false;
    apps_fault.started_ms         = 0U;
    apps_fault.elapsed_ms         = 0U;
    apps_fault.pending_status     = APPS_STATUS_OK;
  }
  else
  {
    uint32_t now_ms = millis();

    if ((apps_fault.timer_active != true) ||
        (apps_fault.pending_status != timed_fault))
    {
      apps_fault.timer_active     = true;
      apps_fault.started_ms       = now_ms;
      apps_fault.elapsed_ms       = 0U;
      apps_fault.pending_status   = timed_fault;
    }
    else
    {
      apps_fault.elapsed_ms = now_ms - apps_fault.started_ms;

      if (apps_fault.elapsed_ms >= apps_config.fault_confirm_ms)
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

static bool appsEvaluateRaw(uint16_t raw_signal1,
                            uint16_t raw_signal2)  // 전압/페달 변환 및 plausibility 판단
{
  bool range_ok[APPS_SIGNAL_COUNT] = {false, false};
  uint32_t instant_status = APPS_STATUS_OK;

  apps_result.raw[APPS_SIGNAL1] = raw_signal1;
  apps_result.raw[APPS_SIGNAL2] = raw_signal2;

  for (uint8_t i = 0; i < APPS_SIGNAL_COUNT; i++)
  {
    apps_result.voltage_mv[i] =
        (uint16_t)adcConvMillivolts(apps_config.input[i].adc_ch,
                                    apps_result.raw[i]);
    apps_result.percent_per_mille[i] = 0U;
  }

  apps_result.difference_per_mille = 0U;
  apps_result.pedal_per_mille = 0U;
  apps_result.command_sent = 0;
  apps_result.adc_ok = true;
  apps_result.valid = false;
  apps_result.can_tx_ok = false;

  if ((apps_initialized != true) || (appsConfigIsValid() != true))
  {
    appsUpdateFaultTimer(APPS_STATUS_NOT_CONFIGURED);
    return false;
  }

  for (uint8_t i = 0; i < APPS_SIGNAL_COUNT; i++)
  {
    apps_result.percent_per_mille[i] =
        appsScaleRaw(apps_result.raw[i],
                     &apps_config.input[i],
                     &range_ok[i]);
  }

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

  if (apps_result.difference_per_mille >=
      apps_config.max_diff_per_mille)
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

bool appsInit(uint8_t signal1_adc_ch,
              uint8_t signal2_adc_ch,
              uint8_t can_ch)
{
  memset(&apps_config, 0, sizeof(apps_config));
  memset(&apps_result, 0, sizeof(apps_result));
  appsResetFaultState();

  apps_config.input[APPS_SIGNAL1].adc_ch  = signal1_adc_ch;
  apps_config.input[APPS_SIGNAL2].adc_ch  = signal2_adc_ch;
  apps_config.can_ch                      = can_ch;
  apps_config.max_diff_per_mille          = APPS_DEFAULT_DIFF_PER_MILLE;
  apps_config.fault_confirm_ms            = APPS_DEFAULT_FAULT_CONFIRM_MS;
  apps_config.configured                  = false;

  apps_initialized = true;
  apps_result.status = APPS_STATUS_NOT_CONFIGURED;

  return true;
}

bool appsSetConfig(uint16_t signal1_raw_min,
                   uint16_t signal1_raw_max,
                   uint16_t signal2_raw_min,
                   uint16_t signal2_raw_max)
{
  apps_config.input[APPS_SIGNAL1].raw_min = signal1_raw_min;
  apps_config.input[APPS_SIGNAL1].raw_max = signal1_raw_max;
  apps_config.input[APPS_SIGNAL2].raw_min = signal2_raw_min;
  apps_config.input[APPS_SIGNAL2].raw_max = signal2_raw_max;

  apps_config.configured =
      (signal1_raw_min != signal1_raw_max) &&
      (signal2_raw_min != signal2_raw_max);

  appsClearFault();

  return apps_config.configured;
}

void appsSetRangeMargins(uint16_t signal1_low_margin,
                        uint16_t signal1_high_margin,
                        uint16_t signal2_low_margin,
                        uint16_t signal2_high_margin)
{
  apps_config.input[APPS_SIGNAL1].low_margin = signal1_low_margin;
  apps_config.input[APPS_SIGNAL1].high_margin = signal1_high_margin;
  apps_config.input[APPS_SIGNAL2].low_margin = signal2_low_margin;
  apps_config.input[APPS_SIGNAL2].high_margin = signal2_high_margin;
}

void appsSetFaultLimits(uint16_t max_diff_per_mille,
                        uint32_t fault_confirm_ms)
{
  if ((max_diff_per_mille == 0U) ||
      (max_diff_per_mille > APPS_PER_MILLE_MAX))
  {
    max_diff_per_mille = APPS_DEFAULT_DIFF_PER_MILLE;
  }
  if (fault_confirm_ms == 0U)
  {
    fault_confirm_ms = APPS_DEFAULT_FAULT_CONFIRM_MS;
  }

  apps_config.max_diff_per_mille = max_diff_per_mille;
  apps_config.fault_confirm_ms = fault_confirm_ms;
  appsClearFault();
}

void appsClearFault(void)
{
  appsResetFaultState();
  apps_result.valid = false;
  apps_result.command_sent = 0;
  apps_result.can_tx_ok = false;
  apps_result.status =
      appsConfigIsValid() ? APPS_STATUS_OK : APPS_STATUS_NOT_CONFIGURED;
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

    if (appsConfigIsValid() != true)
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

  raw_signal1 = adcRead(apps_config.input[APPS_SIGNAL1].adc_ch);
  raw_signal2 = adcRead(apps_config.input[APPS_SIGNAL2].adc_ch);

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
  apps_result.can_tx_ok =
      bamocarCanSendTorqueCmd(apps_config.can_ch, command_to_send);

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
  return appsConfigIsValid();
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
  return appsSignalIsValid(signal) ? apps_config.input[signal].adc_ch : 0U;
}

uint8_t appsGetCanChannel(void)
{
  return apps_config.can_ch;
}

uint16_t appsGetRawMin(uint8_t signal)
{
  return appsSignalIsValid(signal) ? apps_config.input[signal].raw_min : 0U;
}

uint16_t appsGetRawMax(uint8_t signal)
{
  return appsSignalIsValid(signal) ? apps_config.input[signal].raw_max : 0U;
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
  return apps_config.max_diff_per_mille;
}

uint32_t appsGetFaultConfirmMs(void)
{
  return apps_config.fault_confirm_ms;
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
