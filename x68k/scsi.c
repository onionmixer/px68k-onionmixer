/*
 * scsi.c - SCSI System Implementation for X68000
 *
 * Supports internal SCSI (Super/XVI/030) and external SCSI (CZ-6BS1).
 */

#include "common.h"
#include "fileio.h"
#include "winx68k.h"
#include "irqh.h"
#include "ioc.h"
#include "scsi.h"
#include <string.h>
#include <stdio.h>

/* Debug output */
#if 0
#define SCSI_DEBUG(...) printf("[SCSI] " __VA_ARGS__)
#else
#define SCSI_DEBUG(...)
#endif

/* Global SCSI system */
SCSI_SYSTEM scsi_system;

/* Legacy compatibility - points to external SCSI ROM */
BYTE SCSIIPL[SCSI_EXT_ROM_SIZE];

/* Default dummy ROM for external SCSI (CZ-6BS1) */
/* This provides minimal SCSI IOCS support when no real ROM is loaded */
static const BYTE SCSI_DEFAULT_ROM[] = {
    0x00, 0xea, 0x00, 0x34,           /* $ea0020 SCSI boot entry address */
    0x00, 0xea, 0x00, 0x36,           /* $ea0024 IOCS vector setup entry */
    0x00, 0xea, 0x00, 0x4a,           /* $ea0028 SCSI IOCS entry address */
    0x48, 0x75, 0x6d, 0x61,           /* $ea002c ID "Human68k" */
    0x6e, 0x36, 0x38, 0x6b,           /* $ea0030 */
    0x4e, 0x75,                       /* $ea0034 rts (boot entry - do nothing) */
    0x23, 0xfc, 0x00, 0xea, 0x00, 0x4a, /* $ea0036 move.l #$ea004a, $7d4.l */
    0x00, 0x00, 0x07, 0xd4,           /* $ea003c (IOCS $F5 vector setup) */
    0x74, 0xff,                       /* $ea0040 moveq #-1, d2 */
    0x4e, 0x75,                       /* $ea0042 rts */
    0x53, 0x43, 0x53, 0x49, 0x45, 0x58, /* $ea0044 ID "SCSIEX" */
    0x13, 0xc1, 0x00, 0xe9, 0xf8, 0x00, /* $ea004a move.b d1, $e9f800 (IOCS entry) */
    0x4e, 0x75,                       /* $ea0050 rts */
};

/* Bus control change callback for SPC */
static void scsi_bus_ctrl_changed(SCSI_BUS *bus, void *param);

/*
 * Initialize the default dummy ROM (byte-swapped for 68000)
 */
static void init_default_rom(BYTE *rom, size_t size)
{
    size_t i;
    BYTE tmp;

    memset(rom, 0, size);
    if (sizeof(SCSI_DEFAULT_ROM) <= size) {
        memcpy(&rom[0x20], SCSI_DEFAULT_ROM, sizeof(SCSI_DEFAULT_ROM));
    }

    /* Byte swap for 68000 word access */
    for (i = 0; i < size; i += 2) {
        tmp = rom[i];
        rom[i] = rom[i + 1];
        rom[i + 1] = tmp;
    }
}

/*
 * Initialize SCSI system
 */
void SCSI_Init(void)
{
    int i;

    SCSI_DEBUG("Init\n");

    memset(&scsi_system, 0, sizeof(SCSI_SYSTEM));

    /* Initialize buses */
    SCSI_BUS_Init(&scsi_system.internal_bus);
    SCSI_BUS_Init(&scsi_system.external_bus);

    /* Initialize SPCs */
    SPC_Init(&scsi_system.internal_spc);
    SPC_Init(&scsi_system.external_spc);

    /* Connect SPCs to buses (ID 7 is standard for initiator) */
    SPC_ConnectBus(&scsi_system.internal_spc, &scsi_system.internal_bus, 7);
    SPC_ConnectBus(&scsi_system.external_spc, &scsi_system.external_bus, 7);

    /* Set bus control change callbacks */
    SCSI_BUS_SetCtrlChangedCallback(&scsi_system.internal_bus,
        scsi_bus_ctrl_changed, &scsi_system.internal_spc);
    SCSI_BUS_SetCtrlChangedCallback(&scsi_system.external_bus,
        scsi_bus_ctrl_changed, &scsi_system.external_spc);

    /* Set IRQ callbacks */
    SPC_SetIRQCallback(&scsi_system.internal_spc, SCSI_IRQ_Internal, NULL);
    SPC_SetIRQCallback(&scsi_system.external_spc, SCSI_IRQ_External, NULL);

    /* Initialize device pointers */
    for (i = 0; i < SCSI_MAX_DEVS; i++) {
        scsi_system.int_hdd[i] = NULL;
        scsi_system.ext_hdd[i] = NULL;
    }

    /* Initialize ROMs with default dummy code */
    init_default_rom(scsi_system.internal_rom, SCSI_INT_ROM_SIZE);
    init_default_rom(scsi_system.external_rom, SCSI_EXT_ROM_SIZE);

    /* Copy external ROM to legacy SCSIIPL for compatibility */
    memcpy(SCSIIPL, scsi_system.external_rom, SCSI_EXT_ROM_SIZE);

    scsi_system.internal_rom_loaded = 0;
    scsi_system.external_rom_loaded = 0;

    /* Enable external SCSI by default (for compatibility) */
    scsi_system.external_enabled = 1;
    scsi_system.internal_enabled = 0;
}

/*
 * Reset SCSI system
 */
void SCSI_Reset(void)
{
    SCSI_DEBUG("Reset\n");

    /* Reset buses */
    SCSI_BUS_Reset(&scsi_system.internal_bus);
    SCSI_BUS_Reset(&scsi_system.external_bus);

    /* Reset SPCs */
    SPC_Reset(&scsi_system.internal_spc);
    SPC_Reset(&scsi_system.external_spc);
}

/*
 * Cleanup SCSI system
 */
void SCSI_Cleanup(void)
{
    int i;

    SCSI_DEBUG("Cleanup\n");

    /* Unmount and destroy all HDDs */
    for (i = 0; i < SCSI_MAX_DEVS; i++) {
        if (scsi_system.int_hdd[i]) {
            SCSI_HDD_Destroy(scsi_system.int_hdd[i]);
            scsi_system.int_hdd[i] = NULL;
        }
        if (scsi_system.ext_hdd[i]) {
            SCSI_HDD_Destroy(scsi_system.ext_hdd[i]);
            scsi_system.ext_hdd[i] = NULL;
        }
    }

    /* Cleanup buses */
    SCSI_BUS_Cleanup(&scsi_system.internal_bus);
    SCSI_BUS_Cleanup(&scsi_system.external_bus);

    /* Cleanup SPCs */
    SPC_Cleanup(&scsi_system.internal_spc);
    SPC_Cleanup(&scsi_system.external_spc);
}

/*
 * Load internal SCSI ROM
 */
int SCSI_LoadInternalROM(const char *path)
{
    FILEH fp;
    DWORD size;
    size_t i;
    BYTE tmp;

    if (!path || !path[0]) return 0;

    fp = File_Open(path);
    if (!fp) {
        SCSI_DEBUG("Failed to load internal ROM: %s\n", path);
        return 0;
    }

    /* Get file size */
    File_Seek(fp, 0, FSEEK_END);
    size = File_Seek(fp, 0, FSEEK_CUR);
    File_Seek(fp, 0, FSEEK_SET);

    if (size > SCSI_INT_ROM_SIZE) {
        size = SCSI_INT_ROM_SIZE;
    }

    memset(scsi_system.internal_rom, 0xFF, SCSI_INT_ROM_SIZE);
    File_Read(fp, scsi_system.internal_rom, size);
    File_Close(fp);

    /* Byte swap for 68000 */
    for (i = 0; i < SCSI_INT_ROM_SIZE; i += 2) {
        tmp = scsi_system.internal_rom[i];
        scsi_system.internal_rom[i] = scsi_system.internal_rom[i + 1];
        scsi_system.internal_rom[i + 1] = tmp;
    }

    scsi_system.internal_rom_loaded = 1;
    SCSI_DEBUG("Loaded internal ROM: %s (%d bytes)\n", path, size);

    return 1;
}

/*
 * Load external SCSI ROM (CZ-6BS1)
 */
int SCSI_LoadExternalROM(const char *path)
{
    FILEH fp;
    DWORD size;
    size_t i;
    BYTE tmp;

    if (!path || !path[0]) return 0;

    fp = File_Open(path);
    if (!fp) {
        SCSI_DEBUG("Failed to load external ROM: %s\n", path);
        return 0;
    }

    /* Get file size */
    File_Seek(fp, 0, FSEEK_END);
    size = File_Seek(fp, 0, FSEEK_CUR);
    File_Seek(fp, 0, FSEEK_SET);

    if (size > SCSI_EXT_ROM_SIZE) {
        size = SCSI_EXT_ROM_SIZE;
    }

    memset(scsi_system.external_rom, 0xFF, SCSI_EXT_ROM_SIZE);
    File_Read(fp, scsi_system.external_rom, size);
    File_Close(fp);

    /* Byte swap for 68000 */
    for (i = 0; i < SCSI_EXT_ROM_SIZE; i += 2) {
        tmp = scsi_system.external_rom[i];
        scsi_system.external_rom[i] = scsi_system.external_rom[i + 1];
        scsi_system.external_rom[i + 1] = tmp;
    }

    /* Update legacy SCSIIPL */
    memcpy(SCSIIPL, scsi_system.external_rom, SCSI_EXT_ROM_SIZE);

    scsi_system.external_rom_loaded = 1;
    SCSI_DEBUG("Loaded external ROM: %s (%d bytes)\n", path, size);

    return 1;
}

/*
 * Mount HDD image to SCSI bus
 */
int SCSI_MountHDD(int bus_type, int id, const char *path)
{
    SCSI_HDD **hdd_array;
    SCSI_BUS *bus;

    if (id < 0 || id >= SCSI_MAX_DEVS) return 0;
    if (!path || !path[0]) return 0;

    if (bus_type == SCSI_BUS_INTERNAL) {
        hdd_array = scsi_system.int_hdd;
        bus = &scsi_system.internal_bus;
    } else {
        hdd_array = scsi_system.ext_hdd;
        bus = &scsi_system.external_bus;
    }

    /* Unmount existing HDD at this ID */
    SCSI_UnmountHDD(bus_type, id);

    /* Create new HDD */
    hdd_array[id] = SCSI_HDD_Create();
    if (!hdd_array[id]) {
        SCSI_DEBUG("Failed to create HDD for ID %d\n", id);
        return 0;
    }

    /* Open image file */
    if (!SCSI_HDD_Open(hdd_array[id], path)) {
        SCSI_HDD_Destroy(hdd_array[id]);
        hdd_array[id] = NULL;
        return 0;
    }

    /* Attach to bus */
    SCSI_HDD_AttachToBus(hdd_array[id], bus, id);

    SCSI_DEBUG("Mounted HDD: bus=%d id=%d path=%s\n", bus_type, id, path);
    return 1;
}

/*
 * Unmount HDD from SCSI bus
 */
void SCSI_UnmountHDD(int bus_type, int id)
{
    SCSI_HDD **hdd_array;

    if (id < 0 || id >= SCSI_MAX_DEVS) return;

    if (bus_type == SCSI_BUS_INTERNAL) {
        hdd_array = scsi_system.int_hdd;
    } else {
        hdd_array = scsi_system.ext_hdd;
    }

    if (hdd_array[id]) {
        SCSI_HDD_DetachFromBus(hdd_array[id]);
        SCSI_HDD_Destroy(hdd_array[id]);
        hdd_array[id] = NULL;
        SCSI_DEBUG("Unmounted HDD: bus=%d id=%d\n", bus_type, id);
    }
}

/*
 * Check if HDD is mounted
 */
int SCSI_IsHDDMounted(int bus_type, int id)
{
    SCSI_HDD **hdd_array;

    if (id < 0 || id >= SCSI_MAX_DEVS) return 0;

    if (bus_type == SCSI_BUS_INTERNAL) {
        hdd_array = scsi_system.int_hdd;
    } else {
        hdd_array = scsi_system.ext_hdd;
    }

    return hdd_array[id] != NULL && SCSI_HDD_IsOpened(hdd_array[id]);
}

/*
 * External SCSI Read ($EA0000-$EA1FFF)
 */
BYTE FASTCALL SCSI_Read(DWORD adr)
{
    BYTE ret = 0xFF;

    if (!scsi_system.external_enabled) {
        return 0xFF;
    }

    adr &= 0x1FFF;

    if (adr < 0x20) {
        /* MB89352 registers ($EA0000-$EA001F) */
        /* Registers are at even addresses, odd addresses return 0xFF */
        if ((adr & 1) == 0) {
            ret = SPC_ReadReg(&scsi_system.external_spc, adr >> 1);
        }
    } else {
        /* ROM area ($EA0020-$EA1FFF) */
        ret = scsi_system.external_rom[(adr ^ 1) & (SCSI_EXT_ROM_SIZE - 1)];
    }

    SCSI_DEBUG("ExtRead: $%06X -> $%02X\n", 0xEA0000 + adr, ret);
    return ret;
}

/*
 * External SCSI Write ($EA0000-$EA1FFF)
 */
void FASTCALL SCSI_Write(DWORD adr, BYTE data)
{
    if (!scsi_system.external_enabled) {
        return;
    }

    adr &= 0x1FFF;

    if (adr < 0x20) {
        /* MB89352 registers ($EA0000-$EA001F) */
        if ((adr & 1) == 0) {
            SCSI_DEBUG("ExtWrite: $%06X <- $%02X\n", 0xEA0000 + adr, data);
            SPC_WriteReg(&scsi_system.external_spc, adr >> 1, data);
        }
    }
    /* ROM area is read-only */
}

/*
 * Internal SCSI Read ($E96020-$E9603F)
 */
BYTE FASTCALL SCSI_Internal_Read(DWORD adr)
{
    BYTE ret = 0xFF;

    if (!scsi_system.internal_enabled) {
        return 0xFF;
    }

    /* Map $E96020-$E9603F to register 0-15 */
    adr = (adr - SCSI_INT_REG_START) & 0x1F;

    /* Registers are at odd addresses for internal SCSI */
    if (adr & 1) {
        ret = SPC_ReadReg(&scsi_system.internal_spc, adr >> 1);
    }

    SCSI_DEBUG("IntRead: $%06X -> $%02X\n", SCSI_INT_REG_START + adr, ret);
    return ret;
}

/*
 * Internal SCSI Write ($E96020-$E9603F)
 */
void FASTCALL SCSI_Internal_Write(DWORD adr, BYTE data)
{
    if (!scsi_system.internal_enabled) {
        return;
    }

    /* Map $E96020-$E9603F to register 0-15 */
    adr = (adr - SCSI_INT_REG_START) & 0x1F;

    /* Registers are at odd addresses for internal SCSI */
    if (adr & 1) {
        SCSI_DEBUG("IntWrite: $%06X <- $%02X\n", SCSI_INT_REG_START + adr, data);
        SPC_WriteReg(&scsi_system.internal_spc, adr >> 1, data);
    }
}

/*
 * Clock processing
 */
void SCSI_Exec(int cycles)
{
    if (scsi_system.internal_enabled) {
        SPC_Exec(&scsi_system.internal_spc, cycles);
    }
    if (scsi_system.external_enabled) {
        SPC_Exec(&scsi_system.external_spc, cycles);
    }
}

/*
 * Internal SCSI IRQ callback
 * Uses IOC like SASI
 */
void SCSI_IRQ_Internal(int state, void *param)
{
    (void)param;

    if (state) {
        /* Assert IRQ through IOC */
        IOC_IntStat |= 0x10;
        if (IOC_IntStat & 8) {
            IRQH_Int(1, NULL);  /* IRQ1 through IOC */
        }
        SCSI_DEBUG("Internal IRQ asserted\n");
    } else {
        /* Deassert IRQ */
        IOC_IntStat &= ~0x10;
        SCSI_DEBUG("Internal IRQ deasserted\n");
    }
}

/*
 * External SCSI IRQ callback (vector)
 */
static DWORD FASTCALL SCSI_ExtIRQ_Vector(BYTE irq)
{
    IRQH_IRQCallBack(irq);
    /* External SCSI uses vector $F6 (246) */
    return 0xF6;
}

/*
 * External SCSI IRQ callback
 * Uses separate IRQ (typically IRQ2, vector $F6)
 */
void SCSI_IRQ_External(int state, void *param)
{
    (void)param;

    if (state) {
        /* Assert IRQ2 for external SCSI */
        IRQH_Int(2, &SCSI_ExtIRQ_Vector);
        SCSI_DEBUG("External IRQ asserted\n");
    }
    /* Note: IRQ deassertion is handled by IRQ callback system */
}

/*
 * Enable/disable internal SCSI
 */
void SCSI_EnableInternal(int enable)
{
    scsi_system.internal_enabled = enable ? 1 : 0;
    SCSI_DEBUG("Internal SCSI %s\n", enable ? "enabled" : "disabled");
}

/*
 * Enable/disable external SCSI
 */
void SCSI_EnableExternal(int enable)
{
    scsi_system.external_enabled = enable ? 1 : 0;
    SCSI_DEBUG("External SCSI %s\n", enable ? "enabled" : "disabled");
}

/*
 * Check if internal SCSI is enabled
 */
int SCSI_IsInternalEnabled(void)
{
    return scsi_system.internal_enabled;
}

/*
 * Check if external SCSI is enabled
 */
int SCSI_IsExternalEnabled(void)
{
    return scsi_system.external_enabled;
}

/*
 * Bus control change callback
 * Called when bus signals change, forwards to SPC
 */
static void scsi_bus_ctrl_changed(SCSI_BUS *bus, void *param)
{
    MB89352 *spc = (MB89352*)param;

    if (spc) {
        SPC_BusCtrlChanged(spc);
    }
    (void)bus;
}
