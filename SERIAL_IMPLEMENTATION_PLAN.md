# X68000 Serial Port Implementation Plan

## 개요

X68000 에뮬레이터에 RS-232C 시리얼 포트 지원을 추가하는 구현 계획입니다.
호스트 Linux 시스템의 시리얼 포트 (/dev/ttyS*, /dev/ttyUSB*, /dev/ttyACM*)를
X68000의 RS-232C 포트에 연결하여 실제 시리얼 통신이 가능하도록 합니다.

## 구현 범위

### 1. Z8530 SCC 전체 구현

현재 마우스 전용으로 구현된 SCC를 확장하여 RS-232C 통신을 지원합니다.

#### 1.1 레지스터 모델

```
Channel A (RS-232C):
- WR0-WR15: Write Registers
- RR0-RR15: Read Registers

주요 레지스터:
- WR3: 수신 파라미터 (RX Enable, Bits/Char)
- WR4: TX/RX 모드 (Parity, Stop Bits, Clock Mode)
- WR5: 송신 파라미터 (TX Enable, DTR, RTS, Bits/Char)
- WR9: Master Interrupt Control
- RR0: 버퍼 상태 (RX Available, TX Empty, CTS, DCD)
- RR1: Special Receive Condition
```

#### 1.2 지원 Baud Rate

- 300, 600, 1200, 2400, 4800, 9600, 19200 bps

#### 1.3 데이터 포맷

- Data bits: 5, 6, 7, 8
- Parity: None, Odd, Even
- Stop bits: 1, 1.5, 2

### 2. 호스트 시리얼 포트 추상화 (serial.c/h)

#### 2.1 새 파일 생성

```
x68k/serial.c  - 호스트 시리얼 포트 구현
x68k/serial.h  - 인터페이스 정의
```

#### 2.2 API 설계

```c
// 시리얼 포트 구조체
typedef struct {
    int fd;                     // File descriptor
    char device[64];            // Device path (e.g., /dev/ttyUSB0)
    int baudrate;               // Current baud rate
    int databits;               // 5, 6, 7, 8
    int stopbits;               // 1, 2
    int parity;                 // 0=None, 1=Odd, 2=Even
    int rts;                    // RTS line state
    int dtr;                    // DTR line state
    int cts;                    // CTS line state (read)
    int dcd;                    // DCD line state (read)
    int is_open;                // Connection status
} HostSerial;

// 함수 인터페이스
int  Serial_Init(void);
void Serial_Cleanup(void);
int  Serial_Open(const char *device);
void Serial_Close(void);
int  Serial_SetConfig(int baudrate, int databits, int stopbits, int parity);
int  Serial_Write(BYTE data);
int  Serial_Read(BYTE *data);
int  Serial_GetStatus(void);      // CTS, DCD 상태
void Serial_SetRTS(int state);
void Serial_SetDTR(int state);

// 디바이스 열거
int  Serial_EnumDevices(char devices[][64], int max_devices);
const char* Serial_GetCurrentDevice(void);
```

#### 2.3 Linux termios 설정

```c
struct termios {
    tcflag_t c_iflag;   // Input modes
    tcflag_t c_oflag;   // Output modes
    tcflag_t c_cflag;   // Control modes (baud, bits, parity)
    tcflag_t c_lflag;   // Local modes
    cc_t c_cc[NCCS];    // Special characters
};
```

### 3. F12 메뉴 UI 확장 (winui.c)

#### 3.1 새 메뉴 항목 추가

```
[SYSTEM]
[Joy/Mouse]
[FDD0]
[FDD1]
[HDD0]
[HDD1]
[SERIAL]          <-- 새 메뉴
  ├─ Device: /dev/ttyUSB0  (선택된 디바이스)
  ├─ -- disconnect --      (연결 해제)
  ├─ /dev/ttyS0
  ├─ /dev/ttyS1
  ├─ /dev/ttyUSB0
  ├─ /dev/ttyUSB1
  ├─ /dev/ttyACM0
  └─ ...
[Frame Skip]
[Sound Rate]
...
```

#### 3.2 메뉴 구조

```c
// 시리얼 디바이스 목록 구조체
#define MAX_SERIAL_DEVICES 16
struct serial_menu {
    char devices[MAX_SERIAL_DEVICES][64];  // 디바이스 경로
    int num_devices;                        // 발견된 디바이스 수
    int selected;                           // 선택된 인덱스 (-1: 연결 안됨)
};

// 메뉴 함수
static void menu_serial_device(int v);
static void menu_create_serial_list(void);
```

#### 3.3 디바이스 열거 방법

```c
// /dev/ttyS*, /dev/ttyUSB*, /dev/ttyACM* 검색
const char *serial_patterns[] = {
    "/dev/ttyS",
    "/dev/ttyUSB",
    "/dev/ttyACM",
    NULL
};

// 각 패턴에 대해 0-15 번호 검사
// 존재하고 접근 가능한 디바이스만 목록에 추가
```

### 4. SCC-Serial 연동 (scc.c 수정)

#### 4.1 Channel A 데이터 흐름

```
X68000 Software
      ↓ Write to 0xE98007
SCC Channel A TX Register
      ↓ SCC_Write()
Serial_Write()
      ↓
Host /dev/ttyUSBx
      ↓
External Device
```

```
External Device
      ↓
Host /dev/ttyUSBx
      ↓ Serial_Read()
SCC Channel A RX Register
      ↓ SCC_Read() from 0xE98007
X68000 Software
```

#### 4.2 인터럽트 처리

```c
// RX 인터럽트: 데이터 수신 시
// TX 인터럽트: 송신 버퍼 비었을 때
// External Status: CTS/DCD 변경 시

void SCC_CheckSerial(void) {
    // 주기적으로 호출 (메인 루프에서)
    // 호스트 시리얼에서 데이터 확인
    // RX 버퍼에 데이터 있으면 인터럽트 발생
}
```

### 5. 설정 저장 (prop.c/h)

#### 5.1 Config 구조체 확장

```c
typedef struct {
    // ... 기존 설정 ...

    // Serial Port 설정
    char SerialDevice[64];      // 선택된 디바이스 경로
    int  SerialBaudRate;        // Baud rate (기본: 9600)
    int  SerialEnabled;         // 시리얼 포트 활성화 여부
} Win68Conf;
```

#### 5.2 설정 파일 항목

```
[Serial]
Device=/dev/ttyUSB0
BaudRate=9600
Enabled=1
```

### 6. 파일 구조

```
x68k/
├── scc.c          (수정) - Z8530 전체 구현
├── scc.h          (수정) - 구조체 확장
├── serial.c       (신규) - 호스트 시리얼 추상화
├── serial.h       (신규) - 시리얼 인터페이스
x11/
├── winui.c        (수정) - 시리얼 메뉴 추가
├── prop.c         (수정) - 설정 저장/로드
├── prop.h         (수정) - Config 구조체 확장
Makefile           (수정) - serial.c 추가
```

## 구현 순서

### Phase 1: 호스트 시리얼 추상화 레이어
1. serial.h 인터페이스 정의
2. serial.c 구현
   - Serial_Init/Cleanup
   - Serial_Open/Close
   - Serial_SetConfig
   - Serial_Read/Write
   - Serial_EnumDevices
3. 단독 테스트

### Phase 2: F12 메뉴 UI
1. winui.c에 SERIAL 메뉴 항목 추가
2. 디바이스 열거 및 표시
3. 디바이스 선택 처리
4. prop.c/h에 설정 저장 추가

### Phase 3: SCC 확장
1. scc.h 구조체 정의 확장
2. scc.c Channel A 레지스터 구현
3. TX/RX 데이터 처리
4. 인터럽트 생성

### Phase 4: 통합 및 테스트
1. SCC-Serial 연동
2. 메인 루프에서 Serial 체크 추가
3. Human68K에서 테스트
4. 다양한 baud rate 테스트

## 예상 코드량

| 파일 | 예상 줄 수 | 설명 |
|------|-----------|------|
| serial.h | 50 | 인터페이스 정의 |
| serial.c | 350 | 호스트 시리얼 구현 |
| scc.c (수정) | +400 | Channel A 구현 |
| scc.h (수정) | +30 | 구조체 확장 |
| winui.c (수정) | +150 | 메뉴 UI |
| prop.c (수정) | +30 | 설정 저장 |
| prop.h (수정) | +5 | Config 확장 |
| **합계** | **~1,015** | |

## 테스트 계획

### 1. 단위 테스트
- serial.c 디바이스 열거 테스트
- serial.c 연결/해제 테스트
- serial.c 송수신 테스트 (loopback)

### 2. 통합 테스트
- F12 메뉴에서 디바이스 목록 표시 확인
- 디바이스 선택 및 연결 확인
- X68000 소프트웨어에서 시리얼 통신 테스트

### 3. 호환성 테스트
- 다양한 USB-Serial 어댑터
- 다양한 baud rate
- 장시간 통신 안정성

## 주의사항

1. **권한**: /dev/ttyUSB* 접근에 dialout 그룹 권한 필요
   ```
   sudo usermod -a -G dialout $USER
   ```

2. **비동기 I/O**: Serial_Read()는 non-blocking으로 구현
   - O_NONBLOCK 플래그 사용
   - 또는 select()/poll() 사용

3. **버퍼 오버플로우**: RX 버퍼 관리 주의
   - 높은 baud rate에서 데이터 손실 방지

4. **핫플러그**: USB 디바이스 연결/해제 처리
   - 디바이스 사라짐 감지
   - 자동 재연결 또는 에러 처리
