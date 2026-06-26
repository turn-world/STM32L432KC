/*
 * telemetry.c
 */

#include "telemetry.h"

#ifdef _USE_HW_FATFS

#include "apps/bamocar_can.h"
#include "cli.h"
#include "fatfs.h"

/*
 * Telemetry 흐름
 * - CAN RX queue에서 메시지를 꺼내 Orion/Bamocar 신호로 해석한다.
 * - 로깅 OFF 상태에서도 해석값 snapshot은 계속 갱신해서 LCD/TouchGFX가 사용할 수 있게 한다.
 * - 로깅 ON 상태에서는 raw CAN CSV(LOGxxx.CSV)와 해석값 CSV(SIGxxx.CSV)를 함께 기록한다.
 */

#define TELEMETRY_SYNC_PERIOD_MS              1000U
#define TELEMETRY_SYNC_PERIOD_LINES           64U
#define TELEMETRY_SIGNAL_PERIOD_MS            100U
#define TELEMETRY_MAX_LINE_LEN                256U
#define TELEMETRY_MAX_FILES                   1000U

#define TELEMETRY_RAW_PREFIX                  "LOG"
#define TELEMETRY_SIGNAL_PREFIX               "SIG"

#define TELEMETRY_BAMOCAR_REG_ERROR_WARNING   0x8FU
#define TELEMETRY_BAMOCAR_REG_IGBT_TEMP       0x4AU
#define TELEMETRY_BAMOCAR_REG_DC_BUS          0xEBU

/* BAMOCAR 700 V model: manual page 49 says 1 V ~= 31.5 Num for DC bus. */
#define TELEMETRY_BAMOCAR_DC_BUS_NUM_PER_10V  315L

/*
 * --- VERIFY THESE AGAINST YOUR NDrive CONFIGURATION ---
 * Speed / torque / motor-temperature registers are NOT documented in the
 * BAMOCAR-PG-D3 product guide (it only covers 0xEB, 0x4A, 0x40, 0x8F). The IDs
 * and scaling below use the commonly-used BAMOCAR D3 register set together with
 * the EMRAX 228 spec (limiting speed 6500 rpm, motor sensor KTY81/210).
 *
 * If a register ID here does not match what the inverter actually transmits, that
 * SIG*.CSV column just stays empty - the raw LOG*.CSV still records every frame,
 * so nothing is lost. Confirm each value in the BAMOCAR CAN manual / NDrive
 * parameter list and adjust here in one place.
 */
#define TELEMETRY_BAMOCAR_REG_N_ACTUAL        0x30U   /* speed actual: +/-32767 Num = +/- N_max */
#define TELEMETRY_BAMOCAR_REG_TORQUE_ACTUAL   0xA8U   /* torque/current actual: raw Num logged as-is */
#define TELEMETRY_BAMOCAR_REG_MOTOR_TEMP      0x49U   /* motor temperature (EMRAX KTY81/210) */
#define TELEMETRY_BAMOCAR_N_MAX_RPM           6500L   /* EMRAX 228 limiting speed = NDrive N_max */

/*
 * --- ORION BMS: YOU DEFINE THIS LAYOUT IN THE BMS UTILITY ---
 * Orion BMS 2 CAN frames are fully user-configured ("Editing CAN Messages" in the
 * Orion BMS Utility) - there is no factory-fixed layout. The decode below expects
 * ONE 8-byte big-endian (MSB first) frame; configure the BMS to broadcast exactly:
 *
 *   CAN ID 0x6B0, std, 8 bytes, MSB first:
 *     [0..1] Pack Voltage   u16, 0.1 V  / bit
 *     [2..3] Pack Current    i16, 0.1 A  / bit   (signed, + = discharge)
 *     [4..5] Pack SOC        u16, 0.5 %  / bit   (200 = 100.0 %)
 *     [6..7] High Temperature i16, 1 deg C / bit
 *
 * Either match the BMS to this, or change the ID/offsets/scaling here to match
 * your existing BMS profile.
 */
#define TELEMETRY_ORION_CAN_ID                0x6B0U
#define TELEMETRY_ORION_VOLT_MV_PER_BIT       100L    /* 0.1 V  -> 100 mV          */
#define TELEMETRY_ORION_CURR_MA_PER_BIT       100L    /* 0.1 A  -> 100 mA          */
#define TELEMETRY_ORION_SOC_PERMILLE_PER_BIT  5L      /* 0.5 %  -> 5 per-mille     */
#define TELEMETRY_ORION_TEMP_DECIC_PER_BIT    10L     /* 1 degC -> 10 deci-Celsius */

#define TELEMETRY_SIG_BMS_PACK_MV              TELEMETRY_SIGNAL_BMS_PACK_MV
#define TELEMETRY_SIG_BMS_PACK_MA              TELEMETRY_SIGNAL_BMS_PACK_MA
#define TELEMETRY_SIG_BMS_SOC_PERMILLE         TELEMETRY_SIGNAL_BMS_SOC_PERMILLE
#define TELEMETRY_SIG_BMS_MAX_TEMP_DECIC       TELEMETRY_SIGNAL_BMS_MAX_TEMP_DECIC
#define TELEMETRY_SIG_BMS_STATUS               TELEMETRY_SIGNAL_BMS_STATUS
#define TELEMETRY_SIG_BAMOCAR_RPM              TELEMETRY_SIGNAL_BAMOCAR_RPM
#define TELEMETRY_SIG_BAMOCAR_TORQUE_RAW       TELEMETRY_SIGNAL_BAMOCAR_TORQUE_RAW
#define TELEMETRY_SIG_BAMOCAR_DC_BUS_MV        TELEMETRY_SIGNAL_BAMOCAR_DC_BUS_MV
#define TELEMETRY_SIG_BAMOCAR_MOTOR_TEMP_DECIC TELEMETRY_SIGNAL_BAMOCAR_MOTOR_TEMP_DECIC
#define TELEMETRY_SIG_BAMOCAR_IGBT_TEMP_DECIC  TELEMETRY_SIGNAL_BAMOCAR_IGBT_TEMP_DECIC
#define TELEMETRY_SIG_BAMOCAR_STATUS           TELEMETRY_SIGNAL_BAMOCAR_STATUS
#define TELEMETRY_SIG_BAMOCAR_ERROR_WARNING    TELEMETRY_SIGNAL_BAMOCAR_ERROR_WARNING

typedef struct
{
  uint16_t raw;
  int16_t  temp_decic;
} telemetry_temp_point_t;

/* LCD/TouchGFX 표시와 SIG CSV 기록이 같이 사용하는 현재 해석값 저장 영역. */
typedef struct
{
  uint32_t valid_mask;
  bool     dirty;
  uint32_t last_update_ms;

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
} telemetry_signal_t;

/* FatFs 파일 상태, CAN 채널, 기록 통계, 현재 signal snapshot을 함께 보관한다. */
typedef struct
{
  bool     initialized;
  bool     mounted;
  bool     logging;
  bool     raw_file_open;
  bool     signal_file_open;
  FIL      raw_file;
  FIL      signal_file;
  uint8_t  can_ch;
  uint32_t last_sync_ms;
  uint32_t last_signal_log_ms;
  uint32_t lines_since_sync;
  uint32_t lines_written;
  uint32_t signal_lines_written;
  uint32_t bytes_written;
  uint32_t signal_bytes_written;
  uint32_t sync_count;
  uint32_t write_error_count;
  FRESULT  last_result;
  char     path[TELEMETRY_PATH_LEN];
  char     signal_path[TELEMETRY_PATH_LEN];
  telemetry_signal_t signal;
} telemetry_t;

static telemetry_t telemetry;

static const telemetry_temp_point_t bamocar_igbt_temp_table[] =
{
  {16308U, -300}, {16387U, -250}, {16487U, -200}, {16609U, -150},
  {16757U, -100}, {16938U,  -50}, {17151U,    0}, {17400U,   50},
  {17688U,  100}, {18017U,  150}, {18387U,  200}, {18797U,  250},
  {19247U,  300}, {19733U,  350}, {20250U,  400}, {20793U,  450},
  {21357U,  500}, {21933U,  550}, {22515U,  600}, {23097U,  650},
  {23671U,  700}, {24232U,  750}, {24775U,  800}, {25296U,  850},
  {25792U,  900}, {26261U,  950}, {26702U, 1000}, {27114U, 1050},
  {27497U, 1100}, {27851U, 1150}, {28179U, 1200}, {28480U, 1250},
};

#ifdef _USE_HW_CLI
static void cliTelemetry(cli_args_t *args);
#endif

static uint16_t telemetryReadU16Le(const uint8_t *p_data)
{
  return (uint16_t)p_data[0] | ((uint16_t)p_data[1] << 8);
}

static uint32_t telemetryReadU32Le(const uint8_t *p_data)
{
  return (uint32_t)p_data[0] |
         ((uint32_t)p_data[1] << 8) |
         ((uint32_t)p_data[2] << 16) |
         ((uint32_t)p_data[3] << 24);
}

static uint16_t telemetryReadU16Be(const uint8_t *p_data)
{
  return ((uint16_t)p_data[0] << 8) | (uint16_t)p_data[1];
}

static int16_t telemetryBamocarTempRawToDeciC(uint16_t raw)
{
  const uint32_t count = sizeof(bamocar_igbt_temp_table) / sizeof(bamocar_igbt_temp_table[0]);

  if (raw <= bamocar_igbt_temp_table[0].raw)
  {
    return bamocar_igbt_temp_table[0].temp_decic;
  }
  if (raw >= bamocar_igbt_temp_table[count - 1U].raw)
  {
    return bamocar_igbt_temp_table[count - 1U].temp_decic;
  }

  for (uint32_t i = 1U; i < count; i++)
  {
    if (raw <= bamocar_igbt_temp_table[i].raw)
    {
      int32_t raw0 = bamocar_igbt_temp_table[i - 1U].raw;
      int32_t raw1 = bamocar_igbt_temp_table[i].raw;
      int32_t temp0 = bamocar_igbt_temp_table[i - 1U].temp_decic;
      int32_t temp1 = bamocar_igbt_temp_table[i].temp_decic;

      return (int16_t)(temp0 + (((int32_t)raw - raw0) * (temp1 - temp0)) / (raw1 - raw0));
    }
  }

  return 0;
}

static bool telemetryAppendI32(char *line, uint32_t line_len, int *p_len, uint32_t valid_mask, int32_t value)
{
  int remain = (int)line_len - *p_len;
  int written;

  if ((telemetry.signal.valid_mask & valid_mask) != 0U)
  {
    written = snprintf(&line[*p_len], remain, ",%ld", (long)value);
  }
  else
  {
    written = snprintf(&line[*p_len], remain, ",");
  }

  if ((written < 0) || (written >= remain))
  {
    telemetry.write_error_count++;
    return false;
  }

  *p_len += written;
  return true;
}

static bool telemetryAppendU32(char *line, uint32_t line_len, int *p_len, uint32_t valid_mask, uint32_t value)
{
  int remain = (int)line_len - *p_len;
  int written;

  if ((telemetry.signal.valid_mask & valid_mask) != 0U)
  {
    written = snprintf(&line[*p_len], remain, ",%lu", (unsigned long)value);
  }
  else
  {
    written = snprintf(&line[*p_len], remain, ",");
  }

  if ((written < 0) || (written >= remain))
  {
    telemetry.write_error_count++;
    return false;
  }

  *p_len += written;
  return true;
}

static bool telemetryAppendHex32(char *line, uint32_t line_len, int *p_len, uint32_t valid_mask, uint32_t value)
{
  int remain = (int)line_len - *p_len;
  int written;

  if ((telemetry.signal.valid_mask & valid_mask) != 0U)
  {
    written = snprintf(&line[*p_len], remain, ",0x%08lX", (unsigned long)value);
  }
  else
  {
    written = snprintf(&line[*p_len], remain, ",");
  }

  if ((written < 0) || (written >= remain))
  {
    telemetry.write_error_count++;
    return false;
  }

  *p_len += written;
  return true;
}

/* FatFs write 실패 시 로깅을 멈춰 이후 데이터가 깨진 상태로 계속 기록되지 않게 한다. */
static bool telemetryWriteFileString(FIL *p_file, const char *str, uint32_t *p_bytes_written)
{
  UINT written = 0;
  UINT len;

  if ((telemetry.logging != true) || (p_file == NULL) || (str == NULL))
  {
    return false;
  }

  len = (UINT)strlen(str);
  telemetry.last_result = f_write(p_file, str, len, &written);

  if ((telemetry.last_result != FR_OK) || (written != len))
  {
    telemetry.write_error_count++;
    telemetry.logging = false;
    telemetry.last_result = (telemetry.last_result == FR_OK) ? FR_DISK_ERR : telemetry.last_result;
    return false;
  }

  if (p_bytes_written != NULL)
  {
    *p_bytes_written += written;
  }

  return true;
}

static bool telemetryWriteRawString(const char *str)
{
  return telemetryWriteFileString(&telemetry.raw_file, str, &telemetry.bytes_written);
}

static bool telemetryWriteSignalString(const char *str)
{
  return telemetryWriteFileString(&telemetry.signal_file, str, &telemetry.signal_bytes_written);
}

/* 주기/라인 수 기준으로 f_sync()를 호출해 전원 차단 시 손실 구간을 줄인다. */
static bool telemetrySyncIfNeeded(bool force)
{
  uint32_t now_ms;

  if (telemetry.logging != true)
  {
    return false;
  }

  now_ms = millis();

  if ((force == true) ||
      (telemetry.lines_since_sync >= TELEMETRY_SYNC_PERIOD_LINES) ||
      ((now_ms - telemetry.last_sync_ms) >= TELEMETRY_SYNC_PERIOD_MS))
  {
    if (telemetry.raw_file_open == true)
    {
      telemetry.last_result = f_sync(&telemetry.raw_file);
      if (telemetry.last_result != FR_OK)
      {
        telemetry.write_error_count++;
        telemetry.logging = false;
        return false;
      }
    }

    if (telemetry.signal_file_open == true)
    {
      telemetry.last_result = f_sync(&telemetry.signal_file);
      if (telemetry.last_result != FR_OK)
      {
        telemetry.write_error_count++;
        telemetry.logging = false;
        return false;
      }
    }

    telemetry.lines_since_sync = 0U;
    telemetry.last_sync_ms = now_ms;
    telemetry.sync_count++;
  }

  return true;
}

static bool telemetryMakePath(char *path, uint32_t path_len, const char *prefix, uint32_t index)
{
  const char *drive_path = (SDPath[0] != 0) ? SDPath : "0:/";
  int written;

  written = snprintf(path, path_len, "%s%s%03lu.CSV", drive_path, prefix, (unsigned long)index);
  return (written > 0) && ((uint32_t)written < path_len);
}

static bool telemetryMakeAutoPathWithPrefix(char *path, uint32_t path_len, const char *prefix)
{
  FILINFO info;

  if ((path == NULL) || (path_len < sizeof("0:/LOG000.CSV")))
  {
    return false;
  }

  for (uint32_t i = 0; i < TELEMETRY_MAX_FILES; i++)
  {
    if (telemetryMakePath(path, path_len, prefix, i) != true)
    {
      telemetry.last_result = FR_INVALID_NAME;
      return false;
    }

    telemetry.last_result = f_stat(path, &info);
    if (telemetry.last_result == FR_NO_FILE)
    {
      telemetry.last_result = FR_OK;
      return true;
    }
    if (telemetry.last_result != FR_OK)
    {
      return false;
    }
  }

  telemetry.last_result = FR_DENIED;
  return false;
}

static bool telemetryMakeAutoPaths(char *raw_path, uint32_t raw_path_len,
                                    char *signal_path, uint32_t signal_path_len)
{
  FILINFO info;

  for (uint32_t i = 0; i < TELEMETRY_MAX_FILES; i++)
  {
    if ((telemetryMakePath(raw_path, raw_path_len, TELEMETRY_RAW_PREFIX, i) != true) ||
        (telemetryMakePath(signal_path, signal_path_len, TELEMETRY_SIGNAL_PREFIX, i) != true))
    {
      telemetry.last_result = FR_INVALID_NAME;
      return false;
    }

    telemetry.last_result = f_stat(raw_path, &info);
    if (telemetry.last_result == FR_OK)
    {
      continue;
    }
    if (telemetry.last_result != FR_NO_FILE)
    {
      return false;
    }

    telemetry.last_result = f_stat(signal_path, &info);
    if (telemetry.last_result == FR_NO_FILE)
    {
      telemetry.last_result = FR_OK;
      return true;
    }
    if (telemetry.last_result != FR_OK)
    {
      return false;
    }
  }

  telemetry.last_result = FR_DENIED;
  return false;
}

static bool telemetryWriteRawHeaderIfEmpty(void)
{
  if (f_size(&telemetry.raw_file) != 0U)
  {
    return true;
  }

  return telemetryWriteRawString("time_ms,id_type,id,dlc,b0,b1,b2,b3,b4,b5,b6,b7\r\n");
}

static bool telemetryWriteSignalHeaderIfEmpty(void)
{
  if (f_size(&telemetry.signal_file) != 0U)
  {
    return true;
  }

  return telemetryWriteSignalString(
      "time_ms,bms_pack_mv,bms_pack_ma,bms_soc_permille,bms_max_temp_decic,bms_status,"
      "bamocar_rpm,bamocar_torque_raw,bamocar_dc_bus_mv,bamocar_motor_temp_decic,"
      "bamocar_igbt_temp_decic,bamocar_status,bamocar_error_warning\r\n");
}

/* Orion BMS 사용자 CAN layout을 현재 signal 값으로 변환한다. */
static bool telemetryDecodeOrion(const can_msg_t *p_msg)
{
  uint16_t raw;

  if ((p_msg->id_type != CAN_STD) ||
      (p_msg->id != TELEMETRY_ORION_CAN_ID) ||
      (p_msg->length < 8U))
  {
    return false;
  }

  raw = telemetryReadU16Be(&p_msg->data[0]);
  telemetry.signal.bms_pack_mv = (int32_t)raw * TELEMETRY_ORION_VOLT_MV_PER_BIT;
  telemetry.signal.valid_mask |= TELEMETRY_SIG_BMS_PACK_MV;

  raw = telemetryReadU16Be(&p_msg->data[2]);
  telemetry.signal.bms_pack_ma = (int32_t)(int16_t)raw * TELEMETRY_ORION_CURR_MA_PER_BIT;
  telemetry.signal.valid_mask |= TELEMETRY_SIG_BMS_PACK_MA;

  raw = telemetryReadU16Be(&p_msg->data[4]);
  telemetry.signal.bms_soc_permille =
      (uint16_t)((int32_t)raw * TELEMETRY_ORION_SOC_PERMILLE_PER_BIT);
  telemetry.signal.valid_mask |= TELEMETRY_SIG_BMS_SOC_PERMILLE;

  raw = telemetryReadU16Be(&p_msg->data[6]);
  telemetry.signal.bms_max_temp_decic =
      (int16_t)((int32_t)(int16_t)raw * TELEMETRY_ORION_TEMP_DECIC_PER_BIT);
  telemetry.signal.valid_mask |= TELEMETRY_SIG_BMS_MAX_TEMP_DECIC;

  telemetry.signal.last_update_ms = p_msg->timestamp;
  telemetry.signal.dirty = true;

  return true;
}

/* Bamocar register response frame을 현재 signal 값으로 변환한다. */
static bool telemetryDecodeBamocar(const can_msg_t *p_msg)
{
  uint8_t reg_id;
  uint16_t raw16;
  bool updated = false;

  if ((p_msg->id_type != CAN_STD) ||
      (p_msg->id != BAMOCAR_CAN_DEFAULT_TX_ID) ||
      (p_msg->length < 3U))
  {
    return false;
  }

  reg_id = p_msg->data[0];

  switch (reg_id)
  {
    case BAMOCAR_REG_STATUS:
      raw16 = telemetryReadU16Le(&p_msg->data[1]);
      telemetry.signal.bamocar_status = raw16;
      telemetry.signal.valid_mask |= TELEMETRY_SIG_BAMOCAR_STATUS;
      updated = true;
      break;

    case TELEMETRY_BAMOCAR_REG_ERROR_WARNING:
      if (p_msg->length >= 5U)
      {
        telemetry.signal.bamocar_error_warning = telemetryReadU32Le(&p_msg->data[1]);
        telemetry.signal.valid_mask |= TELEMETRY_SIG_BAMOCAR_ERROR_WARNING;
        updated = true;
      }
      break;

    case TELEMETRY_BAMOCAR_REG_DC_BUS:
      raw16 = telemetryReadU16Le(&p_msg->data[1]);
      telemetry.signal.bamocar_dc_bus_mv =
          (int32_t)(((int32_t)raw16 * 10000L) / TELEMETRY_BAMOCAR_DC_BUS_NUM_PER_10V);
      telemetry.signal.valid_mask |= TELEMETRY_SIG_BAMOCAR_DC_BUS_MV;
      updated = true;
      break;

    case TELEMETRY_BAMOCAR_REG_IGBT_TEMP:
      raw16 = telemetryReadU16Le(&p_msg->data[1]);
      telemetry.signal.bamocar_igbt_temp_decic = telemetryBamocarTempRawToDeciC(raw16);
      telemetry.signal.valid_mask |= TELEMETRY_SIG_BAMOCAR_IGBT_TEMP_DECIC;
      updated = true;
      break;

    case TELEMETRY_BAMOCAR_REG_N_ACTUAL:
      raw16 = telemetryReadU16Le(&p_msg->data[1]);
      telemetry.signal.bamocar_rpm =
          ((int32_t)(int16_t)raw16 * TELEMETRY_BAMOCAR_N_MAX_RPM) / 32767L;
      telemetry.signal.valid_mask |= TELEMETRY_SIG_BAMOCAR_RPM;
      updated = true;
      break;

    case TELEMETRY_BAMOCAR_REG_TORQUE_ACTUAL:
      raw16 = telemetryReadU16Le(&p_msg->data[1]);
      telemetry.signal.bamocar_torque_raw = (int32_t)(int16_t)raw16;
      telemetry.signal.valid_mask |= TELEMETRY_SIG_BAMOCAR_TORQUE_RAW;
      updated = true;
      break;

    case TELEMETRY_BAMOCAR_REG_MOTOR_TEMP:
      raw16 = telemetryReadU16Le(&p_msg->data[1]);
      /*
       * Motor sensor is a KTY81/210, NOT the IGBT NTC, so the IGBT lookup table
       * does not apply. The raw Num is stored as-is for now: replace this line
       * with the KTY81/210 conversion once the NDrive motor-temp scaling is known.
       */
      telemetry.signal.bamocar_motor_temp_decic = (int16_t)raw16;
      telemetry.signal.valid_mask |= TELEMETRY_SIG_BAMOCAR_MOTOR_TEMP_DECIC;
      updated = true;
      break;

    default:
      break;
  }

  if (updated == true)
  {
    telemetry.signal.last_update_ms = p_msg->timestamp;
    telemetry.signal.dirty = true;
  }

  return updated;
}

static bool telemetryDecodeCan(const can_msg_t *p_msg)
{
  bool updated = false;

  updated |= telemetryDecodeOrion(p_msg);
  updated |= telemetryDecodeBamocar(p_msg);

  return updated;
}

/* dirty signal을 100 ms 주기로 SIG CSV에 기록한다. */
static bool telemetryWriteSignalIfNeeded(bool force)
{
  char line[TELEMETRY_MAX_LINE_LEN];
  uint32_t now_ms;
  uint32_t time_ms;
  int len;

  if ((telemetry.logging != true) || (telemetry.signal_file_open != true))
  {
    return false;
  }

  if (telemetry.signal.valid_mask == 0U)
  {
    return true;
  }

  now_ms = millis();
  if ((force != true) &&
      (telemetry.signal.dirty != true) &&
      ((now_ms - telemetry.last_signal_log_ms) < TELEMETRY_SIGNAL_PERIOD_MS))
  {
    return true;
  }

  if ((force != true) && ((now_ms - telemetry.last_signal_log_ms) < TELEMETRY_SIGNAL_PERIOD_MS))
  {
    return true;
  }

  time_ms = (telemetry.signal.last_update_ms != 0U) ? telemetry.signal.last_update_ms : now_ms;
  len = snprintf(line, sizeof(line), "%lu", (unsigned long)time_ms);
  if ((len < 0) || ((uint32_t)len >= sizeof(line)))
  {
    telemetry.write_error_count++;
    return false;
  }

  if ((telemetryAppendI32(line, sizeof(line), &len, TELEMETRY_SIG_BMS_PACK_MV, telemetry.signal.bms_pack_mv) != true) ||
      (telemetryAppendI32(line, sizeof(line), &len, TELEMETRY_SIG_BMS_PACK_MA, telemetry.signal.bms_pack_ma) != true) ||
      (telemetryAppendU32(line, sizeof(line), &len, TELEMETRY_SIG_BMS_SOC_PERMILLE, telemetry.signal.bms_soc_permille) != true) ||
      (telemetryAppendI32(line, sizeof(line), &len, TELEMETRY_SIG_BMS_MAX_TEMP_DECIC, telemetry.signal.bms_max_temp_decic) != true) ||
      (telemetryAppendHex32(line, sizeof(line), &len, TELEMETRY_SIG_BMS_STATUS, telemetry.signal.bms_status) != true) ||
      (telemetryAppendI32(line, sizeof(line), &len, TELEMETRY_SIG_BAMOCAR_RPM, telemetry.signal.bamocar_rpm) != true) ||
      (telemetryAppendI32(line, sizeof(line), &len, TELEMETRY_SIG_BAMOCAR_TORQUE_RAW, telemetry.signal.bamocar_torque_raw) != true) ||
      (telemetryAppendI32(line, sizeof(line), &len, TELEMETRY_SIG_BAMOCAR_DC_BUS_MV, telemetry.signal.bamocar_dc_bus_mv) != true) ||
      (telemetryAppendI32(line, sizeof(line), &len, TELEMETRY_SIG_BAMOCAR_MOTOR_TEMP_DECIC, telemetry.signal.bamocar_motor_temp_decic) != true) ||
      (telemetryAppendI32(line, sizeof(line), &len, TELEMETRY_SIG_BAMOCAR_IGBT_TEMP_DECIC, telemetry.signal.bamocar_igbt_temp_decic) != true) ||
      (telemetryAppendHex32(line, sizeof(line), &len, TELEMETRY_SIG_BAMOCAR_STATUS, telemetry.signal.bamocar_status) != true) ||
      (telemetryAppendHex32(line, sizeof(line), &len, TELEMETRY_SIG_BAMOCAR_ERROR_WARNING, telemetry.signal.bamocar_error_warning) != true))
  {
    return false;
  }

  if (((uint32_t)len + 3U) > sizeof(line))
  {
    telemetry.write_error_count++;
    return false;
  }

  line[len++] = '\r';
  line[len++] = '\n';
  line[len] = 0;

  if (telemetryWriteSignalString(line) != true)
  {
    return false;
  }

  telemetry.signal_lines_written++;
  telemetry.lines_since_sync++;
  telemetry.last_signal_log_ms = now_ms;
  telemetry.signal.dirty = false;

  return telemetrySyncIfNeeded(false);
}

bool telemetryInit(void)
{
  memset(&telemetry, 0, sizeof(telemetry));
  telemetry.initialized = true;
  telemetry.can_ch = _DEF_CAN1;
  telemetry.last_result = FR_OK;

#ifdef _USE_HW_CLI
  cliAdd("telemetry", cliTelemetry);
#endif

  return true;
}

bool telemetryMount(void)
{
  if (telemetry.initialized != true)
  {
    (void)telemetryInit();
  }

  if (telemetry.mounted == true)
  {
    return true;
  }

  telemetry.mounted = fatfsInit();
  telemetry.last_result = fatfsGetLastResult();

  return telemetry.mounted;
}

bool telemetryStart(uint8_t can_ch, const char *path)
{
  char raw_path[TELEMETRY_PATH_LEN];
  char signal_path[TELEMETRY_PATH_LEN];
  int written;

  if (telemetryMount() != true)
  {
    return false;
  }

  if (telemetry.logging == true)
  {
    telemetryStop();
  }

  memset(raw_path, 0, sizeof(raw_path));
  memset(signal_path, 0, sizeof(signal_path));

  if ((path != NULL) && (path[0] != 0))
  {
    written = snprintf(raw_path, sizeof(raw_path), "%s", path);
    if ((written <= 0) || ((uint32_t)written >= sizeof(raw_path)))
    {
      telemetry.last_result = FR_INVALID_NAME;
      return false;
    }

    if (telemetryMakeAutoPathWithPrefix(signal_path, sizeof(signal_path), TELEMETRY_SIGNAL_PREFIX) != true)
    {
      return false;
    }
  }
  else if (telemetryMakeAutoPaths(raw_path, sizeof(raw_path), signal_path, sizeof(signal_path)) != true)
  {
    return false;
  }

  telemetry.last_result = f_open(&telemetry.raw_file, raw_path, FA_OPEN_APPEND | FA_WRITE);
  if (telemetry.last_result != FR_OK)
  {
    return false;
  }
  telemetry.raw_file_open = true;

  telemetry.last_result = f_open(&telemetry.signal_file, signal_path, FA_OPEN_APPEND | FA_WRITE);
  if (telemetry.last_result != FR_OK)
  {
    (void)f_close(&telemetry.raw_file);
    telemetry.raw_file_open = false;
    return false;
  }
  telemetry.signal_file_open = true;

  telemetry.logging = true;
  telemetry.can_ch = can_ch;
  telemetry.last_sync_ms = millis();
  telemetry.last_signal_log_ms = telemetry.last_sync_ms;
  telemetry.lines_since_sync = 0U;
  telemetry.lines_written = 0U;
  telemetry.signal_lines_written = 0U;
  telemetry.bytes_written = 0U;
  telemetry.signal_bytes_written = 0U;
  telemetry.sync_count = 0U;
  telemetry.write_error_count = 0U;
  memset(&telemetry.signal, 0, sizeof(telemetry.signal));
  (void)snprintf(telemetry.path, sizeof(telemetry.path), "%s", raw_path);
  (void)snprintf(telemetry.signal_path, sizeof(telemetry.signal_path), "%s", signal_path);

  if ((telemetryWriteRawHeaderIfEmpty() != true) ||
      (telemetryWriteSignalHeaderIfEmpty() != true))
  {
    telemetryStop();
    return false;
  }

  return telemetrySyncIfNeeded(true);
}

void telemetryStop(void)
{
  bool had_open_file = (telemetry.raw_file_open == true) || (telemetry.signal_file_open == true);

  if (telemetry.logging == true)
  {
    (void)telemetryWriteSignalIfNeeded(true);
    (void)telemetrySyncIfNeeded(true);
  }

  if (telemetry.raw_file_open == true)
  {
    telemetry.last_result = f_close(&telemetry.raw_file);
    telemetry.raw_file_open = false;
  }

  if (telemetry.signal_file_open == true)
  {
    telemetry.last_result = f_close(&telemetry.signal_file);
    telemetry.signal_file_open = false;
  }

  if (had_open_file != true)
  {
    telemetry.last_result = FR_OK;
  }

  telemetry.logging = false;
}

/* Raw CAN frame을 LOG CSV 한 줄로 기록한다. Decode는 telemetryDecodeCan()에서 별도로 수행된다. */
static bool telemetryWriteRawCan(const can_msg_t *p_msg)
{
  char line[TELEMETRY_MAX_LINE_LEN];
  int len;
  uint8_t payload_len;

  if ((telemetry.logging != true) ||
      (telemetry.raw_file_open != true) ||
      (p_msg == NULL))
  {
    return false;
  }

  payload_len = (p_msg->length > 8U) ? 8U : (uint8_t)p_msg->length;

  len = snprintf(line,
                 sizeof(line),
                 "%lu,%s,0x%08lX,%u",
                 (unsigned long)p_msg->timestamp,
                 (p_msg->id_type == CAN_STD) ? "STD" : "EXT",
                 (unsigned long)p_msg->id,
                 (unsigned int)p_msg->length);

  if ((len < 0) || ((uint32_t)len >= sizeof(line)))
  {
    telemetry.write_error_count++;
    return false;
  }

  for (uint8_t i = 0; i < 8U; i++)
  {
    int remain = (int)sizeof(line) - len;
    int written;

    if (i < payload_len)
    {
      written = snprintf(&line[len], remain, ",0x%02X", p_msg->data[i]);
    }
    else
    {
      written = snprintf(&line[len], remain, ",");
    }

    if ((written < 0) || (written >= remain))
    {
      telemetry.write_error_count++;
      return false;
    }

    len += written;
  }

  if (((uint32_t)len + 3U) > sizeof(line))
  {
    telemetry.write_error_count++;
    return false;
  }

  line[len++] = '\r';
  line[len++] = '\n';
  line[len] = 0;

  if (telemetryWriteRawString(line) != true)
  {
    return false;
  }

  telemetry.lines_written++;
  telemetry.lines_since_sync++;

  return true;
}

/* 로깅 여부와 관계없이 decode를 수행하고, 로깅 중이면 raw/signal 파일을 갱신한다. */
bool telemetryLogCan(const can_msg_t *p_msg)
{
  bool decoded;
  bool logged = false;
  bool ret;

  if (p_msg == NULL)
  {
    return false;
  }

  decoded = telemetryDecodeCan(p_msg);
  ret = decoded;

  if (telemetry.logging == true)
  {
    logged = telemetryWriteRawCan(p_msg);
    ret = logged;

    if (logged != true)
    {
      ret = false;
    }
    if (telemetryWriteSignalIfNeeded(false) != true)
    {
      ret = false;
    }
    if (telemetrySyncIfNeeded(false) != true)
    {
      ret = false;
    }
  }

  return ret;
}

/* apMain()에서 계속 호출한다. CAN이 열려 있으면 로깅 중이 아니어도 snapshot을 갱신한다. */
void telemetryUpdate(void)
{
  can_msg_t msg;
  uint32_t available;

  if (telemetry.initialized != true)
  {
    (void)telemetryInit();
  }

  if (canIsOpen(telemetry.can_ch) != true)
  {
    if (telemetry.logging == true)
    {
      (void)telemetryWriteSignalIfNeeded(false);
      (void)telemetrySyncIfNeeded(false);
    }

    return;
  }

  available = canMsgAvailable(telemetry.can_ch);

  while (available > 0U)
  {
    if (canMsgRead(telemetry.can_ch, &msg) == true)
    {
      (void)telemetryLogCan(&msg);
    }

    available--;
  }

  if (telemetry.logging == true)
  {
    (void)telemetryWriteSignalIfNeeded(false);
    (void)telemetrySyncIfNeeded(false);
  }
}

bool telemetryIsLogging(void)
{
  return telemetry.logging;
}

void telemetryGetInfo(telemetry_info_t *p_info)
{
  if (p_info == NULL)
  {
    return;
  }

  memset(p_info, 0, sizeof(*p_info));

  if (telemetry.logging == true)
  {
    p_info->state = TELEMETRY_STATE_LOGGING;
  }
  else if (telemetry.mounted == true)
  {
    p_info->state = TELEMETRY_STATE_MOUNTED;
  }
  else if (telemetry.last_result != FR_OK)
  {
    p_info->state = TELEMETRY_STATE_ERROR;
  }
  else
  {
    p_info->state = TELEMETRY_STATE_STOPPED;
  }

  p_info->can_ch = telemetry.can_ch;
  p_info->lines_written = telemetry.lines_written;
  p_info->bytes_written = telemetry.bytes_written;
  p_info->sync_count = telemetry.sync_count;
  p_info->write_error_count = telemetry.write_error_count;
  p_info->last_result = telemetry.last_result;
  p_info->signal_lines_written = telemetry.signal_lines_written;
  p_info->signal_bytes_written = telemetry.signal_bytes_written;
  (void)snprintf(p_info->path, sizeof(p_info->path), "%s", telemetry.path);
  (void)snprintf(p_info->signal_path, sizeof(p_info->signal_path), "%s", telemetry.signal_path);
}

/* TouchGFX/화면 표시 로직이 내부 static signal을 복사할 수 있는 진입점. */
bool telemetryGetSignalSnapshot(telemetry_signal_snapshot_t *p_snapshot)
{
  if (p_snapshot == NULL)
  {
    return false;
  }

  memset(p_snapshot, 0, sizeof(*p_snapshot));

  p_snapshot->valid_mask = telemetry.signal.valid_mask;
  p_snapshot->last_update_ms = telemetry.signal.last_update_ms;
  p_snapshot->logging = telemetry.logging;
  p_snapshot->can_ch = telemetry.can_ch;

  p_snapshot->bms_pack_mv = telemetry.signal.bms_pack_mv;
  p_snapshot->bms_pack_ma = telemetry.signal.bms_pack_ma;
  p_snapshot->bms_soc_permille = telemetry.signal.bms_soc_permille;
  p_snapshot->bms_max_temp_decic = telemetry.signal.bms_max_temp_decic;
  p_snapshot->bms_status = telemetry.signal.bms_status;

  p_snapshot->bamocar_rpm = telemetry.signal.bamocar_rpm;
  p_snapshot->bamocar_torque_raw = telemetry.signal.bamocar_torque_raw;
  p_snapshot->bamocar_dc_bus_mv = telemetry.signal.bamocar_dc_bus_mv;
  p_snapshot->bamocar_motor_temp_decic = telemetry.signal.bamocar_motor_temp_decic;
  p_snapshot->bamocar_igbt_temp_decic = telemetry.signal.bamocar_igbt_temp_decic;
  p_snapshot->bamocar_status = telemetry.signal.bamocar_status;
  p_snapshot->bamocar_error_warning = telemetry.signal.bamocar_error_warning;

  return p_snapshot->valid_mask != 0U;
}

const char *telemetryResultToString(FRESULT result)
{
  switch (result)
  {
    case FR_OK: return "FR_OK";
    case FR_DISK_ERR: return "FR_DISK_ERR";
    case FR_INT_ERR: return "FR_INT_ERR";
    case FR_NOT_READY: return "FR_NOT_READY";
    case FR_NO_FILE: return "FR_NO_FILE";
    case FR_NO_PATH: return "FR_NO_PATH";
    case FR_INVALID_NAME: return "FR_INVALID_NAME";
    case FR_DENIED: return "FR_DENIED";
    case FR_EXIST: return "FR_EXIST";
    case FR_INVALID_OBJECT: return "FR_INVALID_OBJECT";
    case FR_WRITE_PROTECTED: return "FR_WRITE_PROTECTED";
    case FR_INVALID_DRIVE: return "FR_INVALID_DRIVE";
    case FR_NOT_ENABLED: return "FR_NOT_ENABLED";
    case FR_NO_FILESYSTEM: return "FR_NO_FILESYSTEM";
    case FR_MKFS_ABORTED: return "FR_MKFS_ABORTED";
    case FR_TIMEOUT: return "FR_TIMEOUT";
    case FR_LOCKED: return "FR_LOCKED";
    case FR_NOT_ENOUGH_CORE: return "FR_NOT_ENOUGH_CORE";
    case FR_TOO_MANY_OPEN_FILES: return "FR_TOO_MANY_OPEN_FILES";
    case FR_INVALID_PARAMETER: return "FR_INVALID_PARAMETER";
    default: return "FR_UNKNOWN";
  }
}

#ifdef _USE_HW_CLI
static void cliTelemetry(cli_args_t *args)
{
  bool ret = false;

  if ((args->argc == 1) && args->isStr(0, "mount"))
  {
    cliPrintf("telemetry mount : %s (%s)\n",
              telemetryMount() ? "OK" : "FAIL",
              telemetryResultToString(telemetry.last_result));
    ret = true;
  }

  if ((args->argc >= 1) && args->isStr(0, "start"))
  {
    const char *path = NULL;
    uint8_t can_ch = _DEF_CAN1;

    if (args->argc >= 2)
    {
      path = args->getStr(1);
    }

    if (canIsOpen(can_ch) != true)
    {
      (void)canOpen(can_ch, CAN_NORMAL, CAN_CLASSIC, CAN_500K, CAN_500K);
    }

    cliPrintf("telemetry start : %s (%s)\n",
              telemetryStart(can_ch, path) ? "OK" : "FAIL",
              telemetryResultToString(telemetry.last_result));
    if (telemetry.path[0] != 0)
    {
      cliPrintf("raw_path   : %s\n", telemetry.path);
      cliPrintf("sig_path   : %s\n", telemetry.signal_path);
    }
    ret = true;
  }

  if ((args->argc == 1) && args->isStr(0, "stop"))
  {
    telemetryStop();
    cliPrintf("telemetry stop  : %s\n", telemetryResultToString(telemetry.last_result));
    ret = true;
  }

  if ((args->argc == 1) && args->isStr(0, "info"))
  {
    telemetry_info_t info;

    telemetryGetInfo(&info);
    cliPrintf("state      : %d\n", info.state);
    cliPrintf("can_ch     : %d\n", info.can_ch);
    cliPrintf("raw_path   : %s\n", info.path);
    cliPrintf("sig_path   : %s\n", info.signal_path);
    cliPrintf("raw_lines  : %lu\n", (unsigned long)info.lines_written);
    cliPrintf("raw_bytes  : %lu\n", (unsigned long)info.bytes_written);
    cliPrintf("sig_lines  : %lu\n", (unsigned long)info.signal_lines_written);
    cliPrintf("sig_bytes  : %lu\n", (unsigned long)info.signal_bytes_written);
    cliPrintf("sync       : %lu\n", (unsigned long)info.sync_count);
    cliPrintf("wr_err     : %lu\n", (unsigned long)info.write_error_count);
    cliPrintf("fatfs      : %s\n", telemetryResultToString(info.last_result));
    ret = true;
  }

  if ((args->argc == 1) && args->isStr(0, "signal"))
  {
    telemetry_signal_snapshot_t signal;
    bool valid;

    valid = telemetryGetSignalSnapshot(&signal);

    cliPrintf("valid      : %d\n", valid);
    cliPrintf("valid_mask : 0x%08lX\n", (unsigned long)signal.valid_mask);
    cliPrintf("time_ms    : %lu\n", (unsigned long)signal.last_update_ms);
    cliPrintf("logging    : %d\n", signal.logging);
    cliPrintf("can_ch     : %d\n", signal.can_ch);
    cliPrintf("bms volt   : %ld mV\n", (long)signal.bms_pack_mv);
    cliPrintf("bms curr   : %ld mA\n", (long)signal.bms_pack_ma);
    cliPrintf("bms soc    : %u.%u %%\n",
              (unsigned int)(signal.bms_soc_permille / 10U),
              (unsigned int)(signal.bms_soc_permille % 10U));
    cliPrintf("bms temp   : %d.%d C\n",
              (int)(signal.bms_max_temp_decic / 10),
              (int)abs(signal.bms_max_temp_decic % 10));
    cliPrintf("bam rpm    : %ld rpm\n", (long)signal.bamocar_rpm);
    cliPrintf("bam torque : %ld raw\n", (long)signal.bamocar_torque_raw);
    cliPrintf("bam dc bus : %ld mV\n", (long)signal.bamocar_dc_bus_mv);
    cliPrintf("bam m temp : %d.%d C\n",
              (int)(signal.bamocar_motor_temp_decic / 10),
              (int)abs(signal.bamocar_motor_temp_decic % 10));
    cliPrintf("bam i temp : %d.%d C\n",
              (int)(signal.bamocar_igbt_temp_decic / 10),
              (int)abs(signal.bamocar_igbt_temp_decic % 10));
    cliPrintf("bam status : 0x%08lX\n", (unsigned long)signal.bamocar_status);
    cliPrintf("bam error  : 0x%08lX\n", (unsigned long)signal.bamocar_error_warning);

    ret = true;
  }

  if (ret != true)
  {
    cliPrintf("telemetry mount\n");
    cliPrintf("telemetry start [raw_path]\n");
    cliPrintf("telemetry stop\n");
    cliPrintf("telemetry info\n");
    cliPrintf("telemetry signal\n");
  }
}
#endif

#endif
