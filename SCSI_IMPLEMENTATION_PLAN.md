# PX68K SCSI 컨트롤러 구현 개발 계획

## 개요

MAME의 X68000 SCSI 구현을 참조하여 px68k에 완전한 SCSI 지원을 추가합니다.

### 목표
- MB89352 SCSI 컨트롤러 (SPC) 에뮬레이션
- 내장 SCSI (X68000 Super/XVI/Compact/030) 지원
- 외장 SCSI (CZ-6BS1) 지원
- SCSI 하드디스크 이미지 지원

### 참조 코드
- MAME `src/devices/machine/mb87030.cpp` (MB89352 컨트롤러)
- MAME `src/devices/bus/nscsi/hd.cpp` (SCSI 하드디스크)
- MAME `src/devices/bus/x68k/x68k_scsiext.cpp` (외장 SCSI)
- MAME `src/mame/sharp/x68k.cpp` (통합)

---

## Phase 1: 기반 구조 구현

### 1.1 SCSI 버스 에뮬레이션

**새 파일: `x68k/scsi_bus.h`**

```c
#ifndef _winx68k_scsi_bus_h
#define _winx68k_scsi_bus_h

#include "common.h"

// SCSI 버스 신호 정의
#define SCSI_SIGNAL_IO   0x0001  // I/O (Input/Output)
#define SCSI_SIGNAL_CD   0x0002  // C/D (Control/Data)
#define SCSI_SIGNAL_MSG  0x0004  // MSG (Message)
#define SCSI_SIGNAL_BSY  0x0008  // BSY (Busy)
#define SCSI_SIGNAL_SEL  0x0010  // SEL (Select)
#define SCSI_SIGNAL_REQ  0x0020  // REQ (Request)
#define SCSI_SIGNAL_ACK  0x0040  // ACK (Acknowledge)
#define SCSI_SIGNAL_ATN  0x0080  // ATN (Attention)
#define SCSI_SIGNAL_RST  0x0100  // RST (Reset)
#define SCSI_SIGNAL_ALL  0x01FF

// SCSI 페이즈 정의
#define SCSI_PHASE_DATA_OUT  0
#define SCSI_PHASE_DATA_IN   (SCSI_SIGNAL_IO)
#define SCSI_PHASE_COMMAND   (SCSI_SIGNAL_CD)
#define SCSI_PHASE_STATUS    (SCSI_SIGNAL_CD | SCSI_SIGNAL_IO)
#define SCSI_PHASE_MSG_OUT   (SCSI_SIGNAL_MSG | SCSI_SIGNAL_CD)
#define SCSI_PHASE_MSG_IN    (SCSI_SIGNAL_MSG | SCSI_SIGNAL_CD | SCSI_SIGNAL_IO)
#define SCSI_PHASE_MASK      (SCSI_SIGNAL_MSG | SCSI_SIGNAL_CD | SCSI_SIGNAL_IO)

// SCSI 디바이스 타입
#define SCSI_DEVICE_NONE     0
#define SCSI_DEVICE_HDD      1
#define SCSI_DEVICE_CDROM    2

// 최대 SCSI ID
#define SCSI_ID_MAX          8

// SCSI 디바이스 구조체
typedef struct _SCSI_DEVICE {
    int type;
    int id;
    void *data;  // 디바이스별 데이터

    // 콜백 함수
    void (*reset)(struct _SCSI_DEVICE *dev);
    void (*ctrl_changed)(struct _SCSI_DEVICE *dev);
    int (*command)(struct _SCSI_DEVICE *dev, BYTE *cmd, int len);
    int (*read_data)(struct _SCSI_DEVICE *dev, BYTE *buf, int len);
    int (*write_data)(struct _SCSI_DEVICE *dev, BYTE *buf, int len);
} SCSI_DEVICE;

// SCSI 버스 구조체
typedef struct {
    DWORD ctrl;           // 현재 컨트롤 신호
    BYTE data;            // 현재 데이터 버스

    SCSI_DEVICE *devices[SCSI_ID_MAX];
    int device_count;

    // 각 디바이스별 신호 상태
    DWORD dev_ctrl[SCSI_ID_MAX];
    BYTE dev_data[SCSI_ID_MAX];
} SCSI_BUS;

// 함수 프로토타입
void SCSI_BUS_Init(SCSI_BUS *bus);
void SCSI_BUS_Reset(SCSI_BUS *bus);
void SCSI_BUS_AttachDevice(SCSI_BUS *bus, SCSI_DEVICE *dev, int id);
void SCSI_BUS_DetachDevice(SCSI_BUS *bus, int id);

void SCSI_BUS_SetCtrl(SCSI_BUS *bus, int id, DWORD value, DWORD mask);
void SCSI_BUS_SetData(SCSI_BUS *bus, int id, BYTE value);
DWORD SCSI_BUS_GetCtrl(SCSI_BUS *bus);
BYTE SCSI_BUS_GetData(SCSI_BUS *bus);

#endif
```

**새 파일: `x68k/scsi_bus.c`**

```c
// SCSI 버스 에뮬레이션
// Wired-OR 로직으로 여러 디바이스의 신호를 결합

#include "scsi_bus.h"

void SCSI_BUS_Init(SCSI_BUS *bus)
{
    int i;
    memset(bus, 0, sizeof(SCSI_BUS));
    for (i = 0; i < SCSI_ID_MAX; i++) {
        bus->devices[i] = NULL;
        bus->dev_ctrl[i] = 0;
        bus->dev_data[i] = 0;
    }
}

void SCSI_BUS_Reset(SCSI_BUS *bus)
{
    int i;
    bus->ctrl = 0;
    bus->data = 0;
    for (i = 0; i < SCSI_ID_MAX; i++) {
        bus->dev_ctrl[i] = 0;
        bus->dev_data[i] = 0;
        if (bus->devices[i] && bus->devices[i]->reset) {
            bus->devices[i]->reset(bus->devices[i]);
        }
    }
}

// Wired-OR: 모든 디바이스의 신호를 OR 결합
static void SCSI_BUS_UpdateSignals(SCSI_BUS *bus)
{
    int i;
    DWORD new_ctrl = 0;
    BYTE new_data = 0;

    for (i = 0; i < SCSI_ID_MAX; i++) {
        new_ctrl |= bus->dev_ctrl[i];
        new_data |= bus->dev_data[i];
    }

    bus->ctrl = new_ctrl;
    bus->data = new_data;
}

void SCSI_BUS_SetCtrl(SCSI_BUS *bus, int id, DWORD value, DWORD mask)
{
    if (id < 0 || id >= SCSI_ID_MAX) return;

    bus->dev_ctrl[id] = (bus->dev_ctrl[id] & ~mask) | (value & mask);
    SCSI_BUS_UpdateSignals(bus);
}

void SCSI_BUS_SetData(SCSI_BUS *bus, int id, BYTE value)
{
    if (id < 0 || id >= SCSI_ID_MAX) return;

    bus->dev_data[id] = value;
    SCSI_BUS_UpdateSignals(bus);
}

DWORD SCSI_BUS_GetCtrl(SCSI_BUS *bus)
{
    return bus->ctrl;
}

BYTE SCSI_BUS_GetData(SCSI_BUS *bus)
{
    return bus->data;
}

void SCSI_BUS_AttachDevice(SCSI_BUS *bus, SCSI_DEVICE *dev, int id)
{
    if (id < 0 || id >= SCSI_ID_MAX) return;

    bus->devices[id] = dev;
    dev->id = id;
    bus->device_count++;
}

void SCSI_BUS_DetachDevice(SCSI_BUS *bus, int id)
{
    if (id < 0 || id >= SCSI_ID_MAX) return;

    if (bus->devices[id]) {
        bus->devices[id] = NULL;
        bus->device_count--;
    }
}
```

---

## Phase 2: MB89352 SPC 컨트롤러 구현

### 2.1 SPC 헤더 파일

**새 파일: `x68k/scsi_spc.h`**

```c
#ifndef _winx68k_scsi_spc_h
#define _winx68k_scsi_spc_h

#include "common.h"
#include "scsi_bus.h"

// MB89352 레지스터 오프셋
#define SPC_REG_BDID  0x00  // Bus Device ID
#define SPC_REG_SCTL  0x01  // SPC Control
#define SPC_REG_SCMD  0x02  // SPC Command
#define SPC_REG_INTS  0x04  // Interrupt Status
#define SPC_REG_PSNS  0x05  // Phase Sense (Read)
#define SPC_REG_SDGC  0x05  // Diag Control (Write)
#define SPC_REG_SSTS  0x06  // SPC Status
#define SPC_REG_SERR  0x07  // SPC Error
#define SPC_REG_PCTL  0x08  // Phase Control
#define SPC_REG_MBC   0x09  // Modified Byte Counter
#define SPC_REG_DREG  0x0A  // Data Register
#define SPC_REG_TEMP  0x0B  // Temporary Register
#define SPC_REG_TCH   0x0C  // Transfer Counter High
#define SPC_REG_TCM   0x0D  // Transfer Counter Middle
#define SPC_REG_TCL   0x0E  // Transfer Counter Low

// SCTL 비트 정의
#define SCTL_INT_ENABLE        0x01
#define SCTL_RESELECT_ENABLE   0x02
#define SCTL_SELECT_ENABLE     0x04
#define SCTL_PARITY_ENABLE     0x08
#define SCTL_ARBITRATION_ENABLE 0x10
#define SCTL_DIAG_MODE         0x20
#define SCTL_CONTROL_RESET     0x40
#define SCTL_RESET_AND_DISABLE 0x80

// SCMD 명령 비트 정의
#define SCMD_TERM_MODE         0x01
#define SCMD_PRG_XFER          0x04
#define SCMD_INTERCEPT_XFER    0x08
#define SCMD_RST_OUT           0x10
#define SCMD_CMD_MASK          0xE0
#define SCMD_CMD_BUS_RELEASE   0x00
#define SCMD_CMD_SELECT        0x20
#define SCMD_CMD_RESET_ATN     0x40
#define SCMD_CMD_SET_ATN       0x60
#define SCMD_CMD_TRANSFER      0x80
#define SCMD_CMD_TRANSFER_PAUSE 0xA0
#define SCMD_CMD_RESET_ACK_REQ 0xC0
#define SCMD_CMD_SET_ACK_REQ   0xE0

// INTS 비트 정의
#define INTS_RESET_CONDITION   0x01
#define INTS_SPC_HARD_ERR      0x02
#define INTS_SPC_TIMEOUT       0x04
#define INTS_SERVICE_REQUIRED  0x08
#define INTS_COMMAND_COMPLETE  0x10
#define INTS_DISCONNECTED      0x20
#define INTS_RESELECTED        0x40
#define INTS_SELECTED          0x80

// SSTS 비트 정의
#define SSTS_DREQ_EMPTY        0x01
#define SSTS_DREQ_FULL         0x02
#define SSTS_TC_ZERO           0x04
#define SSTS_SCSI_RST          0x08
#define SSTS_XFER_IN_PROGRESS  0x10
#define SSTS_SPC_BUSY          0x20
#define SSTS_TARG_CONNECTED    0x40
#define SSTS_INIT_CONNECTED    0x80

// PCTL 비트 정의
#define PCTL_PHASE_MASK        0x07
#define PCTL_BUS_FREE_IE       0x80

// SPC 상태 머신
typedef enum {
    SPC_STATE_IDLE = 0,
    SPC_STATE_ARBITRATION_WAIT_BUS_FREE,
    SPC_STATE_ARBITRATION_ASSERT_BSY,
    SPC_STATE_ARBITRATION_WAIT,
    SPC_STATE_ARBITRATION_ASSERT_SEL,
    SPC_STATE_SELECTION_WAIT_BUS_FREE,
    SPC_STATE_SELECTION_ASSERT_ID,
    SPC_STATE_SELECTION_ASSERT_SEL,
    SPC_STATE_SELECTION_WAIT_BSY,
    SPC_STATE_SELECTION,
    SPC_STATE_TRANSFER_WAIT_REQ,
    SPC_STATE_TRANSFER_SEND_DATA,
    SPC_STATE_TRANSFER_RECV_DATA,
    SPC_STATE_TRANSFER_SEND_ACK,
    SPC_STATE_TRANSFER_WAIT_DEASSERT_REQ,
    SPC_STATE_TRANSFER_DEASSERT_ACK,
    SPC_STATE_TRANSFER_WAIT_FIFO_EMPTY
} SPC_STATE;

// FIFO 사이즈
#define SPC_FIFO_SIZE  8

// MB89352 SPC 구조체
typedef struct {
    // 레지스터
    BYTE bdid;      // Bus Device ID (0-7)
    BYTE sctl;      // SPC Control
    BYTE scmd;      // SPC Command
    BYTE ints;      // Interrupt Status
    BYTE sdgc;      // Diag Control
    BYTE ssts;      // SPC Status
    BYTE serr;      // SPC Error
    BYTE pctl;      // Phase Control
    BYTE mbc;       // Modified Byte Counter
    BYTE dreg;      // Data Register
    BYTE temp;      // Temporary Register
    DWORD tc;       // Transfer Counter (24-bit)

    // 상태 머신
    SPC_STATE state;
    SPC_STATE delay_state;
    int scsi_phase;
    DWORD scsi_ctrl;

    // FIFO 버퍼
    BYTE fifo[SPC_FIFO_SIZE];
    int fifo_read;
    int fifo_write;
    int fifo_count;

    // 플래그
    int send_atn_during_selection;
    int dma_transfer;
    int irq_state;

    // SCSI 버스 연결
    SCSI_BUS *bus;
    int bus_id;  // 이 SPC의 SCSI ID (보통 7)

    // 타이머 (클럭 카운트)
    DWORD timer_count;
    DWORD delay_count;

    // IRQ 콜백
    void (*irq_callback)(int state);

} MB89352;

// 함수 프로토타입
void SPC_Init(MB89352 *spc, SCSI_BUS *bus, int id);
void SPC_Reset(MB89352 *spc);
void SPC_SetIRQCallback(MB89352 *spc, void (*callback)(int));

BYTE SPC_ReadReg(MB89352 *spc, DWORD reg);
void SPC_WriteReg(MB89352 *spc, DWORD reg, BYTE data);

// DMA 인터페이스
BYTE SPC_DMA_Read(MB89352 *spc);
void SPC_DMA_Write(MB89352 *spc, BYTE data);

// 버스 이벤트
void SPC_BusCtrlChanged(MB89352 *spc);

// 타이머 처리 (매 에뮬레이션 프레임 호출)
void SPC_Tick(MB89352 *spc, int cycles);

#endif
```

### 2.2 SPC 구현 파일

**새 파일: `x68k/scsi_spc.c`**

주요 구현 내용:
1. 레지스터 읽기/쓰기
2. 상태 머신 (Arbitration → Selection → Transfer)
3. FIFO 관리
4. IRQ 생성
5. DMA 전송

(약 600-800줄 예상)

---

## Phase 3: SCSI 하드디스크 디바이스 구현

### 3.1 SCSI HDD 헤더

**새 파일: `x68k/scsi_hdd.h`**

```c
#ifndef _winx68k_scsi_hdd_h
#define _winx68k_scsi_hdd_h

#include "common.h"
#include "scsi_bus.h"

// SCSI 명령 코드
#define SCSI_CMD_TEST_UNIT_READY  0x00
#define SCSI_CMD_REZERO_UNIT      0x01
#define SCSI_CMD_REQUEST_SENSE    0x03
#define SCSI_CMD_FORMAT_UNIT      0x04
#define SCSI_CMD_READ_6           0x08
#define SCSI_CMD_WRITE_6          0x0A
#define SCSI_CMD_SEEK_6           0x0B
#define SCSI_CMD_INQUIRY          0x12
#define SCSI_CMD_MODE_SELECT_6    0x15
#define SCSI_CMD_MODE_SENSE_6     0x1A
#define SCSI_CMD_START_STOP_UNIT  0x1B
#define SCSI_CMD_READ_CAPACITY    0x25
#define SCSI_CMD_READ_10          0x28
#define SCSI_CMD_WRITE_10         0x2A
#define SCSI_CMD_SEEK_10          0x2B
#define SCSI_CMD_VERIFY_10        0x2F
#define SCSI_CMD_READ_BUFFER      0x3C
#define SCSI_CMD_WRITE_BUFFER     0x3B

// SCSI 스테이터스
#define SCSI_STATUS_GOOD          0x00
#define SCSI_STATUS_CHECK_CONDITION 0x02
#define SCSI_STATUS_BUSY          0x08

// SCSI Sense Key
#define SCSI_SENSE_NO_SENSE       0x00
#define SCSI_SENSE_RECOVERED_ERROR 0x01
#define SCSI_SENSE_NOT_READY      0x02
#define SCSI_SENSE_MEDIUM_ERROR   0x03
#define SCSI_SENSE_HARDWARE_ERROR 0x04
#define SCSI_SENSE_ILLEGAL_REQUEST 0x05
#define SCSI_SENSE_UNIT_ATTENTION 0x06
#define SCSI_SENSE_DATA_PROTECT   0x07
#define SCSI_SENSE_ABORTED_COMMAND 0x0B

// HDD 상태
typedef enum {
    HDD_STATE_IDLE = 0,
    HDD_STATE_COMMAND,
    HDD_STATE_DATA_IN,
    HDD_STATE_DATA_OUT,
    HDD_STATE_STATUS,
    HDD_STATE_MESSAGE_IN
} HDD_STATE;

// SCSI HDD 구조체
typedef struct {
    SCSI_DEVICE base;  // 기본 SCSI 디바이스 (상속)

    // 이미지 파일
    char image_path[MAX_PATH];
    FILE *image_fp;

    // 디스크 정보
    DWORD total_sectors;
    DWORD bytes_per_sector;
    DWORD cylinders;
    DWORD heads;
    DWORD sectors_per_track;

    // 상태
    HDD_STATE state;

    // 현재 명령
    BYTE cmd_buf[16];
    int cmd_len;
    int cmd_pos;

    // 데이터 버퍼
    BYTE data_buf[65536];  // 최대 64KB
    int data_len;
    int data_pos;

    // Sense 데이터
    BYTE sense_key;
    BYTE sense_asc;   // Additional Sense Code
    BYTE sense_ascq;  // Additional Sense Code Qualifier

    // 스테이터스
    BYTE status;
    BYTE message;

    // 연결된 버스
    SCSI_BUS *bus;

} SCSI_HDD;

// 함수 프로토타입
SCSI_HDD* SCSI_HDD_Create(void);
void SCSI_HDD_Destroy(SCSI_HDD *hdd);

int SCSI_HDD_Open(SCSI_HDD *hdd, const char *path);
void SCSI_HDD_Close(SCSI_HDD *hdd);

void SCSI_HDD_AttachToBus(SCSI_HDD *hdd, SCSI_BUS *bus, int id);

#endif
```

### 3.2 SCSI HDD 구현

**새 파일: `x68k/scsi_hdd.c`**

주요 구현 내용:
1. 이미지 파일 열기/닫기
2. SCSI 명령 파싱 및 실행
3. 섹터 읽기/쓰기
4. Inquiry, Read Capacity 등 지원

(약 400-500줄 예상)

---

## Phase 4: 메모리 맵 및 시스템 통합

### 4.1 전역 SCSI 시스템 관리

**새 파일: `x68k/scsi.h`** (기존 파일 확장)

```c
#ifndef _winx68k_scsi_h
#define _winx68k_scsi_h

#include "common.h"
#include "scsi_bus.h"
#include "scsi_spc.h"
#include "scsi_hdd.h"

// SCSI 시스템 전역 구조체
typedef struct {
    // 내장 SCSI (X68030/XVI/Super)
    MB89352 internal_spc;
    SCSI_BUS internal_bus;
    int internal_enabled;

    // 외장 SCSI (CZ-6BS1)
    MB89352 external_spc;
    SCSI_BUS external_bus;
    int external_enabled;

    // SCSI 디바이스들
    SCSI_HDD *hdd[SCSI_ID_MAX];

    // ROM
    BYTE internal_rom[0x2000];  // 8KB
    BYTE external_rom[0x2000];  // 8KB
    int internal_rom_loaded;
    int external_rom_loaded;

} SCSI_SYSTEM;

extern SCSI_SYSTEM scsi_system;

// 초기화/종료
void SCSI_SystemInit(void);
void SCSI_SystemReset(void);
void SCSI_SystemCleanup(void);

// ROM 로드
int SCSI_LoadInternalROM(const char *path);
int SCSI_LoadExternalROM(const char *path);

// HDD 이미지 설정
int SCSI_MountHDD(int id, const char *path, int internal);
void SCSI_UnmountHDD(int id, int internal);

// 메모리 액세스 (내장 SCSI: $E96020-$E9603F)
BYTE FASTCALL SCSI_Internal_Read(DWORD adr);
void FASTCALL SCSI_Internal_Write(DWORD adr, BYTE data);

// 메모리 액세스 (외장 SCSI: $EA0000-$EA1FFF)
BYTE FASTCALL SCSI_External_Read(DWORD adr);
void FASTCALL SCSI_External_Write(DWORD adr, BYTE data);

// IRQ
void SCSI_IRQ_Internal(int state);
void SCSI_IRQ_External(int state);

// 클럭 처리
void SCSI_Tick(int cycles);

#endif
```

### 4.2 메모리 맵 수정

**파일: `x68k/mem_wrap.c` 수정**

```c
// 메모리 읽기 테이블에 SCSI 추가
// $E96020-$E9603F: 내장 SCSI (MB89352)
// $EA0000-$EA1FFF: 외장 SCSI (ROM + MB89352)
// $FC0000-$FDFFFF: 내장 SCSI ROM

// MemReadTable, MemWriteTable 수정 필요
```

### 4.3 IRQ 통합

**파일: `x68k/irqh.c` 수정**

```c
// SCSI IRQ 벡터
// 내장 SCSI: IOC 경유 (기존 SASI와 동일)
// 외장 SCSI: IRQ2, 벡터 $F6
```

---

## Phase 5: 설정 및 UI 통합

### 5.1 설정 파일 확장

**파일: `x11/prop.h` 수정**

```c
// Config 구조체에 추가
typedef struct {
    // ... 기존 필드 ...

    // SCSI 설정
    char SCSIInternalROM[MAX_PATH];  // 내장 SCSI ROM 경로
    char SCSIExternalROM[MAX_PATH];  // 외장 SCSI ROM 경로
    char SCSIImage[8][MAX_PATH];     // SCSI HDD 이미지 (ID 0-7)
    int SCSIEnabled;                  // SCSI 활성화 여부
} Config_t;
```

### 5.2 커맨드라인 옵션 추가

**파일: `x11/winx68k.cpp` 수정**

```c
// 새 옵션
--scsirom PATH      내장 SCSI ROM 경로
--scsi0 PATH        SCSI ID 0 HDD 이미지
--scsi1 PATH        SCSI ID 1 HDD 이미지
...
--scsi6 PATH        SCSI ID 6 HDD/CD 이미지
```

---

## 파일 목록 요약

### 새로 생성할 파일
| 파일 | 설명 | 예상 줄수 |
|------|------|----------|
| `x68k/scsi_bus.h` | SCSI 버스 헤더 | 100 |
| `x68k/scsi_bus.c` | SCSI 버스 구현 | 150 |
| `x68k/scsi_spc.h` | MB89352 헤더 | 200 |
| `x68k/scsi_spc.c` | MB89352 구현 | 800 |
| `x68k/scsi_hdd.h` | SCSI HDD 헤더 | 100 |
| `x68k/scsi_hdd.c` | SCSI HDD 구현 | 500 |
| **총계** | | **~1,850** |

### 수정할 파일
| 파일 | 수정 내용 |
|------|----------|
| `x68k/scsi.h` | SCSI 시스템 통합 헤더로 확장 |
| `x68k/scsi.c` | SCSI 시스템 통합 구현 |
| `x68k/mem_wrap.c` | 메모리 맵 추가 |
| `x68k/irqh.c` | SCSI IRQ 처리 |
| `x11/prop.h` | 설정 구조체 확장 |
| `x11/prop.c` | 설정 로드/저장 |
| `x11/winx68k.cpp` | 커맨드라인 옵션 |
| `Makefile` | 새 파일 추가 |

---

## 구현 일정 (예상)

| Phase | 내용 | 예상 공수 |
|-------|------|----------|
| Phase 1 | SCSI 버스 구현 | 1일 |
| Phase 2 | MB89352 SPC 구현 | 3-4일 |
| Phase 3 | SCSI HDD 구현 | 2일 |
| Phase 4 | 시스템 통합 | 2일 |
| Phase 5 | 설정/UI | 1일 |
| 테스트/디버그 | | 2-3일 |
| **총계** | | **11-13일** |

---

## 테스트 계획

### 필요한 테스트 리소스
1. **SCSI ROM 이미지**
   - `scsiinrom.dat` (내장 SCSI)
   - `scsiexrom.bin` (외장 SCSI CZ-6BS1)

2. **SCSI HDD 이미지**
   - 빈 이미지 생성 도구
   - Human68K 포맷된 이미지

3. **테스트 소프트웨어**
   - Human68K + SCSI 드라이버
   - FORMAT.X (SCSI 포맷)
   - SX-Window (SCSI 지원 버전)

### 테스트 케이스
1. SCSI ROM 인식 확인
2. SCSI HDD 인식 (INQUIRY)
3. HDD 읽기/쓰기 기본 동작
4. Human68K 부팅 (SCSI HDD에서)
5. 파일 복사 안정성
6. 다중 SCSI 디바이스

---

## 리스크 및 대응

| 리스크 | 영향 | 대응 방안 |
|--------|------|----------|
| SCSI ROM 미확보 | 테스트 불가 | 더미 ROM 생성 또는 MAME에서 추출 |
| 타이밍 문제 | 동작 불안정 | MAME 구현 정밀 분석 |
| IRQ 처리 버그 | 시스템 멈춤 | 단계별 디버그 로그 |
| HDD 이미지 호환성 | 부팅 실패 | 다양한 이미지 포맷 지원 |

---

## 참고 자료

1. MAME 소스 코드
   - `src/devices/machine/mb87030.cpp`
   - `src/devices/bus/nscsi/hd.cpp`
   - `src/mame/sharp/x68k.cpp`

2. X68000 기술 자료
   - Inside X68000 (메모리 맵, I/O)
   - MB89352 데이터시트

3. SCSI 규격
   - SCSI-1/SCSI-2 명령 세트

