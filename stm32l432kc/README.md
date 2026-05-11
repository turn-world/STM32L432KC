# APPS bring-up guide

이 폴더는 APPS(Accelerator Pedal Position Sensor) 두 채널을 읽어서 비타당성(plausibility)을 판단하고, 정상일 때만 Bamocar D3에 CAN 토크 지령을 보내기 위한 시작점입니다.

## 전체 흐름

1. APPS 두 아날로그 출력 -> 아날로그 보호/분압 회로 -> STM32 ADC 2채널
2. ADC raw 값을 각 채널별 보정값으로 0~1000 per-mille(0.1% 단위)로 변환
3. 두 채널의 환산 페달 위치 차이가 10%p(=100 per-mille)를 넘으면 APPS fault
4. 정상일 때만 평균 페달 위치를 `0..32767` Bamocar torque command로 변환
5. CAN standard frame, ID `0x201`, DLC `3`, payload `{0x90, torque_lsb, torque_msb}` 전송
6. fault 시에는 즉시 torque command `0` 전송

## 현재 넣어둔 파일

- `src/common/hw/include/APPS/apps_plausibility.h`, `apps_plausibility.c`: HAL 없이 동작하는 APPS 보정/비타당성 판별 로직입니다. 두 채널이 같은 방향이든 반대 방향이든 `raw_min`, `raw_max`만 실제 측정값으로 넣으면 됩니다.
- `src/common/hw/include/APPS/bamocar_can.h`, `bamocar_can.c`: Bamocar D3의 16-bit register write/read request CAN payload를 만드는 작은 헬퍼입니다.

## 먼저 확정해야 할 하드웨어

### APPS 센서

사용 예정인 KYOCERA AVX `9168100100`은 DigiKey 기준으로 two-channel Hall angle sensor, 0~45도, analog voltage output, 4.75~5.25 V supply입니다. 따라서 STM32 ADC에 직접 5 V 아날로그를 넣으면 안 됩니다. STM32L432KC의 ADC 입력은 MCU의 `VDDA` 범위를 넘기면 안 되므로, 센서는 5 V로 구동하되 각 출력은 분압 또는 버퍼 회로를 거쳐 0~3.3 V 안으로 제한해야 합니다.

추천 시작 회로:

- 센서 전원: 안정적인 5 V regulator
- 센서 GND와 MCU AGND: 같은 기준점으로 연결
- 각 신호: 센서 출력 -> RC 저역통과/보호 -> 분압 -> ADC pin
- 분압 예: `18 kOhm` series, `33 kOhm` to GND이면 5.0 V가 약 3.24 V가 됩니다.
- ADC 앞단에는 너무 큰 저항값을 쓰지 말고, CubeMX에서 ADC sampling time을 길게 잡습니다.

정확한 pinout, output curve, dual-channel 방향(같은 방향/반대 방향)은 실제 datasheet와 실측으로 확인해야 합니다. 비타당성 판단은 raw 전압끼리 비교하지 말고, 반드시 각 채널을 0~100%로 보정한 뒤 비교합니다.

### STM32 ADC 설정

현재 프로젝트의 Cube 설정에는 CAN은 켜져 있지만 ADC HAL이 아직 꺼져 있습니다. 다음 순서로 켜는 것을 추천합니다.

1. CubeMX 또는 STM32CubeIDE에서 ADC1 enable
2. USART1 `PA9/PA10`, CAN1 `PA11/PA12`, SWD `PA13/PA14`와 겹치지 않는 ADC 가능 핀 2개 선택
3. ADC resolution 12-bit, scan conversion 2 ranks
4. 처음에는 polling으로 읽고, 값이 안정되면 DMA circular로 변경
5. 부팅 시 `HAL_ADCEx_Calibration_Start()` 후 측정 시작

ADC raw 보정은 실제 페달을 놓은 상태와 끝까지 밟은 상태에서 각 채널 raw를 기록해서 채웁니다.

```c
static apps_config_t apps_cfg =
{
  .ch1 = { .raw_min = 520,  .raw_max = 3540, .low_margin = 80, .high_margin = 80 },
  .ch2 = { .raw_min = 3480, .raw_max = 470,  .low_margin = 80, .high_margin = 80 },
  .max_diff_per_mille = 100,
  .fault_confirm_samples = 3,
  .max_torque_cmd = APPS_BAMOCAR_CMD_MAX,
};
```

위 예시처럼 2번 채널이 반대 방향이면 `raw_min > raw_max`로 넣어도 됩니다.

## Bamocar CAN 쪽 핵심

STM32의 CAN RX/TX 핀은 CAN bus가 아니라 로직 신호입니다. 반드시 CAN transceiver가 필요합니다.

- STM32 CAN1 RX/TX: 현재 Cube 설정상 `PA11/PA12`
- 권장 transceiver: 3.3 V MCU 로직과 호환되는 ISO 11898-2 CAN transceiver
- Bamocar X9 CAN connector: `X9:3 CAN-GND`, `X9:4 CAN-H`, `X9:5 CAN-L`
- CAN-H/CAN-L 양 끝단에만 120 Ohm termination
- Bamocar 기본 CAN 설정: Rx ID `0x201`, Tx ID `0x181`, 500 kbaud

Bamocar CAN manual 기준으로 master -> controller frame은 byte 0이 register ID, byte 1부터 little-endian data입니다. Torque command register는 `0x90`이고, 100% command는 `32767` 기준입니다. 예를 들어 50% torque는 `16380 = 0x3FFC`라서 payload가 `{0x90, 0xFC, 0x3F}`가 됩니다.

프로젝트의 기존 `can_msg_t`로 보내는 모양은 아래처럼 잡으면 됩니다.

```c
#include "APPS/apps_plausibility.h"
#include "APPS/bamocar_can.h"

apps_result_t apps_ret;
bamocar_can_frame_t frame;
can_msg_t msg;

apps_ret = appsUpdate(&apps_state, adc_raw_ch1, adc_raw_ch2);
bamocarCanMakeTorqueCmd(BAMOCAR_CAN_DEFAULT_RX_ID, apps_ret.torque_cmd, &frame);

canMsgInit(&msg, CAN_CLASSIC, CAN_STD, CAN_DLC_3);
msg.id = frame.id;
msg.length = frame.dlc;
memcpy(msg.data, frame.data, frame.dlc);
canMsgWrite(_DEF_CAN1, &msg, 10);
```

`apps_ret.valid == false`이면 `apps_ret.torque_cmd`는 항상 0입니다.

`apps_plausibility.c`는 fault가 확정되면 latch합니다. 실제 차에서는 fault가 사라진 즉시 다시 토크를 내는 것보다, 운전자가 페달을 놓고 시스템이 안전 상태임을 확인한 뒤 `appsClearFault()`를 호출하는 쪽이 안전합니다.

STM32CubeIDE managed build를 쓰면 새 하위 폴더가 자동 빌드 목록에 들어가도록 project refresh 또는 makefile regeneration이 필요할 수 있습니다.

## 검증 순서

1. 센서만 전원 인가하고 multimeter로 두 출력 전압 범위 확인
2. MCU ADC raw를 CLI로 출력해서 페달 release/full raw 기록
3. `appsUpdate()` 결과의 두 채널 per-mille과 fault 상태 확인
4. CAN transceiver + USB-CAN으로 ID `0x201`, DLC `3`, payload 확인
5. Bamocar는 고전압/모터 enable 없이 먼저 bus 수신 확인
6. NDrive에서 command mode가 digital torque command를 받을 수 있게 설정되었는지 확인
7. 바퀴가 떠 있거나 동력 전달이 끊긴 상태에서 작은 positive command부터 시험
8. 회생제동은 배터리/전원장치가 braking energy를 흡수할 수 있는지 확인한 뒤 negative command 시험

## 브레이크 유압센서 추가 시

브레이크 압력은 별도 ADC 채널로 읽고, APPS와 동일하게 범위/단선/단락 fault를 먼저 만든 뒤 torque command에 합산합니다.

```c
net_cmd = accel_cmd - regen_cmd;
```

초기 안전 정책은 APPS fault 또는 brake sensor fault 중 하나라도 있으면 `net_cmd = 0`으로 두는 것을 추천합니다. 회생제동까지 안전하게 허용하려면 brake sensor의 별도 plausibility와 Bamocar/BMS의 DC bus overvoltage 대응이 먼저 검증되어야 합니다.

## 참고 자료

- APPS 후보: [DigiKey KYOCERA AVX 9168100100](https://www.digikey.com/en/products/detail/kyocera-avx/9168100100/10491367)
- Bamocar hardware manual: [UniTek BAMOCAR-PG-D3-400/400 manual](https://www.unitek-industrie-elektronik.de/wp-content/uploads/BAMOCAR-PG-D3-700-400_EN.pdf)
- Bamocar CAN manual: [UniTek CAN-BUS manual](https://www.unitek-industrie-elektronik.de/wp-content/uploads/CAN_EN.pdf)
- NDrive software manual: [UniTek NDrive manual](https://www.unitek-industrie-elektronik.de/wp-content/uploads/NDrive_EN.pdf)
