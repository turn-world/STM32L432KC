/*
 * apps.h
 *
 * Public APPS API. Runtime structures are private to apps.c.
 */

#ifndef SRC_COMMON_HW_INCLUDE_APPS_APPS_H_
#define SRC_COMMON_HW_INCLUDE_APPS_APPS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"


#define APPS_SIGNAL_COUNT                 2U
#define APPS_PER_MILLE_MAX                1000U
#define APPS_DEFAULT_DIFF_PER_MILLE       100U
#define APPS_DEFAULT_FAULT_CONFIRM_MS     100U


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


bool appsInit(uint8_t signal1_adc_ch,
              uint8_t signal2_adc_ch,
              uint8_t can_ch);                       // ADC/CAN 채널 등록 및 APPS 상태 초기화

bool appsSetConfig(uint16_t signal1_raw_min,
                   uint16_t signal1_raw_max,
                   uint16_t signal2_raw_min,
                   uint16_t signal2_raw_max);         // 두 센서의 페달 해제/최대 raw 보정값 설정

void appsSetRangeMargins(uint16_t signal1_low_margin,
                        uint16_t signal1_high_margin,
                        uint16_t signal2_low_margin,
                        uint16_t signal2_high_margin); // 센서 정상 범위의 raw 허용 여유값 설정

void appsSetFaultLimits(uint16_t max_diff_per_mille,
                        uint32_t fault_confirm_ms);    // 허용 센서 차이와 fault 확정 시간 설정
void appsClearFault(void);                            // latch된 fault와 fault 타이머 초기화

bool appsUpdate(void);                                // ADC 측정 및 APPS plausibility 갱신
bool appsUpdateRaw(uint16_t raw_signal1,
                   uint16_t raw_signal2);             // 전달받은 raw 값으로 APPS 시험
bool appsSendCommand(int16_t prepared_command);       // 정상 시 command, 이상 시 0을 CAN 전송
bool appsRun(int16_t prepared_command);               // ADC 측정, 판단, CAN 전송을 한 번에 실행

bool appsIsInitialized(void);                         // APPS 초기화 여부
bool appsIsConfigured(void);                          // 센서 보정값 설정 여부
bool appsIsValid(void);                               // torque command 허용 여부
bool appsIsAdcOk(void);                               // 마지막 ADC 측정 성공 여부
bool appsIsCanTxOk(void);                             // 마지막 CAN 전송 성공 여부
bool appsIsFaultLatched(void);                        // APPS fault latch 여부

uint8_t  appsGetAdcChannel(uint8_t signal);           // 신호에 연결된 ADC 채널
uint8_t  appsGetCanChannel(void);                     // Bamocar 전송용 CAN 채널
uint16_t appsGetRawMin(uint8_t signal);               // 페달 해제 위치 raw 보정값
uint16_t appsGetRawMax(uint8_t signal);               // 최대 페달 위치 raw 보정값
uint16_t appsGetRaw(uint8_t signal);                  // 마지막 ADC raw 값
uint16_t appsGetVoltageMv(uint8_t signal);            // 마지막 측정 전압(mV)
uint16_t appsGetPercentPerMille(uint8_t signal);      // 신호별 페달 위치(0~1000)
uint16_t appsGetDifferencePerMille(void);             // 두 센서 위치 차이(0~1000)
uint16_t appsGetPedalPerMille(void);                  // 정상 시 평균 페달 위치(0~1000)
uint16_t appsGetMaxDifferencePerMille(void);          // 설정된 최대 허용 차이
uint32_t appsGetFaultConfirmMs(void);                 // fault 확정 시간(ms)
uint32_t appsGetFaultElapsedMs(void);                 // 현재 fault 지속 시간(ms)
uint32_t appsGetStatus(void);                         // APPS 상태 비트
int16_t  appsGetCommandSent(void);                    // 마지막 CAN 전송 command


#ifdef __cplusplus
}
#endif

#endif /* SRC_COMMON_HW_INCLUDE_APPS_APPS_H_ */
