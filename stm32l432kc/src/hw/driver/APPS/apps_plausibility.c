/*
 * apps_plausibility.c
 */

#include "APPS/apps_plausibility.h"

static uint16_t appsAbsDiffU16(uint16_t a, uint16_t b)
{
  return (a > b) ? (uint16_t)(a - b) : (uint16_t)(b - a);
}

static uint16_t appsClampPerMille(int32_t value)
{
  if (value < 0)
  {
    return 0;
  }
  if (value > (int32_t)APPS_PER_MILLE_MAX)
  {
    return APPS_PER_MILLE_MAX;
  }
  return (uint16_t)value;
}

static uint16_t appsScaleRaw(uint16_t raw, const apps_channel_cal_t *p_cal, bool *p_range_ok)
{
  int32_t raw_min = p_cal->raw_min;
  int32_t raw_max = p_cal->raw_max;
  int32_t raw_low = (raw_min < raw_max) ? raw_min : raw_max;
  int32_t raw_high = (raw_min > raw_max) ? raw_min : raw_max;
  int32_t span = raw_max - raw_min;
  int32_t scaled;

  *p_range_ok = true;

  if (span == 0)
  {
    *p_range_ok = false;
    return 0;
  }

  if ((int32_t)raw < (raw_low - (int32_t)p_cal->low_margin))
  {
    *p_range_ok = false;
  }
  if ((int32_t)raw > (raw_high + (int32_t)p_cal->high_margin))
  {
    *p_range_ok = false;
  }

  scaled = (((int32_t)raw - raw_min) * (int32_t)APPS_PER_MILLE_MAX) / span;
  return appsClampPerMille(scaled);
}

void appsInit(apps_state_t *p_state, const apps_config_t *p_cfg)
{
  p_state->cfg = *p_cfg;
  p_state->fault_count = 0;
  p_state->latched_status = APPS_STATUS_OK;
  p_state->fault_latched = false;

  if (p_state->cfg.max_diff_per_mille == 0)
  {
    p_state->cfg.max_diff_per_mille = APPS_DEFAULT_DIFF_PER_MILLE;
  }
  if (p_state->cfg.fault_confirm_samples == 0)
  {
    p_state->cfg.fault_confirm_samples = 1;
  }
  if (p_state->cfg.max_torque_cmd <= 0)
  {
    p_state->cfg.max_torque_cmd = APPS_BAMOCAR_CMD_MAX;
  }
}

void appsClearFault(apps_state_t *p_state)
{
  p_state->fault_count = 0;
  p_state->latched_status = APPS_STATUS_OK;
  p_state->fault_latched = false;
}

int16_t appsPedalToBamocarTorque(uint16_t pedal_per_mille, int16_t max_torque_cmd)
{
  int32_t cmd;

  if (pedal_per_mille > APPS_PER_MILLE_MAX)
  {
    pedal_per_mille = APPS_PER_MILLE_MAX;
  }
  if (max_torque_cmd <= 0)
  {
    max_torque_cmd = APPS_BAMOCAR_CMD_MAX;
  }

  cmd = ((int32_t)pedal_per_mille * (int32_t)max_torque_cmd) / (int32_t)APPS_PER_MILLE_MAX;
  if (cmd > APPS_BAMOCAR_CMD_MAX)
  {
    cmd = APPS_BAMOCAR_CMD_MAX;
  }

  return (int16_t)cmd;
}

apps_result_t appsUpdate(apps_state_t *p_state, uint16_t raw_ch1, uint16_t raw_ch2)
{
  apps_result_t ret;
  bool ch1_range_ok;
  bool ch2_range_ok;
  uint32_t instant_status = APPS_STATUS_OK;
  uint16_t diff_per_mille;

  ret.ch1_per_mille = appsScaleRaw(raw_ch1, &p_state->cfg.ch1, &ch1_range_ok);
  ret.ch2_per_mille = appsScaleRaw(raw_ch2, &p_state->cfg.ch2, &ch2_range_ok);
  ret.pedal_per_mille = 0;
  ret.torque_cmd = 0;
  ret.status = APPS_STATUS_OK;
  ret.valid = false;

  if (ch1_range_ok != true)
  {
    instant_status |= APPS_STATUS_CH1_RANGE_FAULT;
  }
  if (ch2_range_ok != true)
  {
    instant_status |= APPS_STATUS_CH2_RANGE_FAULT;
  }

  diff_per_mille = appsAbsDiffU16(ret.ch1_per_mille, ret.ch2_per_mille);
  if (diff_per_mille > p_state->cfg.max_diff_per_mille)
  {
    instant_status |= APPS_STATUS_DIFF_FAULT;
  }

  if (instant_status != APPS_STATUS_OK)
  {
    if (p_state->fault_count < p_state->cfg.fault_confirm_samples)
    {
      p_state->fault_count++;
    }
    if (p_state->fault_count >= p_state->cfg.fault_confirm_samples)
    {
      p_state->fault_latched = true;
      p_state->latched_status |= instant_status;
    }
  }
  else if (p_state->fault_latched != true)
  {
    p_state->fault_count = 0;
  }

  ret.status = instant_status | p_state->latched_status;
  if (p_state->fault_latched == true)
  {
    ret.status |= APPS_STATUS_FAULT_LATCHED;
  }

  if ((instant_status == APPS_STATUS_OK) && (p_state->fault_latched != true))
  {
    ret.valid = true;
    ret.pedal_per_mille = (uint16_t)(((uint32_t)ret.ch1_per_mille + (uint32_t)ret.ch2_per_mille) / 2U);
    ret.torque_cmd = appsPedalToBamocarTorque(ret.pedal_per_mille, p_state->cfg.max_torque_cmd);
  }

  return ret;
}
