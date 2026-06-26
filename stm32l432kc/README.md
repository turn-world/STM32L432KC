# APPS bring-up guide

이 폴더는 APPS(Accelerator Pedal Position Sensor) 두 채널을 읽어서 비타당성(plausibility)을 판단하고, 정상일 때만 Bamocar D3에 CAN 토크 지령을 보내기 위한 시작점입니다.

## 전체 흐름

1. APPS 두 아날로그 출력 -> 아날로그 보호/분압 회로 -> STM32 ADC 2채널
2. ADC raw 값을 각 채널별 보정값으로 0~1000 per-mille(0.1% 단위)로 변환
3. 두 채널의 환산 페달 위치 차이가 10%p(=100 per-mille)를 넘으면 APPS fault
4. 정상일 때만 상위 제어기에서 준비한 torque command를 사용
5. CAN standard frame, ID `0x201`, DLC `3`, payload `{0x90, torque_lsb, torque_msb}` 전송
6. fault 또는 not configured 상태에서는 torque command `0` 전송

## 현재 넣어둔 파일

- `src/ap/apps/apps.h`, `apps.c`: APPS 구조체와 상태는 `apps.c` 내부 전역변수로 관리합니다. 외부에서는 채널, 보정값, torque command 같은 값만 함수 인자로 전달합니다.
- APPS CLI 출력과 런타임 보정 명령은 `apps.c` 안에서 같이 관리합니다.
- `src/ap/apps/bamocar_can.h`, `bamocar_can.c`: Bamocar D3의 16-bit register write/read request CAN payload를 만드는 작은 헬퍼입니다.

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
appsInit();
appsSetConfig(520, 3540, 3480, 470);
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

## CLI가 출력되지 않았던 원인과 복구 방법

### 최종 원인

CLI와 UART 코드가 실행되지 않은 직접 원인은 ADC 추가가 아니라 MCU의 부팅 모드였습니다.

STM32가 사용자 Flash의 `Reset_Handler`가 아닌 시스템 부트로더 영역에서 실행되고 있었습니다.

```text
정상 실행 주소: 0x0800xxxx (Main Flash)
문제 발생 주소: 0x1FFFxxxx (System Memory Bootloader)
```

당시 옵션 바이트는 `nSWBOOT0=1`이어서 BOOT0 값을 물리 핀에서 읽도록 되어 있었습니다. BOOT0 핀이 High로 인식되면서 시스템 부트로더로 진입했고, 그 결과 다음 현상이 발생했습니다.

- `main()`이 실행되지 않음
- `hwInit()`, `apInit()`이 실행되지 않음
- USART2 레지스터가 초기화되지 않음
- Tera Term에 `cli#` 프롬프트가 출력되지 않음
- 새 펌웨어를 정상적으로 Flash에 기록해도 애플리케이션이 시작되지 않음

ADC 설정을 추가한 시점과 증상 발생 시점이 겹쳤지만, ADC 변환이나 ADC 핀 자체가 CLI를 막은 것은 아니었습니다.

### 적용한 복구 설정

BOOT0 물리 핀 상태를 사용하지 않고 Main Flash로 부팅하도록 옵션 바이트를 변경했습니다.

```text
nSWBOOT0 = 0  // BOOT0 값을 옵션 바이트에서 선택
nBOOT0   = 1  // BOOT0 = 0, Main Flash 부팅
```

STM32CubeProgrammer CLI 명령:

```powershell
STM32_Programmer_CLI.exe -c port=SWD -ob nSWBOOT0=0 nBOOT0=1
STM32_Programmer_CLI.exe -c port=SWD -rst
```

설정 후 PC가 `0x0800xxxx` 영역에서 실행되는지 확인합니다.

```powershell
STM32_Programmer_CLI.exe -c port=SWD -score -coreReg PC LR MSP
```

### CLI 점검 순서

1. Tera Term에서 ST-Link Virtual COM Port 선택
2. `57600 baud`, `8 data bits`, `none parity`, `1 stop bit`, `flow control none`
3. 최신 `Debug/stm32l432kc.elf`를 Flash에 기록
4. PC가 `0x0800xxxx`에서 실행되는지 확인
5. Tera Term에서 Enter 입력

PC가 `0x1FFFxxxx`이면 UART 코드를 수정하기 전에 BOOT0/옵션 바이트부터 확인해야 합니다.

### UART 관련 참고

CLI 프롬프트가 출력되지만 입력이 동작하지 않는다면 그때는 UART RX 경로를 확인합니다.

- `PA2`: USART2 TX
- `PA15`: USART2 RX
- USART2 IRQ가 활성화되어 있는지 확인
- `HAL_UART_Receive_IT()` 또는 RX DMA 시작 결과 확인
- `USART2_IRQHandler()`에서 `HAL_UART_IRQHandler(&huart2)` 호출 확인

프롬프트 자체가 전혀 출력되지 않는 경우에는 UART RX보다 애플리케이션 부팅 여부를 먼저 확인하는 것이 빠릅니다.

상위 제어 루프에서는 준비된 torque command를 `appsRun()`에 전달합니다.

```c
int16_t prepared_command = controlGetPreparedTorqueCommand();

appsRun(prepared_command);
```

`appsRun()`은 ADC를 읽고 같은 호출 안에서 판단 결과에 따라 CAN을 전송합니다. `valid == false`이면 전달받은 command와 관계없이 항상 0을 전송합니다.

`apps.c`는 fault가 100 ms 이상 지속되면 latch합니다. 실제 차에서는 fault가 사라진 즉시 다시 토크를 내는 것보다, 운전자가 페달을 놓고 시스템이 안전 상태임을 확인한 뒤 `appsClearFault()`를 호출하는 쪽이 안전합니다.

## APPS fault 지속시간 계산

현재 기본 fault 확정 시간은 `100 ms`입니다.

```c
#define APPS_FAULT_CONFIRM_MS  100U
```

코드에서는 이상 상태가 처음 발견된 시간을 `started_ms`에 저장하고, 이후 `appsRun()` 또는 `appsUpdate()`가 호출될 때마다 현재까지의 지속시간을 계산합니다.

```c
elapsed_ms = millis() - started_ms;

if (elapsed_ms >= APPS_FAULT_CONFIRM_MS)
{
  fault_latched = true;
}
```

각 값의 의미는 다음과 같습니다.

- `millis()`: MCU가 부팅된 이후 현재까지 경과한 시간
- `started_ms`: 같은 APPS 이상이 처음 감지된 시간
- `elapsed_ms`: 해당 이상이 끊기지 않고 지속된 시간
- `fault_confirm_ms`: fault를 확정하는 기준 시간이며 현재 기본값은 `100 ms`
- `fault_latched`: 확정된 fault를 정상 복구 후에도 유지하는 상태

예를 들어 이상이 `1000 ms` 시점에 시작되고 현재 시간이 `1060 ms`이면 `elapsed_ms`는 `60 ms`입니다. 아직 100 ms에 도달하지 않았으므로 latch되지 않습니다.

```text
1060 - 1000 = 60 ms
```

같은 이상이 `1100 ms`까지 연속으로 유지되면 `elapsed_ms`가 `100 ms`가 되어 fault가 latch됩니다.

```text
1100 - 1000 = 100 ms → FAULT_LATCHED
```

### 100 ms 전에 정상으로 복구되는 경우

이상이 100 ms 전에 사라지면 fault 타이머가 0으로 초기화되며 latch되지 않습니다.

```text
이상 발생 → 60 ms 지속 → 정상 복구
fault timer 초기화
FAULT_LATCHED 발생하지 않음
```

이후 다시 이상이 발생하면 이전 60 ms에 이어서 계산하지 않고 0 ms부터 새로 측정합니다.

### CAN command 동작

100 ms는 CAN command를 0으로 만들기 전의 대기시간이 아닙니다. 이상이 감지되는 즉시 `valid`가 `false`가 되며, `appsRun()`이 호출될 때마다 CAN command `0`을 계속 전송합니다.

```text
정상                         → prepared_command 전송
이상 감지 직후               → command 0 전송
이상 지속 중                 → command 0 계속 전송
100 ms 전에 정상 복구        → prepared_command 전송 재개
100 ms 이상 지속             → fault latch, command 0 계속 전송
센서 정상 복구 후에도 latch  → command 0 계속 전송
appsClearFault() 후 정상 측정 → prepared_command 전송 재개
```

이 시간 계산은 `delay(100)`으로 CPU를 정지시키는 방식이 아닙니다. `millis()` 차이만 계산하는 non-blocking 방식이므로 CLI, CAN, LED 같은 다른 처리를 계속 실행할 수 있습니다. 단, 시간 검사와 CAN 전송은 `appsRun()` 또는 `appsUpdate()`가 호출될 때 진행되므로 실제 제어 루프에서는 이 함수를 일정한 주기로 반복 호출해야 합니다.

STM32CubeIDE managed build를 쓰면 새 하위 폴더가 자동 빌드 목록에 들어가도록 project refresh 또는 makefile regeneration이 필요할 수 있습니다.

## 검증 순서

1. 센서만 전원 인가하고 multimeter로 두 출력 전압 범위 확인
2. MCU ADC raw를 CLI로 출력해서 페달 release/full raw 기록
3. `apps info` 또는 `apps show`로 두 채널 per-mille과 fault 상태 확인
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
