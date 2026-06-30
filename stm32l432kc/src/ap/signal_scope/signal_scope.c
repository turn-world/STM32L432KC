/*
 * signal_scope.c
 *
 * ADC CSV stream for sensor bring-up.
 *
 * 이 모듈은 ADC 드라이버가 읽어온 값을 CLI UART로 CSV 형태로 출력한다.
 * ADC 드라이버는 하드웨어 접근만 담당하고, 이 파일은 센서 검증용 출력 형식과
 * 샘플링 주기 제어만 담당한다.
 */

#include "signal_scope.h"

#ifdef _USE_HW_ADC

#include "adc.h"
#include "cli.h"

#define SIGNAL_SCOPE_DEFAULT_PERIOD_MS   20U
#define SIGNAL_SCOPE_MIN_PERIOD_MS        5U
#define SIGNAL_SCOPE_MAX_PERIOD_MS     1000U
#define SIGNAL_SCOPE_LINE_BUF_LEN       192U

typedef struct
{
  /* scopeInit()이 호출되어 CLI 명령 등록과 기본값 설정이 끝났는지 표시한다. */
  bool     initialized;

  /* scope start 명령 후 메인 루프에서 주기적으로 CSV를 출력할지 표시한다. */
  bool     streaming;

  /* 새 측정 세션이 시작되어 CSV 헤더를 아직 출력하지 않았는지 표시한다. */
  bool     header_pending;

  /* ADC 샘플을 출력할 주기(ms). CLI 입력값은 ClampPeriodMs()에서 제한된다. */
  uint32_t period_ms;

  /* 마지막으로 샘플을 출력한 millis() 시각. 다음 출력 시점 계산에 사용한다. */
  uint32_t last_sample_ms;

  /* 현재 세션에서 출력한 샘플 줄 수. CSV의 sample 열에 들어간다. */
  uint32_t sample_count;

  /* ADC 읽기 실패 또는 CSV 헤더 생성 실패 횟수. scope info에서 확인한다. */
  uint32_t read_error_count;
} scope_t;

static scope_t scope;

#ifdef _USE_HW_CLI
static void CliCommand(cli_args_t *args);
#endif

static uint32_t ClampPeriodMs(uint32_t period_ms);
static void ClearStreamState(void);
static void StartStream(uint32_t period_ms);
static void StopStream(void);
static bool IsSamplePeriodElapsed(uint32_t now_ms);
static bool AppendCsv(char *line, uint32_t line_len, int *p_len,
                      const char *fmt, ...);
static void PrintLine(const char *line);
static bool PrintCsvHeader(void);
static bool PrintCsvSample(uint32_t time_ms);
static bool PrintHeaderIfNeeded(void);
static void RunBlockingStream(uint32_t period_ms);


/* Stream timing and session state ------------------------------------------ */

/*
 * CLI에서 받은 샘플링 주기를 안전한 범위로 제한한다.
 * 0을 입력하면 기본값을 사용하고, 너무 빠르거나 느린 값은 최소/최대값으로 보정한다.
 */
static uint32_t ClampPeriodMs(uint32_t period_ms)
{
  if (period_ms == 0U)                          return SIGNAL_SCOPE_DEFAULT_PERIOD_MS;
  if (period_ms < SIGNAL_SCOPE_MIN_PERIOD_MS)   return SIGNAL_SCOPE_MIN_PERIOD_MS;
  if (period_ms > SIGNAL_SCOPE_MAX_PERIOD_MS)   return SIGNAL_SCOPE_MAX_PERIOD_MS;

  return period_ms;
}

/*
 * 새 CSV 측정 세션을 시작하기 전에 누적 상태를 초기화한다.
 * sample_count와 read_error_count를 0으로 되돌리고, 다음 출력 때 헤더가 먼저
 * 나가도록 header_pending을 true로 만든다.
 */
static void ClearStreamState(void)
{
  scope.sample_count = 0U;
  scope.read_error_count = 0U;
  scope.header_pending = true;
}

/*
 * 백그라운드 CSV 스트림을 시작한다.
 * 이 함수는 scope start 명령에서 호출되며, 이후 실제 출력은 apMain()의
 * scopeUpdate()가 메인 루프에서 주기적으로 수행한다.
 */
static void StartStream(uint32_t period_ms)
{
  if (scope.initialized != true)
  {
    (void)scopeInit();
  }

  scope.period_ms = ClampPeriodMs(period_ms);
  scope.last_sample_ms = millis() - scope.period_ms;

  ClearStreamState();
  scope.streaming = true;
}

/*
 * 백그라운드 CSV 스트림을 멈춘다.
 * scope stop 명령 또는 scope show 시작 전에 호출되어 메인 루프 출력이 중복되지 않게 한다.
 */
static void StopStream(void)
{
  scope.streaming = false;
}

/*
 * 마지막 샘플 출력 이후 설정된 주기가 지났는지 검사한다.
 * true이면 scopeUpdate() 또는 RunBlockingStream()에서 새 샘플을 출력해도 된다.
 */
static bool IsSamplePeriodElapsed(uint32_t now_ms)
{
  return (now_ms - scope.last_sample_ms) >= scope.period_ms;
}


/* CSV formatting and output ------------------------------------------------- */

/*
 * CSV 한 줄을 만들 때 사용하는 안전한 append 함수다.
 * snprintf 결과가 버퍼 크기를 넘으면 false를 반환해서 잘린 CSV 줄이 출력되지 않게 한다.
 */
static bool AppendCsv(char *line, uint32_t line_len, int *p_len,
                      const char *fmt, ...)
{
  va_list args;
  int remain;
  int written;

  if ((line == NULL) || (p_len == NULL) || (*p_len < 0) || ((uint32_t)(*p_len) >= line_len))  return false;

  remain = (int)line_len - *p_len;

  va_start(args, fmt);
  written = vsnprintf(&line[*p_len], (size_t)remain, fmt, args);
  va_end(args);

  if ((written < 0) || (written >= remain))
  {
    return false;
  }

  *p_len += written;
  return true;
}

/*
 * 완성된 한 줄을 CLI UART로 출력한다.
 * 현재 모듈은 PC에서 터미널 로그를 저장하는 용도이므로 출력 경로는 cliPrintf() 하나로 고정한다.
 */
static void PrintLine(const char *line)
{
#ifdef _USE_HW_CLI
  if (line != NULL)
  {
    cliPrintf("%s", line);
  }
#else
  (void)line;
#endif
}

/*
 * CSV 헤더 줄을 출력한다.
 * ADC 채널 수(ADC_MAX_CH)에 맞춰 ch0_raw,ch0_mv,ch1_raw,ch1_mv... 형식의 열 이름을 만든다.
 */
static bool PrintCsvHeader(void)
{
  char line[SIGNAL_SCOPE_LINE_BUF_LEN];
  int len = 0;

  if (AppendCsv(line, sizeof(line), &len, "time_ms,sample") != true)
  {
    return false;
  }

  for (uint8_t ch = 0; ch < ADC_MAX_CH; ch++)
  {
    if (AppendCsv(line,
                  sizeof(line),
                  &len,
                  ",ch%u_raw,ch%u_mv",
                  ch,
                  ch) != true)
    {
      return false;
    }
  }

  if (AppendCsv(line, sizeof(line), &len, "\r\n") != true)
  {
    return false;
  }

  PrintLine(line);
  return true;
}

/*
 * ADC 값을 한 번 읽고 CSV 데이터 한 줄을 출력한다.
 * adcUpdate()로 전체 ADC 채널 값을 갱신한 뒤 각 채널의 raw 값과 mV 변환값을 출력한다.
 */
static bool PrintCsvSample(uint32_t time_ms)
{
  char line[SIGNAL_SCOPE_LINE_BUF_LEN];
  int len = 0;

  if (adcUpdate() != true)
  {
    scope.read_error_count++;
    return false;
  }

  if (AppendCsv(line,
                sizeof(line),
                &len,
                "%lu,%lu",
                (unsigned long)time_ms,
                (unsigned long)scope.sample_count) != true)
  {
    return false;
  }

  for (uint8_t ch = 0; ch < ADC_MAX_CH; ch++)
  {
    uint32_t raw = (uint32_t)adcRead(ch);
    uint32_t mv = adcConvMillivolts(ch, raw);

    if (AppendCsv(line,
                  sizeof(line),
                  &len,
                  ",%lu,%lu",
                  (unsigned long)raw,
                  (unsigned long)mv) != true)
    {
      return false;
    }
  }

  if (AppendCsv(line, sizeof(line), &len, "\r\n") != true)
  {
    return false;
  }

  PrintLine(line);
  scope.sample_count++;

  return true;
}

/*
 * 현재 세션에서 헤더가 아직 출력되지 않았다면 먼저 헤더를 출력한다.
 * start/show 명령 직후 첫 샘플 전에 한 번만 호출되도록 header_pending 플래그를 관리한다.
 */
static bool PrintHeaderIfNeeded(void)
{
  if (scope.header_pending != true)     return true;

  if (PrintCsvHeader() != true)
  {
    scope.read_error_count++;
    return false;
  }

  scope.header_pending = false;
  return true;
}


/* Manual foreground stream -------------------------------------------------- */

/*
 * CLI를 붙잡고 즉석 CSV 스트림을 출력한다.
 * scope show 명령에서 사용하며, 사용자가 터미널에서 아무 키나 누르면 루프가 종료된다.
 * 이 모드는 현장 테스트에서 Tera Term 로그 저장을 켜고 바로 데이터를 모을 때 쓰기 좋다.
 */
static void RunBlockingStream(uint32_t period_ms)
{
  uint32_t last_sample_ms;

  StopStream();
  scope.period_ms = ClampPeriodMs(period_ms);
  ClearStreamState();

  (void)PrintHeaderIfNeeded();
  last_sample_ms = millis() - scope.period_ms;

  while (cliKeepLoop())
  {
    uint32_t now_ms = millis();

    if ((now_ms - last_sample_ms) >= scope.period_ms)
    {
      last_sample_ms = now_ms;
      (void)PrintCsvSample(now_ms);
    }

    delay(1);
  }
}


/* Public module API --------------------------------------------------------- */

/*
 * signal scope 모듈을 초기화한다.
 * 내부 상태를 기본값으로 되돌리고 CLI 명령어 "scope"를 등록한다.
 * apInit()에서 한 번 호출된다.
 */
bool scopeInit(void)
{
  memset(&scope, 0, sizeof(scope));
  scope.period_ms = SIGNAL_SCOPE_DEFAULT_PERIOD_MS;
  ClearStreamState();
  scope.initialized = true;

#ifdef _USE_HW_CLI
  (void)cliAdd("scope", CliCommand);
#endif

  return true;
}

/*
 * 백그라운드 CSV 스트림을 진행한다.
 * apMain()의 메인 루프에서 계속 호출되지만, scope start 명령으로 streaming이 켜진
 * 상태에서만 실제 ADC 샘플을 출력한다.
 */
void scopeUpdate(void)
{
  uint32_t now_ms;

  if ((scope.initialized != true) || (scope.streaming != true))     return;

  now_ms = millis();
  if (IsSamplePeriodElapsed(now_ms) != true)                        return;

  scope.last_sample_ms = now_ms;

  if (PrintHeaderIfNeeded() != true)                                return;

  (void)PrintCsvSample(now_ms);
}

#ifdef _USE_HW_CLI


/* Single CLI entry point ---------------------------------------------------- */

/*
 * "scope" CLI 명령의 유일한 진입점이다.
 * 하위 명령(info/once/show/start/stop)을 해석하고, 실제 동작은 스트림 제어 함수와
 * CSV 출력 함수에 위임한다.
 */
static void CliCommand(cli_args_t *args)
{
  bool handled = false;
  uint32_t period_ms = scope.period_ms;

  if (args->argc >= 2)
  {
    period_ms = ClampPeriodMs((uint32_t)args->getData(1));
  }

  if ((args->argc == 1) && (args->isStr(0, "info") == true))
  {
    cliPrintf("scope running : %s\n", scope.streaming ? "true" : "false");
    cliPrintf("period_ms     : %lu\n", (unsigned long)scope.period_ms);
    cliPrintf("samples       : %lu\n", (unsigned long)scope.sample_count);
    cliPrintf("read_errors   : %lu\n", (unsigned long)scope.read_error_count);
    cliPrintf("channels      : %u\n", ADC_MAX_CH);
    cliPrintf("csv           : time_ms,sample,chN_raw,chN_mv\n");
    handled = true;
  }

  if ((args->argc == 1) && (args->isStr(0, "once") == true))
  {
    ClearStreamState();
    (void)PrintCsvHeader();
    (void)PrintCsvSample(millis());
    handled = true;
  }

  if ((args->argc >= 1) && (args->argc <= 2) &&
      (args->isStr(0, "show") == true))
  {
    RunBlockingStream(period_ms);
    handled = true;
  }

  if ((args->argc >= 1) && (args->argc <= 2) &&
      (args->isStr(0, "start") == true))
  {
    StartStream(period_ms);
    cliPrintf("scope start : %lums\n", (unsigned long)scope.period_ms);
    handled = true;
  }

  if ((args->argc == 1) && (args->isStr(0, "stop") == true))
  {
    StopStream();
    cliPrintf("scope stop\n");
    handled = true;
  }

  if (handled != true)
  {
    cliPrintf("scope - ADC CSV stream over CLI UART\n");
    cliPrintf("scope info\n");
    cliPrintf("scope once\n");
    cliPrintf("scope show [period_ms]  ; press any key to stop\n");
    cliPrintf("scope start [period_ms] ; background stream\n");
    cliPrintf("scope stop\n");
  }
}

#endif

#endif
