/*
 * data_logger.c
 */

#include "data_logger.h"

#ifdef _USE_HW_FATFS

#include "APPS/bamocar_can.h"
#include "cli.h"
#include "fatfs.h"

/*
 * Data logger 흐름
 * - CAN RX queue에서 메시지를 꺼내 Orion/Bamocar 신호로 해석한다.
 * - 로깅 OFF 상태에서도 해석값 snapshot은 계속 갱신해서 LCD/TouchGFX가 사용할 수 있다.
 * - 로깅 ON 상태에서는 raw CAN CSV(LOGxxx.CSV)와 해석값 CSV(SIGxxx.CSV)를 함께 기록한다.
 */

#define DATA_LOGGER_SYNC_PERIOD_MS              1000U
#define DATA_LOGGER_SYNC_PERIOD_LINES           64U
#define DATA_LOGGER_SIGNAL_PERIOD_MS            100U
#define DATA_LOGGER_MAX_LINE_LEN                256U
#define DATA_LOGGER_MAX_FILES                   1000U

#define DATA_LOGGER_RAW_PREFIX                  "LOG"
#define DATA_LOGGER_SIGNAL_PREFIX               "SIG"

#define DATA_LOGGER_BAMOCAR_REG_ERROR_WARNING   0x8FU
#define DATA_LOGGER_BAMOCAR_REG_IGBT_TEMP       0x4AU
#define DATA_LOGGER_BAMOCAR_REG_DC_BUS          0xEBU

/* BAMOCAR 700 V model: manual page 49 says 1 V ~= 31.5 Num for DC bus. */
#define DATA_LOGGER_BAMOCAR_DC_BUS_NUM_PER_10V  315L

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
#define DATA_LOGGER_BAMOCAR_REG_N_ACTUAL        0x30U   /* speed actual: +/-32767 Num = +/- N_max */
#define DATA_LOGGER_BAMOCAR_REG_TORQUE_ACTUAL   0xA8U   /* torque/current actual: raw Num logged as-is */
#define DATA_LOGGER_BAMOCAR_REG_MOTOR_TEMP      0x49U   /* motor temperature (EMRAX KTY81/210) */
#define DATA_LOGGER_BAMOCAR_N_MAX_RPM           6500L   /* EMRAX 228 limiting speed = NDrive N_max */

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
#define DATA_LOGGER_ORION_CAN_ID                0x6B0U
#define DATA_LOGGER_ORION_VOLT_MV_PER_BIT       100L    /* 0.1 V  -> 100 mV          */
#define DATA_LOGGER_ORION_CURR_MA_PER_BIT       100L    /* 0.1 A  -> 100 mA          */
#define DATA_LOGGER_ORION_SOC_PERMILLE_PER_BIT  5L      /* 0.5 %  -> 5 per-mille     */
#define DATA_LOGGER_ORION_TEMP_DECIC_PER_BIT    10L     /* 1 degC -> 10 deci-Celsius */

#define DATA_LOGGER_SIG_BMS_PACK_MV              DATA_LOGGER_SIGNAL_BMS_PACK_MV
#define DATA_LOGGER_SIG_BMS_PACK_MA              DATA_LOGGER_SIGNAL_BMS_PACK_MA
#define DATA_LOGGER_SIG_BMS_SOC_PERMILLE         DATA_LOGGER_SIGNAL_BMS_SOC_PERMILLE
#define DATA_LOGGER_SIG_BMS_MAX_TEMP_DECIC       DATA_LOGGER_SIGNAL_BMS_MAX_TEMP_DECIC
#define DATA_LOGGER_SIG_BMS_STATUS               DATA_LOGGER_SIGNAL_BMS_STATUS
#define DATA_LOGGER_SIG_BAMOCAR_RPM              DATA_LOGGER_SIGNAL_BAMOCAR_RPM
#define DATA_LOGGER_SIG_BAMOCAR_TORQUE_RAW       DATA_LOGGER_SIGNAL_BAMOCAR_TORQUE_RAW
#define DATA_LOGGER_SIG_BAMOCAR_DC_BUS_MV        DATA_LOGGER_SIGNAL_BAMOCAR_DC_BUS_MV
#define DATA_LOGGER_SIG_BAMOCAR_MOTOR_TEMP_DECIC DATA_LOGGER_SIGNAL_BAMOCAR_MOTOR_TEMP_DECIC
#define DATA_LOGGER_SIG_BAMOCAR_IGBT_TEMP_DECIC  DATA_LOGGER_SIGNAL_BAMOCAR_IGBT_TEMP_DECIC
#define DATA_LOGGER_SIG_BAMOCAR_STATUS           DATA_LOGGER_SIGNAL_BAMOCAR_STATUS
#define DATA_LOGGER_SIG_BAMOCAR_ERROR_WARNING    DATA_LOGGER_SIGNAL_BAMOCAR_ERROR_WARNING

typedef struct
{
  uint16_t raw;
  int16_t  temp_decic;
} data_logger_temp_point_t;

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
} data_logger_signal_t;

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
  char     path[DATA_LOGGER_PATH_LEN];
  char     signal_path[DATA_LOGGER_PATH_LEN];
  data_logger_signal_t signal;
} data_logger_t;

static data_logger_t data_logger;

static const data_logger_temp_point_t bamocar_igbt_temp_table[] =
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
static void cliDataLogger(cli_args_t *args);
#endif

static uint16_t dataLoggerReadU16Le(const uint8_t *p_data)
{
  return (uint16_t)p_data[0] | ((uint16_t)p_data[1] << 8);
}

static uint32_t dataLoggerReadU32Le(const uint8_t *p_data)
{
  return (uint32_t)p_data[0] |
         ((uint32_t)p_data[1] << 8) |
         ((uint32_t)p_data[2] << 16) |
         ((uint32_t)p_data[3] << 24);
}

static uint16_t dataLoggerReadU16Be(const uint8_t *p_data)
{
  return ((uint16_t)p_data[0] << 8) | (uint16_t)p_data[1];
}

static int16_t dataLoggerBamocarTempRawToDeciC(uint16_t raw)
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

static bool dataLoggerAppendI32(char *line, uint32_t line_len, int *p_len, uint32_t valid_mask, int32_t value)
{
  int remain = (int)line_len - *p_len;
  int written;

  if ((data_logger.signal.valid_mask & valid_mask) != 0U)
  {
    written = snprintf(&line[*p_len], remain, ",%ld", (long)value);
  }
  else
  {
    written = snprintf(&line[*p_len], remain, ",");
  }

  if ((written < 0) || (written >= remain))
  {
    data_logger.write_error_count++;
    return false;
  }

  *p_len += written;
  return true;
}

static bool dataLoggerAppendU32(char *line, uint32_t line_len, int *p_len, uint32_t valid_mask, uint32_t value)
{
  int remain = (int)line_len - *p_len;
  int written;

  if ((data_logger.signal.valid_mask & valid_mask) != 0U)
  {
    written = snprintf(&line[*p_len], remain, ",%lu", (unsigned long)value);
  }
  else
  {
    written = snprintf(&line[*p_len], remain, ",");
  }

  if ((written < 0) || (written >= remain))
  {
    data_logger.write_error_count++;
    return false;
  }

  *p_len += written;
  return true;
}

static bool dataLoggerAppendHex32(char *line, uint32_t line_len, int *p_len, uint32_t valid_mask, uint32_t value)
{
  int remain = (int)line_len - *p_len;
  int written;

  if ((data_logger.signal.valid_mask & valid_mask) != 0U)
  {
    written = snprintf(&line[*p_len], remain, ",0x%08lX", (unsigned long)value);
  }
  else
  {
    written = snprintf(&line[*p_len], remain, ",");
  }

  if ((written < 0) || (written >= remain))
  {
    data_logger.write_error_count++;
    return false;
  }

  *p_len += written;
  return true;
}

/* FatFs write 실패 시 로깅을 멈춰서 이후 데이터가 깨진 상태로 계속 기록되지 않게 한다. */
static bool dataLoggerWriteFileString(FIL *p_file, const char *str, uint32_t *p_bytes_written)
{
  UINT written = 0;
  UINT len;

  if ((data_logger.logging != true) || (p_file == NULL) || (str == NULL))
  {
    return false;
  }

  len = (UINT)strlen(str);
  data_logger.last_result = f_write(p_file, str, len, &written);

  if ((data_logger.last_result != FR_OK) || (written != len))
  {
    data_logger.write_error_count++;
    data_logger.logging = false;
    data_logger.last_result = (data_logger.last_result == FR_OK) ? FR_DISK_ERR : data_logger.last_result;
    return false;
  }

  if (p_bytes_written != NULL)
  {
    *p_bytes_written += written;
  }

  return true;
}

static bool dataLoggerWriteRawString(const char *str)
{
  return dataLoggerWriteFileString(&data_logger.raw_file, str, &data_logger.bytes_written);
}

static bool dataLoggerWriteSignalString(const char *str)
{
  return dataLoggerWriteFileString(&data_logger.signal_file, str, &data_logger.signal_bytes_written);
}

/* 주기/라인 수 기준으로 f_sync()를 호출해 전원 차단 시 손실 구간을 줄인다. */
static bool dataLoggerSyncIfNeeded(bool force)
{
  uint32_t now_ms;

  if (data_logger.logging != true)
  {
    return false;
  }

  now_ms = millis();

  if ((force == true) ||
      (data_logger.lines_since_sync >= DATA_LOGGER_SYNC_PERIOD_LINES) ||
      ((now_ms - data_logger.last_sync_ms) >= DATA_LOGGER_SYNC_PERIOD_MS))
  {
    if (data_logger.raw_file_open == true)
    {
      data_logger.last_result = f_sync(&data_logger.raw_file);
      if (data_logger.last_result != FR_OK)
      {
        data_logger.write_error_count++;
        data_logger.logging = false;
        return false;
      }
    }

    if (data_logger.signal_file_open == true)
    {
      data_logger.last_result = f_sync(&data_logger.signal_file);
      if (data_logger.last_result != FR_OK)
      {
        data_logger.write_error_count++;
        data_logger.logging = false;
        return false;
      }
    }

    data_logger.lines_since_sync = 0U;
    data_logger.last_sync_ms = now_ms;
    data_logger.sync_count++;
  }

  return true;
}

static bool dataLoggerMakePath(char *path, uint32_t path_len, const char *prefix, uint32_t index)
{
  const char *drive_path = (SDPath[0] != 0) ? SDPath : "0:/";
  int written;

  written = snprintf(path, path_len, "%s%s%03lu.CSV", drive_path, prefix, (unsigned long)index);
  return (written > 0) && ((uint32_t)written < path_len);
}

static bool dataLoggerMakeAutoPathWithPrefix(char *path, uint32_t path_len, const char *prefix)
{
  FILINFO info;

  if ((path == NULL) || (path_len < sizeof("0:/LOG000.CSV")))
  {
    return false;
  }

  for (uint32_t i = 0; i < DATA_LOGGER_MAX_FILES; i++)
  {
    if (dataLoggerMakePath(path, path_len, prefix, i) != true)
    {
      data_logger.last_result = FR_INVALID_NAME;
      return false;
    }

    data_logger.last_result = f_stat(path, &info);
    if (data_logger.last_result == FR_NO_FILE)
    {
      data_logger.last_result = FR_OK;
      return true;
    }
    if (data_logger.last_result != FR_OK)
    {
      return false;
    }
  }

  data_logger.last_result = FR_DENIED;
  return false;
}

static bool dataLoggerMakeAutoPaths(char *raw_path, uint32_t raw_path_len,
                                    char *signal_path, uint32_t signal_path_len)
{
  FILINFO info;

  for (uint32_t i = 0; i < DATA_LOGGER_MAX_FILES; i++)
  {
    if ((dataLoggerMakePath(raw_path, raw_path_len, DATA_LOGGER_RAW_PREFIX, i) != true) ||
        (dataLoggerMakePath(signal_path, signal_path_len, DATA_LOGGER_SIGNAL_PREFIX, i) != true))
    {
      data_logger.last_result = FR_INVALID_NAME;
      return false;
    }

    data_logger.last_result = f_stat(raw_path, &info);
    if (data_logger.last_result == FR_OK)
    {
      continue;
    }
    if (data_logger.last_result != FR_NO_FILE)
    {
      return false;
    }

    data_logger.last_result = f_stat(signal_path, &info);
    if (data_logger.last_result == FR_NO_FILE)
    {
      data_logger.last_result = FR_OK;
      return true;
    }
    if (data_logger.last_result != FR_OK)
    {
      return false;
    }
  }

  data_logger.last_result = FR_DENIED;
  return false;
}

static bool dataLoggerWriteRawHeaderIfEmpty(void)
{
  if (f_size(&data_logger.raw_file) != 0U)
  {
    return true;
  }

  return dataLoggerWriteRawString("time_ms,id_type,id,dlc,b0,b1,b2,b3,b4,b5,b6,b7\r\n");
}

static bool dataLoggerWriteSignalHeaderIfEmpty(void)
{
  if (f_size(&data_logger.signal_file) != 0U)
  {
    return true;
  }

  return dataLoggerWriteSignalString(
      "time_ms,bms_pack_mv,bms_pack_ma,bms_soc_permille,bms_max_temp_decic,bms_status,"
      "bamocar_rpm,bamocar_torque_raw,bamocar_dc_bus_mv,bamocar_motor_temp_decic,"
      "bamocar_igbt_temp_decic,bamocar_status,bamocar_error_warning\r\n");
}

/* Orion BMS 사용자 CAN layout을 현재 signal 값으로 변환한다. */
static bool dataLoggerDecodeOrion(const can_msg_t *p_msg)
{
  uint16_t raw;

  if ((p_msg->id_type != CAN_STD) ||
      (p_msg->id != DATA_LOGGER_ORION_CAN_ID) ||
      (p_msg->length < 8U))
  {
    return false;
  }

  raw = dataLoggerReadU16Be(&p_msg->data[0]);
  data_logger.signal.bms_pack_mv = (int32_t)raw * DATA_LOGGER_ORION_VOLT_MV_PER_BIT;
  data_logger.signal.valid_mask |= DATA_LOGGER_SIG_BMS_PACK_MV;

  raw = dataLoggerReadU16Be(&p_msg->data[2]);
  data_logger.signal.bms_pack_ma = (int32_t)(int16_t)raw * DATA_LOGGER_ORION_CURR_MA_PER_BIT;
  data_logger.signal.valid_mask |= DATA_LOGGER_SIG_BMS_PACK_MA;

  raw = dataLoggerReadU16Be(&p_msg->data[4]);
  data_logger.signal.bms_soc_permille =
      (uint16_t)((int32_t)raw * DATA_LOGGER_ORION_SOC_PERMILLE_PER_BIT);
  data_logger.signal.valid_mask |= DATA_LOGGER_SIG_BMS_SOC_PERMILLE;

  raw = dataLoggerReadU16Be(&p_msg->data[6]);
  data_logger.signal.bms_max_temp_decic =
      (int16_t)((int32_t)(int16_t)raw * DATA_LOGGER_ORION_TEMP_DECIC_PER_BIT);
  data_logger.signal.valid_mask |= DATA_LOGGER_SIG_BMS_MAX_TEMP_DECIC;

  data_logger.signal.last_update_ms = p_msg->timestamp;
  data_logger.signal.dirty = true;

  return true;
}

/* Bamocar register response frame을 현재 signal 값으로 변환한다. */
static bool dataLoggerDecodeBamocar(const can_msg_t *p_msg)
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
      raw16 = dataLoggerReadU16Le(&p_msg->data[1]);
      data_logger.signal.bamocar_status = raw16;
      data_logger.signal.valid_mask |= DATA_LOGGER_SIG_BAMOCAR_STATUS;
      updated = true;
      break;

    case DATA_LOGGER_BAMOCAR_REG_ERROR_WARNING:
      if (p_msg->length >= 5U)
      {
        data_logger.signal.bamocar_error_warning = dataLoggerReadU32Le(&p_msg->data[1]);
        data_logger.signal.valid_mask |= DATA_LOGGER_SIG_BAMOCAR_ERROR_WARNING;
        updated = true;
      }
      break;

    case DATA_LOGGER_BAMOCAR_REG_DC_BUS:
      raw16 = dataLoggerReadU16Le(&p_msg->data[1]);
      data_logger.signal.bamocar_dc_bus_mv =
          (int32_t)(((int32_t)raw16 * 10000L) / DATA_LOGGER_BAMOCAR_DC_BUS_NUM_PER_10V);
      data_logger.signal.valid_mask |= DATA_LOGGER_SIG_BAMOCAR_DC_BUS_MV;
      updated = true;
      break;

    case DATA_LOGGER_BAMOCAR_REG_IGBT_TEMP:
      raw16 = dataLoggerReadU16Le(&p_msg->data[1]);
      data_logger.signal.bamocar_igbt_temp_decic = dataLoggerBamocarTempRawToDeciC(raw16);
      data_logger.signal.valid_mask |= DATA_LOGGER_SIG_BAMOCAR_IGBT_TEMP_DECIC;
      updated = true;
      break;

    case DATA_LOGGER_BAMOCAR_REG_N_ACTUAL:
      raw16 = dataLoggerReadU16Le(&p_msg->data[1]);
      data_logger.signal.bamocar_rpm =
          ((int32_t)(int16_t)raw16 * DATA_LOGGER_BAMOCAR_N_MAX_RPM) / 32767L;
      data_logger.signal.valid_mask |= DATA_LOGGER_SIG_BAMOCAR_RPM;
      updated = true;
      break;

    case DATA_LOGGER_BAMOCAR_REG_TORQUE_ACTUAL:
      raw16 = dataLoggerReadU16Le(&p_msg->data[1]);
      data_logger.signal.bamocar_torque_raw = (int32_t)(int16_t)raw16;
      data_logger.signal.valid_mask |= DATA_LOGGER_SIG_BAMOCAR_TORQUE_RAW;
      updated = true;
      break;

    case DATA_LOGGER_BAMOCAR_REG_MOTOR_TEMP:
      raw16 = dataLoggerReadU16Le(&p_msg->data[1]);
      /*
       * Motor sensor is a KTY81/210, NOT the IGBT NTC, so the IGBT lookup table
       * does not apply. The raw Num is stored as-is for now: replace this line
       * with the KTY81/210 conversion once the NDrive motor-temp scaling is known.
       */
      data_logger.signal.bamocar_motor_temp_decic = (int16_t)raw16;
      data_logger.signal.valid_mask |= DATA_LOGGER_SIG_BAMOCAR_MOTOR_TEMP_DECIC;
      updated = true;
      break;

    default:
      break;
  }

  if (updated == true)
  {
    data_logger.signal.last_update_ms = p_msg->timestamp;
    data_logger.signal.dirty = true;
  }

  return updated;
}

static bool dataLoggerDecodeCan(const can_msg_t *p_msg)
{
  bool updated = false;

  updated |= dataLoggerDecodeOrion(p_msg);
  updated |= dataLoggerDecodeBamocar(p_msg);

  return updated;
}

/* dirty signal을 100ms 주기로 SIG CSV에 기록한다. */
static bool dataLoggerWriteSignalIfNeeded(bool force)
{
  char line[DATA_LOGGER_MAX_LINE_LEN];
  uint32_t now_ms;
  uint32_t time_ms;
  int len;

  if ((data_logger.logging != true) || (data_logger.signal_file_open != true))
  {
    return false;
  }

  if (data_logger.signal.valid_mask == 0U)
  {
    return true;
  }

  now_ms = millis();
  if ((force != true) &&
      (data_logger.signal.dirty != true) &&
      ((now_ms - data_logger.last_signal_log_ms) < DATA_LOGGER_SIGNAL_PERIOD_MS))
  {
    return true;
  }

  if ((force != true) && ((now_ms - data_logger.last_signal_log_ms) < DATA_LOGGER_SIGNAL_PERIOD_MS))
  {
    return true;
  }

  time_ms = (data_logger.signal.last_update_ms != 0U) ? data_logger.signal.last_update_ms : now_ms;
  len = snprintf(line, sizeof(line), "%lu", (unsigned long)time_ms);
  if ((len < 0) || ((uint32_t)len >= sizeof(line)))
  {
    data_logger.write_error_count++;
    return false;
  }

  if ((dataLoggerAppendI32(line, sizeof(line), &len, DATA_LOGGER_SIG_BMS_PACK_MV, data_logger.signal.bms_pack_mv) != true) ||
      (dataLoggerAppendI32(line, sizeof(line), &len, DATA_LOGGER_SIG_BMS_PACK_MA, data_logger.signal.bms_pack_ma) != true) ||
      (dataLoggerAppendU32(line, sizeof(line), &len, DATA_LOGGER_SIG_BMS_SOC_PERMILLE, data_logger.signal.bms_soc_permille) != true) ||
      (dataLoggerAppendI32(line, sizeof(line), &len, DATA_LOGGER_SIG_BMS_MAX_TEMP_DECIC, data_logger.signal.bms_max_temp_decic) != true) ||
      (dataLoggerAppendHex32(line, sizeof(line), &len, DATA_LOGGER_SIG_BMS_STATUS, data_logger.signal.bms_status) != true) ||
      (dataLoggerAppendI32(line, sizeof(line), &len, DATA_LOGGER_SIG_BAMOCAR_RPM, data_logger.signal.bamocar_rpm) != true) ||
      (dataLoggerAppendI32(line, sizeof(line), &len, DATA_LOGGER_SIG_BAMOCAR_TORQUE_RAW, data_logger.signal.bamocar_torque_raw) != true) ||
      (dataLoggerAppendI32(line, sizeof(line), &len, DATA_LOGGER_SIG_BAMOCAR_DC_BUS_MV, data_logger.signal.bamocar_dc_bus_mv) != true) ||
      (dataLoggerAppendI32(line, sizeof(line), &len, DATA_LOGGER_SIG_BAMOCAR_MOTOR_TEMP_DECIC, data_logger.signal.bamocar_motor_temp_decic) != true) ||
      (dataLoggerAppendI32(line, sizeof(line), &len, DATA_LOGGER_SIG_BAMOCAR_IGBT_TEMP_DECIC, data_logger.signal.bamocar_igbt_temp_decic) != true) ||
      (dataLoggerAppendHex32(line, sizeof(line), &len, DATA_LOGGER_SIG_BAMOCAR_STATUS, data_logger.signal.bamocar_status) != true) ||
      (dataLoggerAppendHex32(line, sizeof(line), &len, DATA_LOGGER_SIG_BAMOCAR_ERROR_WARNING, data_logger.signal.bamocar_error_warning) != true))
  {
    return false;
  }

  if (((uint32_t)len + 3U) > sizeof(line))
  {
    data_logger.write_error_count++;
    return false;
  }

  line[len++] = '\r';
  line[len++] = '\n';
  line[len] = 0;

  if (dataLoggerWriteSignalString(line) != true)
  {
    return false;
  }

  data_logger.signal_lines_written++;
  data_logger.lines_since_sync++;
  data_logger.last_signal_log_ms = now_ms;
  data_logger.signal.dirty = false;

  return dataLoggerSyncIfNeeded(false);
}

bool dataLoggerInit(void)
{
  memset(&data_logger, 0, sizeof(data_logger));
  data_logger.initialized = true;
  data_logger.can_ch = _DEF_CAN1;
  data_logger.last_result = FR_OK;

#ifdef _USE_HW_CLI
  cliAdd("dlog", cliDataLogger);
#endif

  return true;
}

bool dataLoggerMount(void)
{
  if (data_logger.initialized != true)
  {
    (void)dataLoggerInit();
  }

  if (data_logger.mounted == true)
  {
    return true;
  }

  data_logger.mounted = fatfsInit();
  data_logger.last_result = fatfsGetLastResult();

  return data_logger.mounted;
}

bool dataLoggerStart(uint8_t can_ch, const char *path)
{
  char raw_path[DATA_LOGGER_PATH_LEN];
  char signal_path[DATA_LOGGER_PATH_LEN];
  int written;

  if (dataLoggerMount() != true)
  {
    return false;
  }

  if (data_logger.logging == true)
  {
    dataLoggerStop();
  }

  memset(raw_path, 0, sizeof(raw_path));
  memset(signal_path, 0, sizeof(signal_path));

  if ((path != NULL) && (path[0] != 0))
  {
    written = snprintf(raw_path, sizeof(raw_path), "%s", path);
    if ((written <= 0) || ((uint32_t)written >= sizeof(raw_path)))
    {
      data_logger.last_result = FR_INVALID_NAME;
      return false;
    }

    if (dataLoggerMakeAutoPathWithPrefix(signal_path, sizeof(signal_path), DATA_LOGGER_SIGNAL_PREFIX) != true)
    {
      return false;
    }
  }
  else if (dataLoggerMakeAutoPaths(raw_path, sizeof(raw_path), signal_path, sizeof(signal_path)) != true)
  {
    return false;
  }

  data_logger.last_result = f_open(&data_logger.raw_file, raw_path, FA_OPEN_APPEND | FA_WRITE);
  if (data_logger.last_result != FR_OK)
  {
    return false;
  }
  data_logger.raw_file_open = true;

  data_logger.last_result = f_open(&data_logger.signal_file, signal_path, FA_OPEN_APPEND | FA_WRITE);
  if (data_logger.last_result != FR_OK)
  {
    (void)f_close(&data_logger.raw_file);
    data_logger.raw_file_open = false;
    return false;
  }
  data_logger.signal_file_open = true;

  data_logger.logging = true;
  data_logger.can_ch = can_ch;
  data_logger.last_sync_ms = millis();
  data_logger.last_signal_log_ms = data_logger.last_sync_ms;
  data_logger.lines_since_sync = 0U;
  data_logger.lines_written = 0U;
  data_logger.signal_lines_written = 0U;
  data_logger.bytes_written = 0U;
  data_logger.signal_bytes_written = 0U;
  data_logger.sync_count = 0U;
  data_logger.write_error_count = 0U;
  memset(&data_logger.signal, 0, sizeof(data_logger.signal));
  (void)snprintf(data_logger.path, sizeof(data_logger.path), "%s", raw_path);
  (void)snprintf(data_logger.signal_path, sizeof(data_logger.signal_path), "%s", signal_path);

  if ((dataLoggerWriteRawHeaderIfEmpty() != true) ||
      (dataLoggerWriteSignalHeaderIfEmpty() != true))
  {
    dataLoggerStop();
    return false;
  }

  return dataLoggerSyncIfNeeded(true);
}

void dataLoggerStop(void)
{
  bool had_open_file = (data_logger.raw_file_open == true) || (data_logger.signal_file_open == true);

  if (data_logger.logging == true)
  {
    (void)dataLoggerWriteSignalIfNeeded(true);
    (void)dataLoggerSyncIfNeeded(true);
  }

  if (data_logger.raw_file_open == true)
  {
    data_logger.last_result = f_close(&data_logger.raw_file);
    data_logger.raw_file_open = false;
  }

  if (data_logger.signal_file_open == true)
  {
    data_logger.last_result = f_close(&data_logger.signal_file);
    data_logger.signal_file_open = false;
  }

  if (had_open_file != true)
  {
    data_logger.last_result = FR_OK;
  }

  data_logger.logging = false;
}

/* Raw CAN frame을 LOG CSV 한 줄로 기록한다. Decode는 dataLoggerDecodeCan()에서 별도로 수행된다. */
static bool dataLoggerWriteRawCan(const can_msg_t *p_msg)
{
  char line[DATA_LOGGER_MAX_LINE_LEN];
  int len;
  uint8_t payload_len;

  if ((data_logger.logging != true) ||
      (data_logger.raw_file_open != true) ||
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
    data_logger.write_error_count++;
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
      data_logger.write_error_count++;
      return false;
    }

    len += written;
  }

  if (((uint32_t)len + 3U) > sizeof(line))
  {
    data_logger.write_error_count++;
    return false;
  }

  line[len++] = '\r';
  line[len++] = '\n';
  line[len] = 0;

  if (dataLoggerWriteRawString(line) != true)
  {
    return false;
  }

  data_logger.lines_written++;
  data_logger.lines_since_sync++;

  return true;
}

/* 로깅 여부와 관계없이 decode를 수행하고, 로깅 중이면 raw/signal 파일도 갱신한다. */
bool dataLoggerLogCan(const can_msg_t *p_msg)
{
  bool decoded;
  bool logged = false;
  bool ret;

  if (p_msg == NULL)
  {
    return false;
  }

  decoded = dataLoggerDecodeCan(p_msg);
  ret = decoded;

  if (data_logger.logging == true)
  {
    logged = dataLoggerWriteRawCan(p_msg);
    ret = logged;

    if (logged != true)
    {
      ret = false;
    }
    if (dataLoggerWriteSignalIfNeeded(false) != true)
    {
      ret = false;
    }
    if (dataLoggerSyncIfNeeded(false) != true)
    {
      ret = false;
    }
  }

  return ret;
}

/* apMain()에서 계속 호출된다. CAN이 열려 있으면 로깅 중이 아니어도 snapshot을 갱신한다. */
void dataLoggerUpdate(void)
{
  can_msg_t msg;
  uint32_t available;

  if (data_logger.initialized != true)
  {
    (void)dataLoggerInit();
  }

  if (canIsOpen(data_logger.can_ch) != true)
  {
    if (data_logger.logging == true)
    {
      (void)dataLoggerWriteSignalIfNeeded(false);
      (void)dataLoggerSyncIfNeeded(false);
    }

    return;
  }

  available = canMsgAvailable(data_logger.can_ch);

  while (available > 0U)
  {
    if (canMsgRead(data_logger.can_ch, &msg) == true)
    {
      (void)dataLoggerLogCan(&msg);
    }

    available--;
  }

  if (data_logger.logging == true)
  {
    (void)dataLoggerWriteSignalIfNeeded(false);
    (void)dataLoggerSyncIfNeeded(false);
  }
}

bool dataLoggerIsLogging(void)
{
  return data_logger.logging;
}

void dataLoggerGetInfo(data_logger_info_t *p_info)
{
  if (p_info == NULL)
  {
    return;
  }

  memset(p_info, 0, sizeof(*p_info));

  if (data_logger.logging == true)
  {
    p_info->state = DATA_LOGGER_STATE_LOGGING;
  }
  else if (data_logger.mounted == true)
  {
    p_info->state = DATA_LOGGER_STATE_MOUNTED;
  }
  else if (data_logger.last_result != FR_OK)
  {
    p_info->state = DATA_LOGGER_STATE_ERROR;
  }
  else
  {
    p_info->state = DATA_LOGGER_STATE_STOPPED;
  }

  p_info->can_ch = data_logger.can_ch;
  p_info->lines_written = data_logger.lines_written;
  p_info->bytes_written = data_logger.bytes_written;
  p_info->sync_count = data_logger.sync_count;
  p_info->write_error_count = data_logger.write_error_count;
  p_info->last_result = data_logger.last_result;
  p_info->signal_lines_written = data_logger.signal_lines_written;
  p_info->signal_bytes_written = data_logger.signal_bytes_written;
  (void)snprintf(p_info->path, sizeof(p_info->path), "%s", data_logger.path);
  (void)snprintf(p_info->signal_path, sizeof(p_info->signal_path), "%s", data_logger.signal_path);
}

/* TouchGFX/외부 표시 로직이 내부 static signal을 복사해 가는 진입점. */
bool dataLoggerGetSignalSnapshot(data_logger_signal_snapshot_t *p_snapshot)
{
  if (p_snapshot == NULL)
  {
    return false;
  }

  memset(p_snapshot, 0, sizeof(*p_snapshot));

  p_snapshot->valid_mask = data_logger.signal.valid_mask;
  p_snapshot->last_update_ms = data_logger.signal.last_update_ms;
  p_snapshot->logging = data_logger.logging;
  p_snapshot->can_ch = data_logger.can_ch;

  p_snapshot->bms_pack_mv = data_logger.signal.bms_pack_mv;
  p_snapshot->bms_pack_ma = data_logger.signal.bms_pack_ma;
  p_snapshot->bms_soc_permille = data_logger.signal.bms_soc_permille;
  p_snapshot->bms_max_temp_decic = data_logger.signal.bms_max_temp_decic;
  p_snapshot->bms_status = data_logger.signal.bms_status;

  p_snapshot->bamocar_rpm = data_logger.signal.bamocar_rpm;
  p_snapshot->bamocar_torque_raw = data_logger.signal.bamocar_torque_raw;
  p_snapshot->bamocar_dc_bus_mv = data_logger.signal.bamocar_dc_bus_mv;
  p_snapshot->bamocar_motor_temp_decic = data_logger.signal.bamocar_motor_temp_decic;
  p_snapshot->bamocar_igbt_temp_decic = data_logger.signal.bamocar_igbt_temp_decic;
  p_snapshot->bamocar_status = data_logger.signal.bamocar_status;
  p_snapshot->bamocar_error_warning = data_logger.signal.bamocar_error_warning;

  return p_snapshot->valid_mask != 0U;
}

const char *dataLoggerResultToString(FRESULT result)
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
static void cliDataLogger(cli_args_t *args)
{
  bool ret = false;

  if ((args->argc == 1) && args->isStr(0, "mount"))
  {
    cliPrintf("dlog mount : %s (%s)\n",
              dataLoggerMount() ? "OK" : "FAIL",
              dataLoggerResultToString(data_logger.last_result));
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

    cliPrintf("dlog start : %s (%s)\n",
              dataLoggerStart(can_ch, path) ? "OK" : "FAIL",
              dataLoggerResultToString(data_logger.last_result));
    if (data_logger.path[0] != 0)
    {
      cliPrintf("raw_path   : %s\n", data_logger.path);
      cliPrintf("sig_path   : %s\n", data_logger.signal_path);
    }
    ret = true;
  }

  if ((args->argc == 1) && args->isStr(0, "stop"))
  {
    dataLoggerStop();
    cliPrintf("dlog stop  : %s\n", dataLoggerResultToString(data_logger.last_result));
    ret = true;
  }

  if ((args->argc == 1) && args->isStr(0, "info"))
  {
    data_logger_info_t info;

    dataLoggerGetInfo(&info);
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
    cliPrintf("fatfs      : %s\n", dataLoggerResultToString(info.last_result));
    ret = true;
  }

  if ((args->argc == 1) && args->isStr(0, "signal"))
  {
    data_logger_signal_snapshot_t signal;
    bool valid;

    valid = dataLoggerGetSignalSnapshot(&signal);

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
    cliPrintf("dlog mount\n");
    cliPrintf("dlog start [raw_path]\n");
    cliPrintf("dlog stop\n");
    cliPrintf("dlog info\n");
    cliPrintf("dlog signal\n");
  }
}
#endif

#endif
