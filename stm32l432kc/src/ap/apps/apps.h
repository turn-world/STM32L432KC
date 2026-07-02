/*
 * apps.h
 *
 * Accelerator Pedal Position Sensor application logic.
 */

#ifndef SRC_AP_APPS_APPS_H_
#define SRC_AP_APPS_APPS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"
#include "adc.h"
#include "bamocar_can.h"


#define APPS_SIGNAL_COUNT                  2U
#define APPS_PER_MILLE_MAX                 1000U

#define APPS_SIGNAL1_ADC_CH                _DEF_ADC1
#define APPS_SIGNAL2_ADC_CH                _DEF_ADC2

#define APPS_CAN_CH                        _DEF_CAN1

#define APPS_SIGNAL1_RAW_MIN_DEFAULT       0U
#define APPS_SIGNAL1_RAW_MAX_DEFAULT       3102U      //2.5v
#define APPS_SIGNAL2_RAW_MIN_DEFAULT       0U
#define APPS_SIGNAL2_RAW_MAX_DEFAULT       4095U      //3.3v

#define APPS_RAW_LOW_MARGIN                80U
#define APPS_RAW_HIGH_MARGIN               80U
#define APPS_MAX_DIFF_PER_MILLE            100U
#define APPS_FAULT_CONFIRM_MS              100U

typedef enum
{
  APPS_SIGNAL1 = 0,
  APPS_SIGNAL2,
} apps_signal_t;

typedef enum
{
  APPS_STATUS_OK                  = 0x00000000U,
  APPS_STATUS_NOT_CONFIGURED      = 0x00000001U,
  APPS_STATUS_ADC_READ_FAULT      = 0x00000002U,
  APPS_STATUS_SIGNAL1_RANGE_FAULT = 0x00000004U,
  APPS_STATUS_SIGNAL2_RANGE_FAULT = 0x00000008U,
  APPS_STATUS_DIFF_FAULT          = 0x00000010U,
  APPS_STATUS_FAULT_LATCHED       = 0x00000020U,
  APPS_STATUS_CAN_TX_FAULT        = 0x00000040U,
} apps_status_t;


bool appsInit(void);
bool appsSetConfig(uint16_t signal1_raw_min,
                   uint16_t signal1_raw_max,
                   uint16_t signal2_raw_min,
                   uint16_t signal2_raw_max);
void appsClearFault(void);

bool appsUpdate(void);
bool appsSendCommand(int16_t prepared_command);
bool appsRun(int16_t prepared_command);


#ifdef __cplusplus
}
#endif

#endif /* SRC_AP_APPS_APPS_H_ */
