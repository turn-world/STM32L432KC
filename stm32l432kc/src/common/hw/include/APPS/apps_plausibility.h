/*
 * apps_plausibility.h
 *
 * Core APPS plausibility logic. This file intentionally has no STM32 HAL
 * dependency, so it can be unit-tested on a PC before being wired to ADC code.
 */

#ifndef SRC_COMMON_HW_INCLUDE_APPS_APPS_PLAUSIBILITY_H_
#define SRC_COMMON_HW_INCLUDE_APPS_APPS_PLAUSIBILITY_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#define APPS_PER_MILLE_MAX             1000U
#define APPS_DEFAULT_DIFF_PER_MILLE    100U
#define APPS_BAMOCAR_CMD_MAX           32767

typedef enum
{
  APPS_STATUS_OK              = 0x00000000U,
  APPS_STATUS_CH1_RANGE_FAULT = 0x00000001U,
  APPS_STATUS_CH2_RANGE_FAULT = 0x00000002U,
  APPS_STATUS_DIFF_FAULT      = 0x00000004U,
  APPS_STATUS_FAULT_LATCHED   = 0x00000008U,
} apps_status_t;

typedef struct
{
  uint16_t raw_min;
  uint16_t raw_max;
  uint16_t low_margin;
  uint16_t high_margin;
} apps_channel_cal_t;

typedef struct
{
  apps_channel_cal_t ch1;
  apps_channel_cal_t ch2;
  uint16_t max_diff_per_mille;
  uint16_t fault_confirm_samples;
  int16_t max_torque_cmd;
} apps_config_t;

typedef struct
{
  apps_config_t cfg;
  uint16_t fault_count;
  uint32_t latched_status;
  bool fault_latched;
} apps_state_t;

typedef struct
{
  uint16_t ch1_per_mille;
  uint16_t ch2_per_mille;
  uint16_t pedal_per_mille;
  int16_t torque_cmd;
  uint32_t status;
  bool valid;
} apps_result_t;

void appsInit(apps_state_t *p_state, const apps_config_t *p_cfg);
void appsClearFault(apps_state_t *p_state);
apps_result_t appsUpdate(apps_state_t *p_state, uint16_t raw_ch1, uint16_t raw_ch2);
/* Torque calculation is intentionally disabled in apps_plausibility.c. */
bool appsSendBamocarCommand(const apps_result_t *p_result, int16_t prepared_command);
apps_result_t appsUpdateAndSend(apps_state_t *p_state,
                                uint16_t raw_ch1,
                                uint16_t raw_ch2,
                                int16_t prepared_command);
bool appsCliInit(void);

#ifdef __cplusplus
}
#endif

#endif /* SRC_COMMON_HW_INCLUDE_APPS_APPS_PLAUSIBILITY_H_ */
