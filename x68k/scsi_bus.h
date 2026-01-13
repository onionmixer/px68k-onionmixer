/*
 * scsi_bus.h - SCSI Bus Emulation
 *
 * SCSI bus signal and device abstraction layer.
 * Implements wired-OR logic for multiple device connection.
 *
 * Reference: MAME src/devices/machine/nscsi_bus.h
 */

#ifndef _winx68k_scsi_bus_h
#define _winx68k_scsi_bus_h

#include "common.h"

/* SCSI Bus Signal Definitions */
#define SCSI_SIGNAL_IO   0x0001  /* I/O (Input/Output) */
#define SCSI_SIGNAL_CD   0x0002  /* C/D (Control/Data) */
#define SCSI_SIGNAL_MSG  0x0004  /* MSG (Message) */
#define SCSI_SIGNAL_BSY  0x0008  /* BSY (Busy) */
#define SCSI_SIGNAL_SEL  0x0010  /* SEL (Select) */
#define SCSI_SIGNAL_REQ  0x0020  /* REQ (Request) */
#define SCSI_SIGNAL_ACK  0x0040  /* ACK (Acknowledge) */
#define SCSI_SIGNAL_ATN  0x0080  /* ATN (Attention) */
#define SCSI_SIGNAL_RST  0x0100  /* RST (Reset) */
#define SCSI_SIGNAL_ALL  0x01FF

/* SCSI Phase Definitions (based on MSG, C/D, I/O signals) */
#define SCSI_PHASE_DATA_OUT  0
#define SCSI_PHASE_DATA_IN   (SCSI_SIGNAL_IO)
#define SCSI_PHASE_COMMAND   (SCSI_SIGNAL_CD)
#define SCSI_PHASE_STATUS    (SCSI_SIGNAL_CD | SCSI_SIGNAL_IO)
#define SCSI_PHASE_MSG_OUT   (SCSI_SIGNAL_MSG | SCSI_SIGNAL_CD)
#define SCSI_PHASE_MSG_IN    (SCSI_SIGNAL_MSG | SCSI_SIGNAL_CD | SCSI_SIGNAL_IO)
#define SCSI_PHASE_MASK      (SCSI_SIGNAL_MSG | SCSI_SIGNAL_CD | SCSI_SIGNAL_IO)

/* SCSI Device Types */
#define SCSI_DEVTYPE_NONE     0
#define SCSI_DEVTYPE_HDD      1
#define SCSI_DEVTYPE_CDROM    2
#define SCSI_DEVTYPE_MO       3

/* Maximum SCSI IDs (0-7) */
#define SCSI_ID_MAX          8

/* Forward declaration */
struct _SCSI_BUS;

/* SCSI Device Structure */
typedef struct _SCSI_DEVICE {
    int type;           /* Device type (HDD, CDROM, etc.) */
    int id;             /* SCSI ID (0-7) */
    void *private_data; /* Device-specific data */

    struct _SCSI_BUS *bus;  /* Connected bus */

    /* Callback functions */
    void (*reset)(struct _SCSI_DEVICE *dev);
    void (*bus_ctrl_changed)(struct _SCSI_DEVICE *dev);

    /* SCSI protocol callbacks */
    int (*select)(struct _SCSI_DEVICE *dev);
    void (*command)(struct _SCSI_DEVICE *dev, BYTE *cmd, int len);
    int (*data_read)(struct _SCSI_DEVICE *dev, BYTE *buf, int max_len);
    int (*data_write)(struct _SCSI_DEVICE *dev, const BYTE *buf, int len);
    BYTE (*get_status)(struct _SCSI_DEVICE *dev);
    BYTE (*get_message)(struct _SCSI_DEVICE *dev);
} SCSI_DEVICE;

/* SCSI Bus Structure */
typedef struct _SCSI_BUS {
    /* Combined bus signals (wired-OR result) */
    DWORD ctrl;         /* Current control signals */
    BYTE data;          /* Current data bus value */

    /* Per-device signal state */
    DWORD dev_ctrl[SCSI_ID_MAX];
    BYTE dev_data[SCSI_ID_MAX];

    /* Connected devices */
    SCSI_DEVICE *devices[SCSI_ID_MAX];
    int device_count;

    /* Bus state change callback (for SPC notification) */
    void (*ctrl_changed_callback)(struct _SCSI_BUS *bus, void *param);
    void *callback_param;
} SCSI_BUS;

/* Bus Management Functions */
void SCSI_BUS_Init(SCSI_BUS *bus);
void SCSI_BUS_Reset(SCSI_BUS *bus);
void SCSI_BUS_Cleanup(SCSI_BUS *bus);

/* Device Management */
void SCSI_BUS_AttachDevice(SCSI_BUS *bus, SCSI_DEVICE *dev, int id);
void SCSI_BUS_DetachDevice(SCSI_BUS *bus, int id);
SCSI_DEVICE* SCSI_BUS_GetDevice(SCSI_BUS *bus, int id);

/* Signal Control (called by devices/controller) */
void SCSI_BUS_SetCtrl(SCSI_BUS *bus, int id, DWORD value, DWORD mask);
void SCSI_BUS_SetData(SCSI_BUS *bus, int id, BYTE value);

/* Signal Read (returns wired-OR combined signals) */
DWORD SCSI_BUS_GetCtrl(SCSI_BUS *bus);
BYTE SCSI_BUS_GetData(SCSI_BUS *bus);

/* Callback registration */
void SCSI_BUS_SetCtrlChangedCallback(SCSI_BUS *bus,
    void (*callback)(SCSI_BUS *bus, void *param), void *param);

/* Utility functions */
int SCSI_BUS_IsBusFree(SCSI_BUS *bus);
const char* SCSI_BUS_GetPhaseName(int phase);

#endif /* _winx68k_scsi_bus_h */
