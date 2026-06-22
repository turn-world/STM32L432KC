/*
 * apps_plausibility.c
 */

#include "APPS/apps_plausibility.h"
#include "APPS/bamocar_can.h"
#include "can.h"
#include "hw_def.h"

#ifdef _USE_HW_CLI
#include "cli.h"
#endif

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

#if 0  /* Torque calculation is not used. */
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
#endif

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

    /*
     * Torque calculation is intentionally disabled.
     * A command prepared by the upper control layer must be sent immediately
     * after this function returns valid == true.
     */
    /* ret.torque_cmd = appsPedalToBamocarTorque(ret.pedal_per_mille,
                                                 p_state->cfg.max_torque_cmd); */
  }

  return ret;
}

bool appsSendBamocarCommand(const apps_result_t *p_result, int16_t prepared_command)
{
  int16_t command_to_send = 0;

  if (p_result == NULL)
  {
    return false;
  }

  /*
   * No torque calculation is performed here.
   * The upper control layer supplies an already prepared Bamocar command.
   * Fault or invalid input always overrides that command to zero.
   */
  if (p_result->valid == true)
  {
    command_to_send = prepared_command;
  }

  return bamocarCanSendTorqueCmd(_DEF_CAN1, command_to_send);
}

apps_result_t appsUpdateAndSend(apps_state_t *p_state,
                                uint16_t raw_ch1,
                                uint16_t raw_ch2,
                                int16_t prepared_command)
{
  apps_result_t result;

  result = appsUpdate(p_state, raw_ch1, raw_ch2);

  /*
   * Send in the same control-cycle as the plausibility decision.
   * valid == false is forced to command zero by appsSendBamocarCommand().
   */
  (void)appsSendBamocarCommand(&result, prepared_command);

  return result;
}

#ifdef _USE_HW_CLI
static apps_state_t cli_apps_state;

static const apps_config_t cli_apps_cfg =
{
  .ch1 =
  {
    .raw_min     = 0,
    .raw_max     = 4095,
    .low_margin  = 0,
    .high_margin = 0,
  },
  .ch2 =
  {
    .raw_min     = 0,
    .raw_max     = 4095,
    .low_margin  = 0,
    .high_margin = 0,
  },
  .max_diff_per_mille   = APPS_DEFAULT_DIFF_PER_MILLE,
  .fault_confirm_samples = 1,
  .max_torque_cmd       = APPS_BAMOCAR_CMD_MAX,
};

static uint16_t appsCliGetU16(cli_args_t *args, uint8_t index)
{
  int32_t value;

  value = args->getData(index);
  if (value < 0)
  {
    return 0;
  }
  if (value > 65535)
  {
    return 65535;
  }

  return (uint16_t)value;
}

static void appsCliPrintPercent(const char *name, uint16_t per_mille)
{
  cliPrintf("%s : %u.%u%%\n", name, per_mille / 10U, per_mille % 10U);
}

static void appsCliPrintStatus(uint32_t status)
{
  cliPrintf("status : 0x%08lX\n", status);

  if (status == APPS_STATUS_OK)
  {
    cliPrintf("  OK\n");
  }
  if (status & APPS_STATUS_CH1_RANGE_FAULT)
  {
    cliPrintf("  CH1_RANGE_FAULT\n");
  }
  if (status & APPS_STATUS_CH2_RANGE_FAULT)
  {
    cliPrintf("  CH2_RANGE_FAULT\n");
  }
  if (status & APPS_STATUS_DIFF_FAULT)
  {
    cliPrintf("  DIFF_FAULT\n");
  }
  if (status & APPS_STATUS_FAULT_LATCHED)
  {
    cliPrintf("  FAULT_LATCHED\n");
  }
}

static void appsCliPrintFrame(int16_t torque_cmd)
{
  bamocar_can_frame_t frame;

  bamocarCanMakeTorqueCmd(BAMOCAR_CAN_DEFAULT_RX_ID, torque_cmd, &frame);

  cliPrintf("bamocar torque frame\n");
  cliPrintf("  id   : 0x%03lX\n", frame.id);
  cliPrintf("  dlc  : %d\n", frame.dlc);
  cliPrintf("  data :");
  for (uint8_t i = 0; i < frame.dlc; i++)
  {
    cliPrintf(" 0x%02X", frame.data[i]);
  }
  cliPrintf("\n");
}

static void cliApps(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 1 && args->isStr(0, "clear"))
  {
    appsClearFault(&cli_apps_state);
    cliPrintf("apps fault clear\n");
    ret = true;
  }

#if 0  /* Torque calculation CLI is not used. */
  if (args->argc == 2 && args->isStr(0, "torque"))
  {
    uint16_t pedal_per_mille;
    int16_t torque_cmd;

    pedal_per_mille = appsCliGetU16(args, 1);
    if (pedal_per_mille > APPS_PER_MILLE_MAX)
    {
      pedal_per_mille = APPS_PER_MILLE_MAX;
    }

    torque_cmd = appsPedalToBamocarTorque(pedal_per_mille, APPS_BAMOCAR_CMD_MAX);

    appsCliPrintPercent("pedal", pedal_per_mille);
    cliPrintf("torque_cmd : %d\n", torque_cmd);
    appsCliPrintFrame(torque_cmd);
    ret = true;
  }
#endif

  if (args->argc == 3 && args->isStr(0, "test"))
  {
    uint16_t raw_ch1;
    uint16_t raw_ch2;
    apps_result_t result;

    raw_ch1 = appsCliGetU16(args, 1);
    raw_ch2 = appsCliGetU16(args, 2);
    result = appsUpdate(&cli_apps_state, raw_ch1, raw_ch2);

    cliPrintf("raw_ch1 : %u\n", raw_ch1);
    cliPrintf("raw_ch2 : %u\n", raw_ch2);
    appsCliPrintPercent("ch1", result.ch1_per_mille);
    appsCliPrintPercent("ch2", result.ch2_per_mille);
    appsCliPrintPercent("pedal", result.pedal_per_mille);
    cliPrintf("valid : %s\n", result.valid ? "true" : "false");
    cliPrintf("torque_cmd : %d\n", result.torque_cmd);
    appsCliPrintStatus(result.status);
    appsCliPrintFrame(result.torque_cmd);
    ret = true;
  }

  if (ret != true)
  {
    cliPrintf("apps test raw_ch1 raw_ch2\n");
    cliPrintf("apps clear\n");
  }
}
#endif

bool appsCliInit(void)
{
#ifdef _USE_HW_CLI
  appsInit(&cli_apps_state, &cli_apps_cfg);
  return cliAdd("apps", cliApps);
#else
  return true;
#endif
}
