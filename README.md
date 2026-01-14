# PX68K-onionmixer - 포터블 X68000 에뮬레이터

**PX68K-onionmixer V 0.20**

2025/01

원본이 되는 px68k 프로젝트에서 최신의 linux desktop 에서 사용할 수 있도록 몇가지를 개선한 프로젝트입니다. 다음의 개선점이 있습니다.

1. SDL1 의 제거 및 SDL2 의 적용
2. 64bit 환경을 위한 몇가지 코드 개선
3. command line 실행을 위한 몇가지 command line option 의 추가
4. psp, android, mac os x 의 지원 제거
5. SDL2 기반의 display, sound 지원 추가
6. SCSI disk 지원 추가
7. SOURCE code 의 locale 변경 (UTF-8)
8. 2P 조이스틱 동시 지원 추가
9. 24kHz 디스플레이 모드 지원 추가 (15kHz/24kHz/31kHz)
10. 메뉴 UI에 현재 디스플레이 모드 표시
11. SDL2 오디오 출력 수정 (ADPCM/OPM)
12. 데스크톱 마우스 지원 추가
13. UTF-8 파일명 표시 지원 (iconv를 통한 Shift-JIS 변환)
14. Host Serial Port 연결 지원 (RS-232C)
15. 3MODE 디스플레이 모드별 동적 스프라이트 제한 계산 지원

몇몇 부분은 mame code 를 참고해서 개선되었습니다.

### 3MODE 스프라이트 제한

X68000의 하드웨어는 스캔라인당 표시 가능한 스프라이트 개수에 제한이 있습니다. 이 제한은 디스플레이 모드(HSYNC 클럭)에 따라 달라집니다:

| 디스플레이 모드 | HSYNC 클럭 | 스캔라인당 최대 스프라이트 |
|----------------|------------|---------------------------|
| 31kHz          | ~317       | 16개                      |
| 24kHz          | ~357       | 18개                      |
| 15kHz          | ~556       | 28개                      |

에뮬레이터는 실행되는 소프트웨어의 디스플레이 모드를 감지하여 스프라이트 제한을 동적으로 계산합니다. 단, 현재 이 제한은 호환성을 위해 비활성화되어 있습니다 (MAME과 동일한 동작).

## 구성 요소

PX68K는 다음의 구성 요소들로 만들어졌습니다:

- **WinX68k (통칭 케로피)**: 켄조 님(http://retropc.net/kenjo/)이 제작한 SHARP X68000 에뮬레이터
- **xkeropi**: NONAKA Kimihiro 님(http://www.asahi-net.or.jp/~aw9k-nnk/keropi/)이 케로피를 UNIX/X11 환경에서 동작하도록 이식한 버전
- **MC68000 MPU 에뮬레이터**: Stephane Dallongeville 님이 개발하고, NJ 님이 통합한 CPU 에뮬레이터
- **fmgen**: cisc 님(cisc@retropc.net)이 제작한 FM/PSG 음원 구현 (상세 내용은 소스 저장소의 fmgen/readme.txt 참조)
  - PX68K 구현을 위해 Sample 타입을 int32에서 int16으로 변경

## 0. 주의 사항

### 0.1 Raspberry Pi (Raspbian)

- 직접 컴파일해야 합니다.
- 컴파일에 대해서는 develop.txt를 참조하세요.
- 그 외 사항은 아래 Unix 버전 항목을 참조하세요.

## 1. 사전 준비

### BIOS ROM 파일

BIOS ROM 파일을 준비하세요. 파일명은 다음 중 하나이며, 대소문자는 구분하지 않습니다:

```
iplrom.dat, iplrom30.dat, iplromco.dat, iplromxv.dat
```

> **참고**: 특별한 이유가 없다면 iplrom30.dat 사용은 권장하지 않습니다. HD 이미지를 읽지 못하는 등의 문제가 보고되어 있습니다.

### 폰트 파일

폰트 파일을 준비하세요. 파일명은 다음 중 하나이며, 대소문자는 구분하지 않습니다:

```
cgrom.dat, cgrom.tmp
```

폰트 파일이 없는 경우, PC에서 WinX68k 고속판을 실행하면 cgrom.tmp가 생성되므로 그것을 사용하세요.

### 메모리 설정

Human68K는 실행되지만 게임이 시작되지 않는 경우, 기본 메모리 1MB로는 동작하지 않는 게임일 가능성이 높습니다.

HUMAN68K 부팅 후, 키보드 또는 소프트웨어 키보드로 `switch`를 입력하고 리턴 키를 눌러 SWITCH.X 명령을 실행하여 메모리를 설정하세요.

## 2. Linux 버전

### 2.1 파일 배치

BIOS ROM, 폰트 파일을 `~/.keropi`에 배치하세요.

### 2.2 이미지 파일

실행 파일 px68k-onionmixer와 같은 디렉토리에 이미지 파일을 배치하세요. 서브 디렉토리도 사용 가능합니다.

**지원 확장자**:
- FD 이미지: `.D88` `.88D` `.HDM` `.DUP` `.2HD` `.DIM` `.XDF` `.IMG`
- HD 이미지: `.HDF`

**명령줄 인자로 지정**:
```bash
./px68k-onionmixer hoge.xdf hogege.xdf   # 첫 번째: FDD0, 두 번째: FDD1
```

### 2.3 명령줄 옵션

```
--help              도움말 표시
--iplrom <파일>     IPL ROM 파일 경로 지정
--cgrom <파일>      CG ROM(폰트) 파일 경로 지정
--fdd0 <파일>       FDD 드라이브 0 디스크 이미지 지정
--fdd1 <파일>       FDD 드라이브 1 디스크 이미지 지정
--hdd0 <파일>       HDD 드라이브 0 이미지 지정 (SASI)
--hdd1 <파일>       HDD 드라이브 1 이미지 지정 (SASI)
--scsirom <파일>    외부 SCSI ROM 파일 경로 지정 (CZ-6BS1)
--scsiintrom <파일> 내부 SCSI ROM 파일 경로 지정
```

**사용 예**:
```bash
./px68k-onionmixer --iplrom iplrom.dat --cgrom cgrom.dat --fdd0 disk.xdf
./px68k-onionmixer --scsirom scsiexrom.dat --scsiintrom scsiinrom.dat
```

> **참고**: ROM 경로 옵션 (--iplrom, --cgrom, --scsirom, --scsiintrom)은 설정 파일에 저장되어 다음 실행 시 자동으로 사용됩니다.

### 2.4 메뉴 UI

- 이미지 파일 선택 및 각종 설정은 메뉴 UI를 사용합니다.
- **메뉴 진입/퇴장**: F12 키
- **풀스크린 전환**: F11 키
- **설정 적용**: 리턴 키
- **설정 해제**: ESC 키
- 설정은 [SYSTEM] → [QUIT]로 에뮬레이터 종료 시 저장됩니다.
- 설정 초기화: `~/.keropi/config` 파일 삭제

**디스플레이 모드 표시**: 메뉴 타이틀에 현재 디스플레이 주파수 모드가 표시됩니다.
```
PX68K-onionmixer V 0.20 [31kHz]
```
- `[15kHz]`: 표준 해상도 (256라인, TV 호환)
- `[24kHz]`: 중간 해상도 (512라인 인터레이스)
- `[31kHz]`: 고해상도 (512라인 프로그레시브, VGA 호환)

### 2.5 조이스틱

#### 하드웨어 조이스틱/게임패드

SDL2 GameController API를 사용하여 USB 게임패드를 지원합니다.

**2P 동시 지원**: 최대 2개의 조이스틱을 동시에 연결하여 사용할 수 있습니다.
- 첫 번째 조이스틱 → X68000 조이스틱 포트 0 (1P)
- 두 번째 조이스틱 → X68000 조이스틱 포트 1 (2P)

**버튼 매핑 (GameController 모드)**:
| 게임패드 | X68000 |
|----------|--------|
| A | TRG1 |
| B | TRG2 |
| X | TRG3 |
| Y | TRG4 |
| L1 (LB) | TRG5 |
| R1 (RB) | TRG6 |
| L2 (LT) | TRG7 |
| R2 (RT) | TRG8 |
| D-pad / 왼쪽 스틱 | 방향 |

#### JoyKey 모드

JoyKey 모드를 활성화하면:
- 키보드 방향키: 스틱 이동
- Z 키: 트리거 1
- X 키: 트리거 2

현재 UI가 없어 `~/.keropi/config`를 직접 수정해야 합니다.

### 2.6 키보드

- PC의 10키 부분의 NUMLOCK → X68000의 10키 CLR
- PC의 END → X68000의 UNDO

**특수 문자 입력**:

X68000은 일본어 키보드 배열을 사용하므로, 일부 특수 문자 입력 시 Shift 키와 함께 전송되어 의도하지 않은 문자가 입력될 수 있습니다. 이를 해결하기 위해 다음과 같은 커스텀 키 매핑이 적용되어 있습니다:

| 호스트 키 | X68000 입력 | 설명 |
|----------|-------------|------|
| `'` (작은따옴표) | `:` (콜론) | Shift 없이 콜론 입력 |
| `\` (백슬래시) | `_` (언더스코어) | Shift 없이 언더스코어 입력 |

> **참고**: 호스트에서 `Shift + ;`를 누르면 X68000에서 `+`가 입력되고, `Shift + -`를 누르면 `=`가 입력됩니다. 이는 X68000 일본어 키보드의 Shift 조합 때문입니다.

다음 X68000 키는 현재 지원되지 않습니다:
COPY, かな, ローマ字, コード入力, CAPS, 記号入力, 登録, HELP, ひらがな, XF1, XF2, XF3, XF4, XF5, 全角

### 2.7 사운드 출력

- 샘플링 주파수는 22050Hz 고정입니다.
- 현재 ADPCM과 OPM만 지원되며, 머큐리 유닛과 MIDI는 미지원입니다.

## 3. 메뉴 UI 상세

메뉴 구조와 각 항목의 내용입니다:

### [SYSTEM]

| 항목 | 설명 |
|------|------|
| RESET | 에뮬레이터를 리셋합니다. 이미지 파일 선택 후 실행하세요. |
| NMI RESET | NMI 리셋을 수행합니다. 일반적으로 사용하지 않습니다. |
| QUIT | 에뮬레이터를 종료하고 설정을 저장합니다. |

### [Joy/Mouse]

| 항목 | 설명 |
|------|------|
| Joystick | 조이스틱 모드. 하드웨어 조이스틱/게임패드 사용. |
| Mouse | 마우스 모드. PC 마우스를 X68000 마우스로 사용. |

> **참고**: Mouse 모드에서는 PC 마우스의 움직임과 좌/우 버튼이 X68000 마우스 입력으로 전달됩니다. 마우스 속도는 `~/.keropi/config`의 `MouseSpeed` 값(1-20)으로 조절할 수 있습니다.

### [FDD0] / [FDD1]

| 항목 | 설명 |
|------|------|
| 이미지 파일명 / -- no disk -- | 파일러 모드로 이동하여 이미지 선택 |
| EJECT | 이미지 파일을 해제합니다. |

### [HDD0] / [HDD1]

FDD와 동일합니다. SASI HDD 설정용입니다.

> HDD를 2대 이상 연결하는 경우, switch.x로 HD_MAX 값을 확인하고 필요시 변경하세요. 3대 이상 연결하려면 config 파일을 직접 편집하세요.

### [SERIAL] (테스트 필요)

X68000의 RS-232C 포트를 호스트 PC의 시리얼 포트에 연결합니다.

| 항목 | 설명 |
|------|------|
| -- disconnect -- | 현재 연결 해제 (연결 시 현재 장치명 표시) |
| /dev/ttyUSBx | USB-Serial 어댑터 장치 |
| /dev/ttySx | 내장 시리얼 포트 |
| /dev/ttyACMx | USB ACM 장치 (Arduino 등) |
| NOTHING | 사용 가능한 시리얼 포트 없음 |

**특징**:
- 메뉴 진입 시 호스트의 시리얼 장치를 동적으로 검색
- 설정 파일에 저장되지 않음 (매번 수동 연결 필요)
- 시리얼 미연결 시에도 에뮬레이터 정상 동작

**지원 장치**:
- `/dev/ttyS*`: 내장 시리얼 포트
- `/dev/ttyUSB*`: USB-Serial 어댑터 (FTDI, CH340 등)
- `/dev/ttyACM*`: USB CDC ACM 장치

> **참고**: 시리얼 장치에 접근하려면 사용자가 `dialout` 그룹에 속해야 할 수 있습니다:
> ```bash
> sudo usermod -a -G dialout $USER
> ```

### [Frame Skip]

프레임 스킵을 설정합니다:
- Auto: 기본 권장
- Full: 매우 빠른 PC용
- 1/2 ~ 1/60: 느린 PC용

### [Sound Rate]

사운드 출력 주파수를 설정합니다:
- 값이 클수록 고음질이지만 부하가 증가합니다.
- [No Sound]: 소리 끔

> 설정은 다음 실행 시 적용됩니다.

### [HwJoy Setting]

물리 패드 설정:
- Axis0(Left/Right): 좌우 이동 설정
- Axis1(Up/Down): 상하 이동 설정
- Button0~7: 트리거 1~8 버튼 설정

항목 선택 후 다시 선택 버튼을 누르면 설정 모드에 진입합니다.

### [No Wait Mode]

- [On]: 동기화 없이 전속력으로 실행 (실기보다 빠를 수 있음)
- [Off]: 기본값, 일반적으로 권장

### [JoyKey]

- [On]: 물리 키보드의 방향키가 조이스틱 이동, Z/X 키가 버튼으로 동작

## 4. TODO

- 물리 키보드 매핑 개선
- 로그 메시지 출력
- 판타지 존, 사라만다 슈팅 사운드 수정
- 메뉴/소프트키 키 리피트
- 성능 개선
- 시리얼 포트 기능 실기 테스트 (RS-232C 통신 검증)

## 5. 면책 조항

본 소프트웨어 사용으로 인한 어떠한 손해에 대해서도 저자는 책임을 지지 않습니다. 사용은 전적으로 본인 책임입니다.

## 저자 정보

**원본 px68k - hissorii / sakahi**
- 블로그: http://hissorii.blog45.fc2.com (히소리 닷컴)
- 블로그: http://emuhani.seesaa.net (에뮤하니 - Emulator Hacking 일기)
- GitHub: https://github.com/hissorii/px68k
- Twitter: @hissorii_com

**px68k-onionmixer 포크**
- GitHub: https://github.com/onionmixer/px68k-onionmixer
