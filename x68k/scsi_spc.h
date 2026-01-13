/*
 * scsi_spc.h - Fujitsu MB89352 SCSI Protocol Controller Emulation
 *
 * The MB89352 is a SCSI-1 controller chip used in:
 * - X68000 Super/XVI/Compact (internal SCSI at $E96020-$E9603F)
 * - X68000 CZ-6BS1 external SCSI board (at $EA0000-$EA001F)
 *
 * Reference: MAME src/devices/machine/mb87030.cpp
 */

#ifndef _winx68k_scsi_spc_h
#define _winx68k_scsi_spc_h

#include "common.h"
#include "scsi_bus.h"

/* MB89352 Register Offsets (directly accessible, no TMOD/EXBF) */
#define SPC_REG_BDID  0x00  /* Bus Device ID */
#define SPC_REG_SCTL  0x01  /* SPC Control */
#define SPC_REG_SCMD  0x02  /* SPC Command */
/* 0x03: TMOD not present on MB89352 */
#define SPC_REG_INTS  0x04  /* Interrupt Status */
#define SPC_REG_PSNS  0x05  /* Phase Sense (Read) */
#define SPC_REG_SDGC  0x05  /* Diag Control (Write) */
#define SPC_REG_SSTS  0x06  /* SPC Status (Read only) */
#define SPC_REG_SERR  0x07  /* SPC Error (Read only) */
#define SPC_REG_PCTL  0x08  /* Phase Control */
#define SPC_REG_MBC   0x09  /* Modified Byte Counter (Read only) */
#define SPC_REG_DREG  0x0A  /* Data Register */
#define SPC_REG_TEMP  0x0B  /* Temporary Register */
#define SPC_REG_TCH   0x0C  /* Transfer Counter High */
#define SPC_REG_TCM   0x0D  /* Transfer Counter Middle */
#define SPC_REG_TCL   0x0E  /* Transfer Counter Low */
/* 0x0F: EXBF not present on MB89352 */

/* SCTL (SPC Control) Register Bits */
#define SCTL_INT_ENABLE        0x01  /* Interrupt enable */
#define SCTL_RESELECT_ENABLE   0x02  /* Reselection enable */
#define SCTL_SELECT_ENABLE     0x04  /* Selection enable */
#define SCTL_PARITY_ENABLE     0x08  /* Parity check enable */
#define SCTL_ARBITRATION_ENABLE 0x10 /* Arbitration enable */
#define SCTL_DIAG_MODE         0x20  /* Diagnostic mode */
#define SCTL_CONTROL_RESET     0x40  /* Control reset */
#define SCTL_RESET_AND_DISABLE 0x80  /* Reset and disable */

/* SCMD (SPC Command) Register Bits */
#define SCMD_TERM_MODE         0x01  /* Termination mode */
#define SCMD_PRG_XFER          0x04  /* Program transfer mode */
#define SCMD_INTERCEPT_XFER    0x08  /* Intercept transfer */
#define SCMD_RST_OUT           0x10  /* Reset output */
#define SCMD_CMD_MASK          0xE0  /* Command mask */
#define SCMD_CMD_BUS_RELEASE   0x00  /* Bus release */
#define SCMD_CMD_SELECT        0x20  /* Select */
#define SCMD_CMD_RESET_ATN     0x40  /* Reset ATN */
#define SCMD_CMD_SET_ATN       0x60  /* Set ATN */
#define SCMD_CMD_TRANSFER      0x80  /* Transfer */
#define SCMD_CMD_TRANSFER_PAUSE 0xA0 /* Transfer pause */
#define SCMD_CMD_RESET_ACK_REQ 0xC0  /* Reset ACK/REQ */
#define SCMD_CMD_SET_ACK_REQ   0xE0  /* Set ACK/REQ */

/* INTS (Interrupt Status) Register Bits */
#define INTS_RESET_CONDITION   0x01  /* Reset condition */
#define INTS_SPC_HARD_ERR      0x02  /* SPC hard error */
#define INTS_SPC_TIMEOUT       0x04  /* Selection timeout */
#define INTS_SERVICE_REQUIRED  0x08  /* Service required */
#define INTS_COMMAND_COMPLETE  0x10  /* Command complete */
#define INTS_DISCONNECTED      0x20  /* Disconnected */
#define INTS_RESELECTED        0x40  /* Reselected */
#define INTS_SELECTED          0x80  /* Selected */

/* SSTS (SPC Status) Register Bits */
#define SSTS_DREQ_EMPTY        0x01  /* Data register empty */
#define SSTS_DREQ_FULL         0x02  /* Data register full */
#define SSTS_TC_ZERO           0x04  /* Transfer counter zero */
#define SSTS_SCSI_RST          0x08  /* SCSI reset */
#define SSTS_XFER_IN_PROGRESS  0x10  /* Transfer in progress */
#define SSTS_SPC_BUSY          0x20  /* SPC busy */
#define SSTS_TARG_CONNECTED    0x40  /* Target connected */
#define SSTS_INIT_CONNECTED    0x80  /* Initiator connected */

/* SERR (SPC Error) Register Bits */
#define SERR_OFFSET_ERROR      0x01  /* Offset error */
#define SERR_SHORT_PERIOD      0x02  /* Short transfer period */
#define SERR_PHASE_ERROR       0x04  /* Phase error */
#define SERR_TC_P_ERROR        0x08  /* TC parity error */
#define SERR_XFER_OUT          0x20  /* Transfer out (MB89352 only) */
#define SERR_DATA_ERROR_SPC    0x40  /* Data error (SPC) */
#define SERR_DATA_ERROR_SCSI   0x80  /* Data error (SCSI) */

/* SDGC (Diagnostic Control) Register Bits */
#define SDGC_DIAG_IO           0x01  /* Diagnostic I/O */
#define SDGC_DIAG_CD           0x02  /* Diagnostic C/D */
#define SDGC_DIAG_MSG          0x04  /* Diagnostic MSG */
#define SDGC_DIAG_BSY          0x08  /* Diagnostic BSY */
#define SDGC_XFER_ENABLE       0x20  /* Transfer enable (MB89352) */
#define SDGC_DIAG_ACK          0x40  /* Diagnostic ACK */
#define SDGC_DIAG_REQ          0x80  /* Diagnostic REQ */

/* PCTL (Phase Control) Register Bits */
#define PCTL_PHASE_MASK        0x07  /* Phase mask */
#define PCTL_BUS_FREE_IE       0x80  /* Bus free interrupt enable */

/* SPC State Machine States */
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

/* FIFO Size */
#define SPC_FIFO_SIZE  8

/* MB89352 SPC Structure */
typedef struct {
    /* Registers */
    BYTE bdid;      /* Bus Device ID (0-7) */
    BYTE sctl;      /* SPC Control */
    BYTE scmd;      /* SPC Command */
    BYTE ints;      /* Interrupt Status */
    BYTE sdgc;      /* Diag Control */
    BYTE ssts;      /* SPC Status */
    BYTE serr;      /* SPC Error */
    BYTE pctl;      /* Phase Control */
    BYTE mbc;       /* Modified Byte Counter */
    BYTE dreg;      /* Data Register */
    BYTE temp;      /* Temporary Register */
    DWORD tc;       /* Transfer Counter (24-bit) */

    /* State Machine */
    SPC_STATE state;
    SPC_STATE delay_state;
    int scsi_phase;
    DWORD scsi_ctrl;  /* Local control for diag mode */

    /* FIFO Buffer */
    BYTE fifo[SPC_FIFO_SIZE];
    int fifo_read_pos;
    int fifo_write_pos;
    int fifo_count;

    /* Flags */
    int send_atn_during_selection;
    int dma_transfer;
    int irq_state;

    /* SCSI Bus Connection */
    SCSI_BUS *bus;
    int bus_id;     /* This SPC's SCSI ID (usually 7) */

    /* Timing (in system clock cycles) */
    DWORD timer_count;
    DWORD timer_target;
    DWORD delay_timer_count;
    DWORD delay_timer_target;
    DWORD bus_free_timer;

    /* IRQ Callback */
    void (*irq_callback)(int state, void *param);
    void *irq_param;

    /* DRQ Callback (for DMA) */
    void (*drq_callback)(int state, void *param);
    void *drq_param;

} MB89352;

/* Initialization and Cleanup */
void SPC_Init(MB89352 *spc);
void SPC_Reset(MB89352 *spc);
void SPC_Cleanup(MB89352 *spc);

/* Bus Connection */
void SPC_ConnectBus(MB89352 *spc, SCSI_BUS *bus, int id);

/* Callbacks */
void SPC_SetIRQCallback(MB89352 *spc, void (*callback)(int, void*), void *param);
void SPC_SetDRQCallback(MB89352 *spc, void (*callback)(int, void*), void *param);

/* Register Access */
BYTE SPC_ReadReg(MB89352 *spc, DWORD reg);
void SPC_WriteReg(MB89352 *spc, DWORD reg, BYTE data);

/* DMA Interface */
BYTE SPC_DMA_Read(MB89352 *spc);
void SPC_DMA_Write(MB89352 *spc, BYTE data);

/* Bus Event (called when bus signals change) */
void SPC_BusCtrlChanged(MB89352 *spc);

/* Clock/Timer Processing (call periodically) */
void SPC_Exec(MB89352 *spc, int cycles);

/* Debug */
const char* SPC_GetStateName(SPC_STATE state);

#endif /* _winx68k_scsi_spc_h */
