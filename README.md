# STM32L432KC

STM32L432KC 기반 임베디드 제어 펌웨어 프로젝트입니다.

## 프로젝트 소개

전동 차량의 가속 페달 위치 센서(APPS, Accelerator Pedal Position Sensor)를 읽어
신호의 타당성을 검증하고, 정상일 때만 모터 컨트롤러(Bamocar D3)에 CAN 토크 지령을
전달하는 제어 펌웨어입니다.

STM32CubeIDE / HAL 환경 위에서 동작하며, ADC · UART · CAN · CLI · LED 같은 기본
입출력과 통신 모듈을 직접 구성한 베어메탈 구조로 작성되었습니다.

## 주요 기능

- APPS 2채널 아날로그 입력을 ADC로 읽어 페달 위치(0~100%)로 변환
- 두 채널 신호를 비교해 센서 입력의 타당성(plausibility) 판단
- 정상 신호일 때만 CAN으로 토크 지령 전송, 이상 시 0 지령 유지
- UART 기반 CLI로 상태 확인 및 런타임 보정
- 상태 표시용 LED 제어

## 개발 환경

- MCU: STM32L432KC (ARM Cortex-M4)
- IDE: STM32CubeIDE
- 라이브러리: STM32L4xx HAL Driver, CMSIS

## 프로젝트 구조

```
stm32l432kc/
└── src/
    ├── ap/        # 애플리케이션 진입점 (apInit / apMain)
    ├── bsp/       # 보드 지원 (startup, linker script)
    ├── common/hw/ # CLI, UART, LED, CAN, APPS 등 하드웨어 모듈
    └── lib/       # STM32 HAL / CMSIS 라이브러리
```

## 참고

구현 세부 사항, 하드웨어 연동, 검증 절차 등은
[stm32l432kc/README.md](stm32l432kc/README.md)를 참고하세요.
