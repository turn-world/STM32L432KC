/*
 * telemetry.h
 *
 * Raw CAN and decoded signal CSV logger backed by FatFS.
 */

#ifndef SRC_AP_TELEMETRY_TELEMETRY_H_
#define SRC_AP_TELEMETRY_TELEMETRY_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_HW_FATFS

#include "can.h"
#include "ff.h"

#define TELEMETRY_PATH_LEN  32

#define TELEMETRY_SIGNAL_BMS_PACK_MV              (1UL << 0)
#define TELEMETRY_SIGNAL_BMS_PACK_MA              (1UL << 1)
#define TELEMETRY_SIGNAL_BMS_SOC_PERMILLE         (1UL << 2)
#define TELEMETRY_SIGNAL_BMS_MAX_TEMP_DECIC       (1UL << 3)
#define TELEMETRY_SIGNAL_BMS_STATUS               (1UL << 4)
#define TELEMETRY_SIGNAL_BAMOCAR_RPM              (1UL << 5)
#define TELEMETRY_SIGNAL_BAMOCAR_TORQUE_RAW       (1UL << 6)
#define TELEMETRY_SIGNAL_BAMOCAR_DC_BUS_MV        (1UL << 7)
#define TELEMETRY_SIGNAL_BAMOCAR_MOTOR_TEMP_DECIC (1UL << 8)
#define TELEMETRY_SIGNAL_BAMOCAR_IGBT_TEMP_DECIC  (1UL << 9)
#define TELEMETRY_SIGNAL_BAMOCAR_STATUS           (1UL << 10)
#define TELEMETRY_SIGNAL_BAMOCAR_ERROR_WARNING    (1UL << 11)

typedef enum
{
  TELEMETRY_STATE_STOPPED,
  TELEMETRY_STATE_MOUNTED,
  TELEMETRY_STATE_LOGGING,
  TELEMETRY_STATE_ERROR,
} telemetry_state_t;

typedef struct
{
  telemetry_state_t state;
  uint8_t  can_ch;
  uint32_t lines_written;
  uint32_t bytes_written;
  uint32_t sync_count;
  uint32_t write_error_count;
  FRESULT  last_result;
  char     path[TELEMETRY_PATH_LEN];
  char     signal_path[TELEMETRY_PATH_LEN];
  uint32_t signal_lines_written;
  uint32_t signal_bytes_written;
} telemetry_info_t;

typedef struct
{
  uint32_t valid_mask;
  uint32_t last_update_ms;
  bool     logging;
  uint8_t  can_ch;

  int32_t  bms_pack_mv;
  int32_t  bms_pack_ma;
  uint16_t bms_soc_permille;
  int16_t  bms_max_temp_decic;
  uint32_t bms_status;

  int32_t  bamocar_rpm;
  int32_t  bamocar_torque_raw;
  int32_t  bamocar_dc_bus_mv;
  int16_t  bamocar_motor_temp_decic;
  int16_t  bamocar_igbt_temp_decic;
  uint32_t bamocar_status;
  uint32_t bamocar_error_warning;
} telemetry_signal_snapshot_t;

bool telemetryInit(void);
bool telemetryMount(void);
bool telemetryStart(uint8_t can_ch, const char *path);
void telemetryStop(void);
void telemetryUpdate(void);
bool telemetryLogCan(const can_msg_t *p_msg);
bool telemetryIsLogging(void);
void telemetryGetInfo(telemetry_info_t *p_info);
bool telemetryGetSignalSnapshot(telemetry_signal_snapshot_t *p_snapshot);
const char *telemetryResultToString(FRESULT result);

#endif

#ifdef __cplusplus
}
#endif

#endif /* SRC_AP_TELEMETRY_TELEMETRY_H_ */
