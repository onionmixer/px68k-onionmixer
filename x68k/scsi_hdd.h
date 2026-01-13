/*
 * scsi_hdd.h - SCSI Hard Disk Device Emulation
 *
 * Implements a SCSI-1 hard disk device that connects to the SCSI bus.
 *
 * Reference: MAME src/devices/bus/nscsi/hd.cpp
 */

#ifndef _winx68k_scsi_hdd_h
#define _winx68k_scsi_hdd_h

#include "common.h"
#include "scsi_bus.h"

/* SCSI Command Codes */
#define SCSI_CMD_TEST_UNIT_READY  0x00
#define SCSI_CMD_REZERO_UNIT      0x01
#define SCSI_CMD_REQUEST_SENSE    0x03
#define SCSI_CMD_FORMAT_UNIT      0x04
#define SCSI_CMD_READ_6           0x08
#define SCSI_CMD_WRITE_6          0x0A
#define SCSI_CMD_SEEK_6           0x0B
#define SCSI_CMD_INQUIRY          0x12
#define SCSI_CMD_MODE_SELECT_6    0x15
#define SCSI_CMD_RESERVE          0x16
#define SCSI_CMD_RELEASE          0x17
#define SCSI_CMD_MODE_SENSE_6     0x1A
#define SCSI_CMD_START_STOP_UNIT  0x1B
#define SCSI_CMD_SEND_DIAGNOSTIC  0x1D
#define SCSI_CMD_READ_CAPACITY    0x25
#define SCSI_CMD_READ_10          0x28
#define SCSI_CMD_WRITE_10         0x2A
#define SCSI_CMD_SEEK_10          0x2B
#define SCSI_CMD_VERIFY_10        0x2F
#define SCSI_CMD_READ_BUFFER      0x3C
#define SCSI_CMD_WRITE_BUFFER     0x3B

/* SCSI Status Codes */
#define SCSI_STATUS_GOOD              0x00
#define SCSI_STATUS_CHECK_CONDITION   0x02
#define SCSI_STATUS_CONDITION_MET     0x04
#define SCSI_STATUS_BUSY              0x08
#define SCSI_STATUS_RESERVATION_CONFLICT 0x18

/* SCSI Sense Keys */
#define SCSI_SENSE_NO_SENSE           0x00
#define SCSI_SENSE_RECOVERED_ERROR    0x01
#define SCSI_SENSE_NOT_READY          0x02
#define SCSI_SENSE_MEDIUM_ERROR       0x03
#define SCSI_SENSE_HARDWARE_ERROR     0x04
#define SCSI_SENSE_ILLEGAL_REQUEST    0x05
#define SCSI_SENSE_UNIT_ATTENTION     0x06
#define SCSI_SENSE_DATA_PROTECT       0x07
#define SCSI_SENSE_BLANK_CHECK        0x08
#define SCSI_SENSE_VENDOR_SPECIFIC    0x09
#define SCSI_SENSE_COPY_ABORTED       0x0A
#define SCSI_SENSE_ABORTED_COMMAND    0x0B

/* Additional Sense Codes */
#define SCSI_ASC_NO_ADDITIONAL_SENSE  0x00
#define SCSI_ASC_LUN_NOT_READY        0x04
#define SCSI_ASC_INVALID_COMMAND      0x20
#define SCSI_ASC_LBA_OUT_OF_RANGE     0x21
#define SCSI_ASC_INVALID_FIELD_IN_CDB 0x24
#define SCSI_ASC_LUN_NOT_SUPPORTED    0x25
#define SCSI_ASC_MEDIUM_NOT_PRESENT   0x3A

/* HDD Device States */
typedef enum {
    HDD_STATE_IDLE = 0,
    HDD_STATE_COMMAND,
    HDD_STATE_DATA_IN,
    HDD_STATE_DATA_OUT,
    HDD_STATE_STATUS,
    HDD_STATE_MESSAGE_IN,
    HDD_STATE_MESSAGE_OUT
} HDD_STATE;

/* Maximum buffer size (64KB for multi-sector transfers) */
#define HDD_BUFFER_SIZE  65536

/* Command buffer size */
#define HDD_CMD_BUFFER_SIZE  16

/* SCSI HDD Structure */
typedef struct _SCSI_HDD {
    SCSI_DEVICE base;  /* Base SCSI device (inherited) */

    /* Image file */
    char image_path[MAX_PATH];
    void *image_fp;    /* FILEH handle */

    /* Disk geometry */
    DWORD total_sectors;
    DWORD bytes_per_sector;
    DWORD cylinders;
    DWORD heads;
    DWORD sectors_per_track;

    /* Device state */
    HDD_STATE state;
    int selected;

    /* Command buffer */
    BYTE cmd_buf[HDD_CMD_BUFFER_SIZE];
    int cmd_len;
    int cmd_pos;

    /* Data buffer */
    BYTE *data_buf;
    int data_len;
    int data_pos;
    int data_dir;  /* 0=write(out), 1=read(in) */

    /* Current operation */
    DWORD current_lba;
    DWORD current_blocks;

    /* Sense data (for REQUEST SENSE) */
    BYTE sense_key;
    BYTE sense_asc;   /* Additional Sense Code */
    BYTE sense_ascq;  /* Additional Sense Code Qualifier */
    DWORD sense_info; /* Information field (LBA) */

    /* Status and message */
    BYTE status;
    BYTE message;

    /* Unit ready state */
    int unit_ready;
    int media_changed;

} SCSI_HDD;

/* Initialization and Cleanup */
SCSI_HDD* SCSI_HDD_Create(void);
void SCSI_HDD_Destroy(SCSI_HDD *hdd);

/* Image file operations */
int SCSI_HDD_Open(SCSI_HDD *hdd, const char *path);
void SCSI_HDD_Close(SCSI_HDD *hdd);
int SCSI_HDD_IsOpened(SCSI_HDD *hdd);

/* Bus connection */
void SCSI_HDD_AttachToBus(SCSI_HDD *hdd, SCSI_BUS *bus, int id);
void SCSI_HDD_DetachFromBus(SCSI_HDD *hdd);

/* Get SCSI device pointer for bus operations */
SCSI_DEVICE* SCSI_HDD_GetDevice(SCSI_HDD *hdd);

#endif /* _winx68k_scsi_hdd_h */
