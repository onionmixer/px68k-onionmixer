/*
 * scsi.h - SCSI System for X68000
 *
 * Supports:
 * - Internal SCSI (X68000 Super/XVI/Compact/030) at $E96020-$E9603F
 * - External SCSI (CZ-6BS1) at $EA0000-$EA1FFF
 */

#ifndef _winx68k_scsi
#define _winx68k_scsi

#include "common.h"
#include "scsi_bus.h"
#include "scsi_spc.h"
#include "scsi_hdd.h"

/* Maximum SCSI devices per bus */
#define SCSI_MAX_DEVS  7  /* ID 0-6, ID 7 is reserved for SPC */

/* ROM sizes */
#define SCSI_INT_ROM_SIZE  0x2000  /* 8KB for internal SCSI ROM */
#define SCSI_EXT_ROM_SIZE  0x2000  /* 8KB for external SCSI ROM (CZ-6BS1) */

/* External SCSI board memory layout (CZ-6BS1) */
/* $EA0000-$EA001F: MB89352 registers (directly mapped) */
/* $EA0020-$EA1FFF: ROM area */
#define SCSI_EXT_REG_START  0xEA0000
#define SCSI_EXT_REG_END    0xEA001F
#define SCSI_EXT_ROM_START  0xEA0020
#define SCSI_EXT_ROM_END    0xEA1FFF

/* Internal SCSI register area */
/* $E96020-$E9603F: MB89352 registers */
#define SCSI_INT_REG_START  0xE96020
#define SCSI_INT_REG_END    0xE9603F

/* SCSI System Structure */
typedef struct {
    /* Internal SCSI (X68000 Super/XVI/030) */
    MB89352 internal_spc;
    SCSI_BUS internal_bus;
    int internal_enabled;

    /* External SCSI (CZ-6BS1) */
    MB89352 external_spc;
    SCSI_BUS external_bus;
    int external_enabled;

    /* SCSI devices - stored separately for internal/external */
    SCSI_HDD *int_hdd[SCSI_MAX_DEVS];
    SCSI_HDD *ext_hdd[SCSI_MAX_DEVS];

    /* Internal SCSI ROM (loaded from file like scsiinrom.dat) */
    BYTE internal_rom[SCSI_INT_ROM_SIZE];
    int internal_rom_loaded;

    /* External SCSI ROM (CZ-6BS1 ROM) */
    BYTE external_rom[SCSI_EXT_ROM_SIZE];
    int external_rom_loaded;

} SCSI_SYSTEM;

/* Global SCSI system instance */
extern SCSI_SYSTEM scsi_system;

/* Legacy compatibility - external SCSI ROM for existing code */
extern BYTE SCSIIPL[SCSI_EXT_ROM_SIZE];

/* Initialization and Cleanup */
void SCSI_Init(void);
void SCSI_Reset(void);
void SCSI_Cleanup(void);

/* ROM Loading */
int SCSI_LoadInternalROM(const char *path);
int SCSI_LoadExternalROM(const char *path);

/* HDD Image Management */
int SCSI_MountHDD(int bus_type, int id, const char *path);
void SCSI_UnmountHDD(int bus_type, int id);
int SCSI_IsHDDMounted(int bus_type, int id);

/* Bus type constants for MountHDD/UnmountHDD */
#define SCSI_BUS_INTERNAL  0
#define SCSI_BUS_EXTERNAL  1

/* Memory Access - External SCSI ($EA0000-$EA1FFF) */
BYTE FASTCALL SCSI_Read(DWORD adr);
void FASTCALL SCSI_Write(DWORD adr, BYTE data);

/* Memory Access - Internal SCSI ($E96020-$E9603F) */
BYTE FASTCALL SCSI_Internal_Read(DWORD adr);
void FASTCALL SCSI_Internal_Write(DWORD adr, BYTE data);

/* Clock/Timer Processing */
void SCSI_Exec(int cycles);

/* IRQ Handlers */
void SCSI_IRQ_Internal(int state, void *param);
void SCSI_IRQ_External(int state, void *param);

/* Enable/Disable SCSI subsystems */
void SCSI_EnableInternal(int enable);
void SCSI_EnableExternal(int enable);

/* Status checking */
int SCSI_IsInternalEnabled(void);
int SCSI_IsExternalEnabled(void);

#endif /* _winx68k_scsi */
