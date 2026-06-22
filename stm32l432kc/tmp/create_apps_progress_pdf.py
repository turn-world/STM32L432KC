from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER, TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import mm
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (
    BaseDocTemplate,
    Frame,
    KeepTogether,
    PageTemplate,
    Paragraph,
    Spacer,
    Table,
    TableStyle,
)


OUTPUT = "docs/APPS_로직_진행상황_공유본.pdf"

FONT_REGULAR = "C:/Windows/Fonts/malgun.ttf"
FONT_BOLD = "C:/Windows/Fonts/malgunbd.ttf"

pdfmetrics.registerFont(TTFont("Malgun", FONT_REGULAR))
pdfmetrics.registerFont(TTFont("Malgun-Bold", FONT_BOLD))

PAGE_W, PAGE_H = A4
MARGIN_X = 18 * mm
MARGIN_TOP = 18 * mm
MARGIN_BOTTOM = 17 * mm
CONTENT_W = PAGE_W - (MARGIN_X * 2)

NAVY = colors.HexColor("#17324D")
BLUE = colors.HexColor("#2E74B5")
PALE_BLUE = colors.HexColor("#EAF2F8")
LIGHT_GRAY = colors.HexColor("#F3F5F7")
MID_GRAY = colors.HexColor("#66727D")
GREEN = colors.HexColor("#2F6B48")
PALE_GREEN = colors.HexColor("#EAF4EE")
AMBER = colors.HexColor("#8A6116")
PALE_AMBER = colors.HexColor("#FFF5DD")
RED = colors.HexColor("#9B2C2C")
PALE_RED = colors.HexColor("#FBEAEA")


def footer(canvas, doc):
    canvas.saveState()
    canvas.setStrokeColor(colors.HexColor("#D9E0E6"))
    canvas.setLineWidth(0.5)
    canvas.line(MARGIN_X, 12 * mm, PAGE_W - MARGIN_X, 12 * mm)
    canvas.setFont("Malgun", 8)
    canvas.setFillColor(MID_GRAY)
    canvas.drawString(MARGIN_X, 7.5 * mm, "APPS 로직 진행상황 공유본 | 2026-06-19")
    canvas.drawRightString(PAGE_W - MARGIN_X, 7.5 * mm, f"{doc.page}")
    canvas.restoreState()


styles = getSampleStyleSheet()
title_style = ParagraphStyle(
    "TitleK",
    parent=styles["Title"],
    fontName="Malgun-Bold",
    fontSize=22,
    leading=29,
    textColor=NAVY,
    alignment=TA_LEFT,
    spaceAfter=5 * mm,
)
subtitle_style = ParagraphStyle(
    "SubtitleK",
    parent=styles["Normal"],
    fontName="Malgun",
    fontSize=10.5,
    leading=16,
    textColor=MID_GRAY,
    spaceAfter=7 * mm,
)
h1_style = ParagraphStyle(
    "H1K",
    parent=styles["Heading1"],
    fontName="Malgun-Bold",
    fontSize=15,
    leading=20,
    textColor=BLUE,
    spaceBefore=5 * mm,
    spaceAfter=3 * mm,
)
h2_style = ParagraphStyle(
    "H2K",
    parent=styles["Heading2"],
    fontName="Malgun-Bold",
    fontSize=11.5,
    leading=16,
    textColor=NAVY,
    spaceBefore=3.5 * mm,
    spaceAfter=2 * mm,
)
body_style = ParagraphStyle(
    "BodyK",
    parent=styles["BodyText"],
    fontName="Malgun",
    fontSize=9.5,
    leading=15,
    textColor=colors.HexColor("#222222"),
    spaceAfter=2.5 * mm,
)
small_style = ParagraphStyle(
    "SmallK",
    parent=body_style,
    fontSize=8.5,
    leading=13,
)
bullet_style = ParagraphStyle(
    "BulletK",
    parent=body_style,
    leftIndent=5 * mm,
    firstLineIndent=-3.5 * mm,
    bulletIndent=0,
    spaceAfter=1.8 * mm,
)
code_style = ParagraphStyle(
    "CodeK",
    parent=body_style,
    fontName="Courier",
    fontSize=8.2,
    leading=12,
    textColor=colors.HexColor("#18344F"),
)
center_style = ParagraphStyle(
    "CenterK",
    parent=body_style,
    alignment=TA_CENTER,
)


def p(text, style=body_style):
    return Paragraph(text, style)


def bullet(text):
    return Paragraph(f"• {text}", bullet_style)


def callout(title, text, fill=PALE_BLUE, border=BLUE):
    data = [[p(f"<b>{title}</b>", body_style)], [p(text, body_style)]]
    table = Table(data, colWidths=[CONTENT_W - 4 * mm], hAlign="LEFT")
    table.setStyle(
        TableStyle(
            [
                ("BACKGROUND", (0, 0), (-1, -1), fill),
                ("BOX", (0, 0), (-1, -1), 0.8, border),
                ("LINEBELOW", (0, 0), (-1, 0), 0.4, border),
                ("LEFTPADDING", (0, 0), (-1, -1), 8),
                ("RIGHTPADDING", (0, 0), (-1, -1), 8),
                ("TOPPADDING", (0, 0), (-1, -1), 6),
                ("BOTTOMPADDING", (0, 0), (-1, -1), 6),
            ]
        )
    )
    return table


def styled_table(headers, rows, widths):
    data = [[p(f"<b>{h}</b>", small_style) for h in headers]]
    for row in rows:
        data.append([p(str(value), small_style) for value in row])
    table = Table(data, colWidths=widths, repeatRows=1, hAlign="LEFT")
    table.setStyle(
        TableStyle(
            [
                ("BACKGROUND", (0, 0), (-1, 0), LIGHT_GRAY),
                ("TEXTCOLOR", (0, 0), (-1, 0), NAVY),
                ("GRID", (0, 0), (-1, -1), 0.45, colors.HexColor("#BCC7D0")),
                ("VALIGN", (0, 0), (-1, -1), "MIDDLE"),
                ("LEFTPADDING", (0, 0), (-1, -1), 6),
                ("RIGHTPADDING", (0, 0), (-1, -1), 6),
                ("TOPPADDING", (0, 0), (-1, -1), 5),
                ("BOTTOMPADDING", (0, 0), (-1, -1), 5),
            ]
        )
    )
    return table


def flow_table():
    items = [
        ("1", "raw_ch1 / raw_ch2", "두 APPS 채널의 ADC raw 입력"),
        ("2", "채널별 스케일링", "보정 범위를 0~1000 per-mille로 변환"),
        ("3", "범위 검사", "각 채널이 허용 raw 범위 안인지 확인"),
        ("4", "상호 차이 검사", "두 채널의 pedal 값 차이가 10%를 넘는지 확인"),
        ("5", "Fault 확정 및 latch", "설정된 sample 수 이상 지속되면 fault 유지"),
        ("6", "Pedal 값 계산", "정상일 때 두 채널 per-mille의 평균 사용"),
        ("7", "Torque 계산", "pedal 값에 비례하여 0~32767 명령 생성"),
        ("8", "Fault 출력", "fault 상태에서는 valid=false, torque_cmd=0"),
    ]
    return styled_table(
        ["단계", "처리", "현재 구현"],
        items,
        [13 * mm, 42 * mm, CONTENT_W - 55 * mm],
    )


def build_story():
    story = []
    story.append(p("APPS 로직 진행상황 공유본", title_style))
    story.append(
        p(
            "STM32L432KC 기반 Accelerator Pedal Position Sensor 처리 로직의 현재 상태와 인수인계 항목",
            subtitle_style,
        )
    )

    summary = [
        [p("<b>기준일</b>", small_style), p("2026-06-19", small_style)],
        [p("<b>브랜치 / HEAD</b>", small_style), p("main / 94529c4", small_style)],
        [p("<b>현재 범위</b>", small_style), p("APPS 입력 판정, fault 처리, torque 계산, CLI 사전 테스트", small_style)],
        [p("<b>빌드 상태</b>", small_style), p("STM32CubeIDE Debug 전체 빌드 성공", small_style)],
        [p("<b>주의</b>", small_style), p("APPS CLI 추가 변경은 아직 Git 미커밋 상태", small_style)],
    ]
    summary_table = Table(summary, colWidths=[38 * mm, CONTENT_W - 38 * mm], hAlign="LEFT")
    summary_table.setStyle(
        TableStyle(
            [
                ("BACKGROUND", (0, 0), (0, -1), LIGHT_GRAY),
                ("GRID", (0, 0), (-1, -1), 0.45, colors.HexColor("#BCC7D0")),
                ("VALIGN", (0, 0), (-1, -1), "MIDDLE"),
                ("LEFTPADDING", (0, 0), (-1, -1), 7),
                ("RIGHTPADDING", (0, 0), (-1, -1), 7),
                ("TOPPADDING", (0, 0), (-1, -1), 6),
                ("BOTTOMPADDING", (0, 0), (-1, -1), 6),
            ]
        )
    )
    story.append(summary_table)
    story.append(Spacer(1, 4 * mm))
    story.append(
        callout(
            "한 줄 요약",
            "현재 APPS 핵심 로직은 구현되어 전체 펌웨어 빌드까지 통과했다. 실제 센서 ADC 입력과 100 ms 규정 검증은 다음 단계다.",
            PALE_GREEN,
            GREEN,
        )
    )

    story.append(p("1. 목표", h1_style))
    story.append(
        p(
            "두 개의 APPS 센서 신호를 독립적으로 해석하고, 두 신호가 서로 타당한지 검사한 뒤 정상 상태에서만 "
            "pedal 위치에 비례한 torque command를 생성하는 것이 목표다. 입력 이상이나 두 채널 불일치가 확인되면 "
            "torque command는 0으로 제한한다."
        )
    )

    story.append(p("2. 현재 구현 흐름", h1_style))
    story.append(flow_table())

    story.append(
        KeepTogether(
            [
                p("3. 핵심 데이터 구조와 API", h1_style),
                styled_table(
                    ["구성", "역할"],
                    [
                        ("apps_channel_cal_t", "채널별 raw_min, raw_max, low/high margin 보관"),
                        ("apps_config_t", "두 채널 보정값, 최대 허용 차이, fault 확정 sample 수, 최대 torque 설정"),
                        ("apps_state_t", "fault_count, latched_status, fault_latched 상태 유지"),
                        ("apps_result_t", "채널별 per-mille, pedal 값, torque_cmd, status, valid 반환"),
                        ("appsInit()", "APPS 상태와 설정 초기화"),
                        ("appsUpdate()", "raw 입력을 받아 전체 plausibility 판단 및 torque 결과 생성"),
                        ("appsClearFault()", "latched fault와 fault_count 초기화"),
                        ("appsPedalToBamocarTorque()", "pedal per-mille을 16-bit torque 명령으로 변환"),
                    ],
                    [53 * mm, CONTENT_W - 53 * mm],
                ),
            ]
        )
    )

    story.append(p("4. 판정 기준", h1_style))
    story.append(
        styled_table(
            ["항목", "현재 사전 테스트 설정", "설명"],
            [
                ("입력 raw 범위", "0~4095", "실제 센서 보정 전 임시 12-bit ADC 전체 범위"),
                ("pedal 표현", "0~1000 per-mille", "0.0~100.0%를 정수로 표현"),
                ("최대 채널 차이", "100 per-mille", "두 채널 pedal 값 차이가 10% 초과 시 DIFF_FAULT"),
                ("fault 확정", "1 sample", "CLI 사전 테스트용. 실제 규정의 100 ms 조건은 아직 미적용"),
                ("최대 torque", "32767", "pedal 100%일 때 최대 명령값"),
                ("fault 시 출력", "torque_cmd=0", "valid=false로 함께 반환"),
            ],
            [37 * mm, 40 * mm, CONTENT_W - 77 * mm],
        )
    )
    story.append(
        callout(
            "중요",
            "현재 10% 차이 기준은 구현되어 있지만, 규정에서 요구하는 '100 ms 이상 지속'은 아직 시간 기반으로 연결되지 않았다. "
            "현재 구조는 fault_confirm_samples로 확정하므로, 실제 주기와 sample 수를 정한 뒤 100 ms에 맞춰야 한다.",
            PALE_AMBER,
            AMBER,
        )
    )

    story.append(p("5. 정상 및 Fault 동작", h1_style))
    story.append(p("정상 상태", h2_style))
    story.append(bullet("각 채널 raw 값이 설정된 허용 범위 안에 있어야 한다."))
    story.append(bullet("두 채널의 pedal 값 차이가 max_diff_per_mille 이하이어야 한다."))
    story.append(bullet("latched fault가 없어야 한다."))
    story.append(bullet("정상일 때 pedal 값은 두 채널 per-mille의 평균으로 계산한다."))
    story.append(bullet("torque_cmd는 pedal 값에 비례하여 생성한다."))

    story.append(p("Fault 상태", h2_style))
    story.append(bullet("CH1_RANGE_FAULT: 1번 채널 raw 값이 허용 범위를 벗어남"))
    story.append(bullet("CH2_RANGE_FAULT: 2번 채널 raw 값이 허용 범위를 벗어남"))
    story.append(bullet("DIFF_FAULT: 두 채널의 환산 pedal 값 차이가 허용치 초과"))
    story.append(bullet("FAULT_LATCHED: 확정된 fault가 clear 전까지 유지됨"))
    story.append(bullet("fault 발생 시 pedal_per_mille=0, torque_cmd=0, valid=false"))

    story.append(p("6. CLI 사전 테스트", h1_style))
    story.append(
        styled_table(
            ["명령", "확인 내용"],
            [
                ("apps test raw_ch1 raw_ch2", "두 raw 값의 변환 결과, fault 상태, pedal %, torque 명령 확인"),
                ("apps torque pedal_per_mille", "0~1000 입력을 torque 값으로 변환하는 결과 확인"),
                ("apps clear", "latched fault 해제"),
            ],
            [64 * mm, CONTENT_W - 64 * mm],
        )
    )
    story.append(p("권장 확인 예시", h2_style))
    story.append(
        styled_table(
            ["명령", "예상 결과"],
            [
                ("apps test 1000 1000", "두 채널이 같으므로 정상 판정, valid=true"),
                ("apps test 1000 2500", "채널 차이 초과로 DIFF_FAULT, torque_cmd=0"),
                ("apps clear", "FAULT_LATCHED 해제"),
                ("apps torque 500", "pedal 50.0%, torque_cmd 약 16383"),
            ],
            [58 * mm, CONTENT_W - 58 * mm],
        )
    )

    story.append(p("7. 구현 파일 위치", h1_style))
    story.append(
        styled_table(
            ["파일", "내용", "상태"],
            [
                ("src/common/hw/include/APPS/apps_plausibility.h", "APPS 구조체, status, API 선언", "수정됨 / 미커밋"),
                ("src/hw/driver/APPS/apps_plausibility.c", "스케일링, plausibility, latch, torque 계산, CLI", "수정됨 / 미커밋"),
                ("src/common/hw/include/APPS/bamocar_can.h", "torque frame용 상수와 frame 구조", "기존 파일"),
                ("src/hw/driver/APPS/bamocar_can.c", "16-bit torque frame 생성 helper", "기존 파일"),
                ("src/hw/hw.c", "appsCliInit() 등록", "수정됨 / 미커밋"),
            ],
            [73 * mm, 62 * mm, CONTENT_W - 135 * mm],
        )
    )

    story.append(
        KeepTogether(
            [
                p("8. 검증 상태", h1_style),
                styled_table(
                    ["검증 항목", "상태", "비고"],
                    [
                        ("APPS 소스 컴파일", "완료", "STM32 GCC 컴파일 성공"),
                        ("전체 Debug 빌드", "완료", "stm32l432kc.elf 생성 성공"),
                        ("펌웨어 크기", "확인", "text 57,832 B / data 484 B / bss 4,420 B"),
                        ("UART/CLI 기반", "완료", "초기 DeInit 문제 수정 후 CLI 정상 동작 확인"),
                        ("APPS CLI 보드 실행", "최종 확인 필요", "빌드에는 포함됨. 협업 전에 명령 출력 캡처 권장"),
                        ("실제 APPS ADC 입력", "미진행", "ADC 드라이버 및 실제 센서 연결 필요"),
                        ("100 ms fault 지속 조건", "미진행", "실제 update 주기 확정 후 구현 필요"),
                    ],
                    [62 * mm, 29 * mm, CONTENT_W - 91 * mm],
                ),
            ]
        )
    )

    story.append(p("9. 다음 작업", h1_style))
    next_steps = [
        "CubeMX에서 APPS용 ADC 채널 2개를 확정하고 ADC driver를 연결한다.",
        "실제 센서의 0% 및 100% raw 값을 측정해 채널별 raw_min/raw_max를 확정한다.",
        "Signal1과 Signal2의 실제 방향과 기울기를 확인한다. 반전 채널도 현재 스케일 함수에서 지원한다.",
        "appsUpdate() 호출 주기를 확정하고 fault_confirm_samples를 100 ms에 맞춘다.",
        "정상, 단선, 단락, 채널 불일치 시나리오를 CLI 및 실제 입력으로 시험한다.",
        "시험 결과에서 raw 값, per-mille, status, torque_cmd를 기록하여 APPS 보고서의 검증 표로 사용한다.",
        "검증 후 현재 미커밋 APPS 변경을 별도 커밋한다.",
    ]
    for idx, text in enumerate(next_steps, start=1):
        story.append(p(f"<b>{idx}.</b> {text}", body_style))

    story.append(
        callout(
            "인수인계 결론",
            "APPS 순수 로직은 구현 및 빌드 완료 상태다. 다음 담당자는 ADC 실입력 연결과 100 ms fault 확정 조건부터 진행하면 된다. "
            "현재 APPS CLI 변경은 미커밋이므로 작업 시작 전에 Git diff를 확인해야 한다.",
            PALE_BLUE,
            BLUE,
        )
    )
    return story


def main():
    doc = BaseDocTemplate(
        OUTPUT,
        pagesize=A4,
        leftMargin=MARGIN_X,
        rightMargin=MARGIN_X,
        topMargin=MARGIN_TOP,
        bottomMargin=MARGIN_BOTTOM,
        title="APPS 로직 진행상황 공유본",
        author="Codex",
    )
    frame = Frame(
        MARGIN_X,
        MARGIN_BOTTOM,
        CONTENT_W,
        PAGE_H - MARGIN_TOP - MARGIN_BOTTOM,
        id="normal",
    )
    doc.addPageTemplates([PageTemplate(id="APPS", frames=[frame], onPage=footer)])
    doc.build(build_story())


if __name__ == "__main__":
    main()
