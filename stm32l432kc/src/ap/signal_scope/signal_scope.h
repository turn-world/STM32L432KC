/*
 * signal_scope.h
 *
 * UART CSV scope for ADC bring-up.
 */

#ifndef SRC_AP_SIGNAL_SCOPE_SIGNAL_SCOPE_H_
#define SRC_AP_SIGNAL_SCOPE_SIGNAL_SCOPE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_HW_ADC

/*
 * ADC 값을 CLI UART로 CSV 출력하는 테스트용 모듈이다.
 * apInit()에서 scopeInit()을 한 번 호출하고, apMain() 루프에서 scopeUpdate()를
 * 계속 호출한다. 실제 출력 시작/정지는 `scope` CLI 명령이 제어한다.
 */

/*
 * 모듈 상태를 초기화하고 `scope` CLI 명령을 등록한다.
 * 보통 apInit()에서 한 번만 호출한다.
 */
bool scopeInit(void);

/*
 * 백그라운드 CSV 스트림을 한 번 진행한다.
 * `scope start` 명령으로 스트림이 켜진 경우에만 주기에 맞춰 ADC 값을 출력한다.
 */
void scopeUpdate(void);

#endif

#ifdef __cplusplus
}
#endif

#endif /* SRC_AP_SIGNAL_SCOPE_SIGNAL_SCOPE_H_ */
