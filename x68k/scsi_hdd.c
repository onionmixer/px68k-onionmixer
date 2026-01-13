/*
 * scsi_hdd.c - SCSI Hard Disk Device Emulation
 *
 * Implements a SCSI-1 hard disk device.
 *
 * Reference: MAME src/devices/bus/nscsi/hd.cpp
 */

#include "scsi_hdd.h"
#include "fileio.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Debug output */
#if 0
#define HDD_DEBUG(...) printf("[SCSI_HDD] " __VA_ARGS__)
#else
#define HDD_DEBUG(...)
#endif

/* Default disk geometry for different image sizes */
typedef struct {
    DWORD min_size;
    DWORD max_size;
    DWORD cylinders;
    DWORD heads;
    DWORD sectors;
} DISK_GEOMETRY;

static const DISK_GEOMETRY default_geometries[] = {
    /* ~20MB */
    { 0, 21*1024*1024, 615, 4, 17 },
    /* ~40MB */
    { 21*1024*1024, 42*1024*1024, 615, 6, 17 },
    /* ~80MB */
    { 42*1024*1024, 85*1024*1024, 820, 6, 17 },
    /* ~100MB */
    { 85*1024*1024, 110*1024*1024, 1024, 8, 17 },
    /* ~200MB */
    { 110*1024*1024, 210*1024*1024, 1024, 12, 17 },
    /* ~500MB */
    { 210*1024*1024, 520*1024*1024, 1024, 16, 32 },
    /* ~1GB */
    { 520*1024*1024, 1100*1024*1024, 2048, 16, 32 },
    /* ~2GB (max for SCSI-1 with 21-bit LBA) */
    { 1100*1024*1024, 0xFFFFFFFF, 4096, 16, 32 },
    { 0, 0, 0, 0, 0 }
};

/* Forward declarations */
static void hdd_reset(SCSI_DEVICE *dev);
static void hdd_bus_ctrl_changed(SCSI_DEVICE *dev);
static int hdd_select(SCSI_DEVICE *dev);
static void hdd_command(SCSI_DEVICE *dev, BYTE *cmd, int len);
static int hdd_data_read(SCSI_DEVICE *dev, BYTE *buf, int max_len);
static int hdd_data_write(SCSI_DEVICE *dev, const BYTE *buf, int len);
static BYTE hdd_get_status(SCSI_DEVICE *dev);
static BYTE hdd_get_message(SCSI_DEVICE *dev);

/* Internal functions */
static void hdd_set_sense(SCSI_HDD *hdd, BYTE key, BYTE asc, BYTE ascq);
static void hdd_clear_sense(SCSI_HDD *hdd);
static void hdd_start_command(SCSI_HDD *hdd);
static void hdd_execute_command(SCSI_HDD *hdd);
static int hdd_read_sector(SCSI_HDD *hdd, DWORD lba, BYTE *buf);
static int hdd_write_sector(SCSI_HDD *hdd, DWORD lba, const BYTE *buf);

/* Command handlers */
static void cmd_test_unit_ready(SCSI_HDD *hdd);
static void cmd_rezero_unit(SCSI_HDD *hdd);
static void cmd_request_sense(SCSI_HDD *hdd);
static void cmd_format_unit(SCSI_HDD *hdd);
static void cmd_read_6(SCSI_HDD *hdd);
static void cmd_write_6(SCSI_HDD *hdd);
static void cmd_seek_6(SCSI_HDD *hdd);
static void cmd_inquiry(SCSI_HDD *hdd);
static void cmd_mode_select_6(SCSI_HDD *hdd);
static void cmd_mode_sense_6(SCSI_HDD *hdd);
static void cmd_start_stop_unit(SCSI_HDD *hdd);
static void cmd_read_capacity(SCSI_HDD *hdd);
static void cmd_read_10(SCSI_HDD *hdd);
static void cmd_write_10(SCSI_HDD *hdd);
static void cmd_verify_10(SCSI_HDD *hdd);

/*
 * Calculate disk geometry from file size
 */
static void hdd_calc_geometry(SCSI_HDD *hdd, DWORD file_size)
{
    int i;
    const DISK_GEOMETRY *geo;

    /* Default: 512 bytes per sector */
    hdd->bytes_per_sector = 512;

    /* Find appropriate geometry */
    for (i = 0; default_geometries[i].max_size != 0; i++) {
        geo = &default_geometries[i];
        if (file_size >= geo->min_size && file_size < geo->max_size) {
            hdd->cylinders = geo->cylinders;
            hdd->heads = geo->heads;
            hdd->sectors_per_track = geo->sectors;
            hdd->total_sectors = file_size / hdd->bytes_per_sector;
            HDD_DEBUG("Geometry: C=%d H=%d S=%d, Total=%d sectors\n",
                hdd->cylinders, hdd->heads, hdd->sectors_per_track,
                hdd->total_sectors);
            return;
        }
    }

    /* Fallback: generic geometry */
    hdd->total_sectors = file_size / hdd->bytes_per_sector;
    hdd->sectors_per_track = 32;
    hdd->heads = 16;
    hdd->cylinders = hdd->total_sectors / (hdd->heads * hdd->sectors_per_track);
    if (hdd->cylinders < 1) hdd->cylinders = 1;
}

/*
 * Create a new SCSI HDD instance
 */
SCSI_HDD* SCSI_HDD_Create(void)
{
    SCSI_HDD *hdd;

    hdd = (SCSI_HDD*)malloc(sizeof(SCSI_HDD));
    if (!hdd) return NULL;

    memset(hdd, 0, sizeof(SCSI_HDD));

    /* Allocate data buffer */
    hdd->data_buf = (BYTE*)malloc(HDD_BUFFER_SIZE);
    if (!hdd->data_buf) {
        free(hdd);
        return NULL;
    }

    /* Setup base SCSI device callbacks */
    hdd->base.type = SCSI_DEVTYPE_HDD;
    hdd->base.private_data = hdd;
    hdd->base.reset = hdd_reset;
    hdd->base.bus_ctrl_changed = hdd_bus_ctrl_changed;
    hdd->base.select = hdd_select;
    hdd->base.command = hdd_command;
    hdd->base.data_read = hdd_data_read;
    hdd->base.data_write = hdd_data_write;
    hdd->base.get_status = hdd_get_status;
    hdd->base.get_message = hdd_get_message;

    /* Initialize state */
    hdd->state = HDD_STATE_IDLE;
    hdd->unit_ready = 0;
    hdd->media_changed = 0;
    hdd->message = 0x00;  /* COMMAND COMPLETE */

    HDD_DEBUG("Created\n");
    return hdd;
}

/*
 * Destroy SCSI HDD instance
 */
void SCSI_HDD_Destroy(SCSI_HDD *hdd)
{
    if (!hdd) return;

    SCSI_HDD_Close(hdd);

    if (hdd->data_buf) {
        free(hdd->data_buf);
    }

    free(hdd);
    HDD_DEBUG("Destroyed\n");
}

/*
 * Open image file
 */
int SCSI_HDD_Open(SCSI_HDD *hdd, const char *path)
{
    FILEH fp;
    DWORD file_size;

    if (!hdd || !path || !path[0]) return 0;

    /* Close existing image */
    SCSI_HDD_Close(hdd);

    /* Open image file */
    fp = File_Open(path);
    if (!fp) {
        HDD_DEBUG("Failed to open: %s\n", path);
        return 0;
    }

    /* Get file size */
    File_Seek(fp, 0, FSEEK_END);
    file_size = File_Seek(fp, 0, FSEEK_CUR);
    File_Seek(fp, 0, FSEEK_SET);

    if (file_size < 512) {
        HDD_DEBUG("Image too small: %d bytes\n", file_size);
        File_Close(fp);
        return 0;
    }

    /* Store file handle and path */
    hdd->image_fp = fp;
    strncpy(hdd->image_path, path, MAX_PATH - 1);
    hdd->image_path[MAX_PATH - 1] = '\0';

    /* Calculate geometry */
    hdd_calc_geometry(hdd, file_size);

    /* Mark as ready */
    hdd->unit_ready = 1;
    hdd->media_changed = 1;  /* Will clear on first access */
    hdd_clear_sense(hdd);

    HDD_DEBUG("Opened: %s (%d MB, %d sectors)\n",
        path, file_size / (1024*1024), hdd->total_sectors);

    return 1;
}

/*
 * Close image file
 */
void SCSI_HDD_Close(SCSI_HDD *hdd)
{
    if (!hdd) return;

    if (hdd->image_fp) {
        File_Close((FILEH)hdd->image_fp);
        hdd->image_fp = NULL;
    }

    hdd->image_path[0] = '\0';
    hdd->total_sectors = 0;
    hdd->unit_ready = 0;
    hdd->state = HDD_STATE_IDLE;

    HDD_DEBUG("Closed\n");
}

/*
 * Check if image is opened
 */
int SCSI_HDD_IsOpened(SCSI_HDD *hdd)
{
    return hdd && hdd->image_fp != NULL;
}

/*
 * Attach HDD to SCSI bus
 */
void SCSI_HDD_AttachToBus(SCSI_HDD *hdd, SCSI_BUS *bus, int id)
{
    if (!hdd || !bus) return;

    SCSI_BUS_AttachDevice(bus, &hdd->base, id);
    HDD_DEBUG("Attached to bus at ID %d\n", id);
}

/*
 * Detach HDD from SCSI bus
 */
void SCSI_HDD_DetachFromBus(SCSI_HDD *hdd)
{
    if (!hdd || !hdd->base.bus) return;

    SCSI_BUS_DetachDevice(hdd->base.bus, hdd->base.id);
    HDD_DEBUG("Detached from bus\n");
}

/*
 * Get SCSI device pointer
 */
SCSI_DEVICE* SCSI_HDD_GetDevice(SCSI_HDD *hdd)
{
    return hdd ? &hdd->base : NULL;
}

/*
 * Reset callback
 */
static void hdd_reset(SCSI_DEVICE *dev)
{
    SCSI_HDD *hdd = (SCSI_HDD*)dev->private_data;

    hdd->state = HDD_STATE_IDLE;
    hdd->selected = 0;
    hdd->cmd_pos = 0;
    hdd->data_pos = 0;
    hdd->data_len = 0;
    hdd_clear_sense(hdd);

    /* Release bus signals */
    if (hdd->base.bus) {
        SCSI_BUS_SetCtrl(hdd->base.bus, hdd->base.id, 0, SCSI_SIGNAL_ALL);
        SCSI_BUS_SetData(hdd->base.bus, hdd->base.id, 0);
    }

    HDD_DEBUG("Reset\n");
}

/*
 * Bus control signal change callback
 */
static void hdd_bus_ctrl_changed(SCSI_DEVICE *dev)
{
    SCSI_HDD *hdd = (SCSI_HDD*)dev->private_data;
    DWORD ctrl;

    if (!hdd->base.bus) return;

    ctrl = SCSI_BUS_GetCtrl(hdd->base.bus);

    /* Check for bus reset */
    if (ctrl & SCSI_SIGNAL_RST) {
        hdd_reset(dev);
        return;
    }

    /* Handle selection phase */
    if ((ctrl & SCSI_SIGNAL_SEL) && !(ctrl & SCSI_SIGNAL_BSY)) {
        /* Check if we are being selected */
        BYTE data = SCSI_BUS_GetData(hdd->base.bus);
        if (data & (1 << hdd->base.id)) {
            /* We are selected */
            hdd->selected = 1;
            hdd->state = HDD_STATE_COMMAND;
            hdd->cmd_pos = 0;

            /* Assert BSY to acknowledge selection */
            SCSI_BUS_SetCtrl(hdd->base.bus, hdd->base.id,
                SCSI_SIGNAL_BSY, SCSI_SIGNAL_BSY);

            HDD_DEBUG("Selected (ID=%d)\n", hdd->base.id);
        }
    }
}

/*
 * Selection callback (called when selected by initiator)
 */
static int hdd_select(SCSI_DEVICE *dev)
{
    SCSI_HDD *hdd = (SCSI_HDD*)dev->private_data;

    if (!hdd->unit_ready && !hdd->image_fp) {
        /* No medium - don't respond to selection */
        return 0;
    }

    hdd->selected = 1;
    hdd->state = HDD_STATE_COMMAND;
    hdd->cmd_pos = 0;
    hdd->cmd_len = 0;

    HDD_DEBUG("Selected\n");
    return 1;
}

/*
 * Process SCSI command
 */
static void hdd_command(SCSI_DEVICE *dev, BYTE *cmd, int len)
{
    SCSI_HDD *hdd = (SCSI_HDD*)dev->private_data;
    int i;

    if (len > HDD_CMD_BUFFER_SIZE) len = HDD_CMD_BUFFER_SIZE;

    memcpy(hdd->cmd_buf, cmd, len);
    hdd->cmd_len = len;
    hdd->cmd_pos = 0;

    HDD_DEBUG("Command: ");
    for (i = 0; i < len; i++) {
        HDD_DEBUG("%02X ", cmd[i]);
    }
    HDD_DEBUG("\n");

    hdd_execute_command(hdd);
}

/*
 * Read data from device (DATA IN phase)
 */
static int hdd_data_read(SCSI_DEVICE *dev, BYTE *buf, int max_len)
{
    SCSI_HDD *hdd = (SCSI_HDD*)dev->private_data;
    int count = 0;

    if (hdd->state != HDD_STATE_DATA_IN) return 0;

    /* Copy data from buffer */
    while (count < max_len && hdd->data_pos < hdd->data_len) {
        buf[count++] = hdd->data_buf[hdd->data_pos++];
    }

    /* Check if data transfer is complete */
    if (hdd->data_pos >= hdd->data_len) {
        /* Move to status phase */
        hdd->state = HDD_STATE_STATUS;
        HDD_DEBUG("DATA_IN complete, moving to STATUS\n");
    }

    return count;
}

/*
 * Write data to device (DATA OUT phase)
 */
static int hdd_data_write(SCSI_DEVICE *dev, const BYTE *buf, int len)
{
    SCSI_HDD *hdd = (SCSI_HDD*)dev->private_data;
    int count = 0;

    if (hdd->state != HDD_STATE_DATA_OUT) return 0;

    /* Copy data to buffer */
    while (count < len && hdd->data_pos < hdd->data_len) {
        hdd->data_buf[hdd->data_pos++] = buf[count++];

        /* Check if we have a complete sector to write */
        if (hdd->data_pos > 0 &&
            (hdd->data_pos % hdd->bytes_per_sector) == 0) {
            /* Write the sector */
            DWORD sector_offset = (hdd->data_pos / hdd->bytes_per_sector) - 1;
            if (!hdd_write_sector(hdd, hdd->current_lba + sector_offset,
                    hdd->data_buf + sector_offset * hdd->bytes_per_sector)) {
                hdd_set_sense(hdd, SCSI_SENSE_MEDIUM_ERROR,
                    SCSI_ASC_NO_ADDITIONAL_SENSE, 0);
                hdd->status = SCSI_STATUS_CHECK_CONDITION;
                hdd->state = HDD_STATE_STATUS;
                return count;
            }
        }
    }

    /* Check if data transfer is complete */
    if (hdd->data_pos >= hdd->data_len) {
        hdd->state = HDD_STATE_STATUS;
        HDD_DEBUG("DATA_OUT complete, moving to STATUS\n");
    }

    return count;
}

/*
 * Get status byte
 */
static BYTE hdd_get_status(SCSI_DEVICE *dev)
{
    SCSI_HDD *hdd = (SCSI_HDD*)dev->private_data;
    return hdd->status;
}

/*
 * Get message byte
 */
static BYTE hdd_get_message(SCSI_DEVICE *dev)
{
    SCSI_HDD *hdd = (SCSI_HDD*)dev->private_data;
    return hdd->message;
}

/*
 * Set sense data
 */
static void hdd_set_sense(SCSI_HDD *hdd, BYTE key, BYTE asc, BYTE ascq)
{
    hdd->sense_key = key;
    hdd->sense_asc = asc;
    hdd->sense_ascq = ascq;
}

/*
 * Clear sense data
 */
static void hdd_clear_sense(SCSI_HDD *hdd)
{
    hdd->sense_key = SCSI_SENSE_NO_SENSE;
    hdd->sense_asc = SCSI_ASC_NO_ADDITIONAL_SENSE;
    hdd->sense_ascq = 0;
    hdd->sense_info = 0;
}

/*
 * Get command length based on group code
 */
static int get_command_length(BYTE opcode)
{
    switch ((opcode >> 5) & 0x07) {
    case 0: return 6;   /* Group 0: 6-byte commands */
    case 1: return 10;  /* Group 1: 10-byte commands */
    case 2: return 10;  /* Group 2: 10-byte commands */
    case 5: return 12;  /* Group 5: 12-byte commands */
    default: return 6;
    }
}

/*
 * Execute SCSI command
 */
static void hdd_execute_command(SCSI_HDD *hdd)
{
    BYTE opcode = hdd->cmd_buf[0];

    HDD_DEBUG("Execute command: 0x%02X\n", opcode);

    /* Default: good status */
    hdd->status = SCSI_STATUS_GOOD;
    hdd->message = 0x00;  /* COMMAND COMPLETE */

    switch (opcode) {
    case SCSI_CMD_TEST_UNIT_READY:
        cmd_test_unit_ready(hdd);
        break;

    case SCSI_CMD_REZERO_UNIT:
        cmd_rezero_unit(hdd);
        break;

    case SCSI_CMD_REQUEST_SENSE:
        cmd_request_sense(hdd);
        break;

    case SCSI_CMD_FORMAT_UNIT:
        cmd_format_unit(hdd);
        break;

    case SCSI_CMD_READ_6:
        cmd_read_6(hdd);
        break;

    case SCSI_CMD_WRITE_6:
        cmd_write_6(hdd);
        break;

    case SCSI_CMD_SEEK_6:
        cmd_seek_6(hdd);
        break;

    case SCSI_CMD_INQUIRY:
        cmd_inquiry(hdd);
        break;

    case SCSI_CMD_MODE_SELECT_6:
        cmd_mode_select_6(hdd);
        break;

    case SCSI_CMD_MODE_SENSE_6:
        cmd_mode_sense_6(hdd);
        break;

    case SCSI_CMD_START_STOP_UNIT:
        cmd_start_stop_unit(hdd);
        break;

    case SCSI_CMD_READ_CAPACITY:
        cmd_read_capacity(hdd);
        break;

    case SCSI_CMD_READ_10:
        cmd_read_10(hdd);
        break;

    case SCSI_CMD_WRITE_10:
        cmd_write_10(hdd);
        break;

    case SCSI_CMD_VERIFY_10:
        cmd_verify_10(hdd);
        break;

    default:
        /* Unknown command */
        HDD_DEBUG("Unknown command: 0x%02X\n", opcode);
        hdd_set_sense(hdd, SCSI_SENSE_ILLEGAL_REQUEST,
            SCSI_ASC_INVALID_COMMAND, 0);
        hdd->status = SCSI_STATUS_CHECK_CONDITION;
        hdd->state = HDD_STATE_STATUS;
        break;
    }
}

/*
 * Read sector from image
 */
static int hdd_read_sector(SCSI_HDD *hdd, DWORD lba, BYTE *buf)
{
    FILEH fp = (FILEH)hdd->image_fp;
    DWORD offset;

    if (!fp) return 0;
    if (lba >= hdd->total_sectors) return 0;

    offset = lba * hdd->bytes_per_sector;
    if (File_Seek(fp, offset, FSEEK_SET) != offset) {
        return 0;
    }

    if (File_Read(fp, buf, hdd->bytes_per_sector) != (signed)hdd->bytes_per_sector) {
        return 0;
    }

    return 1;
}

/*
 * Write sector to image
 */
static int hdd_write_sector(SCSI_HDD *hdd, DWORD lba, const BYTE *buf)
{
    FILEH fp = (FILEH)hdd->image_fp;
    DWORD offset;

    if (!fp) return 0;
    if (lba >= hdd->total_sectors) return 0;

    offset = lba * hdd->bytes_per_sector;
    if (File_Seek(fp, offset, FSEEK_SET) != offset) {
        return 0;
    }

    if (File_Write(fp, (void*)buf, hdd->bytes_per_sector) != (signed)hdd->bytes_per_sector) {
        return 0;
    }

    return 1;
}

/* ======================================================================
 * Command Handlers
 * ====================================================================== */

/*
 * TEST UNIT READY (0x00)
 */
static void cmd_test_unit_ready(SCSI_HDD *hdd)
{
    HDD_DEBUG("TEST UNIT READY\n");

    if (!hdd->unit_ready || !hdd->image_fp) {
        hdd_set_sense(hdd, SCSI_SENSE_NOT_READY,
            SCSI_ASC_MEDIUM_NOT_PRESENT, 0);
        hdd->status = SCSI_STATUS_CHECK_CONDITION;
    } else if (hdd->media_changed) {
        hdd_set_sense(hdd, SCSI_SENSE_UNIT_ATTENTION, 0x28, 0);
        hdd->status = SCSI_STATUS_CHECK_CONDITION;
        hdd->media_changed = 0;
    }

    hdd->state = HDD_STATE_STATUS;
}

/*
 * REZERO UNIT (0x01)
 */
static void cmd_rezero_unit(SCSI_HDD *hdd)
{
    HDD_DEBUG("REZERO UNIT\n");

    if (!hdd->unit_ready) {
        hdd_set_sense(hdd, SCSI_SENSE_NOT_READY,
            SCSI_ASC_MEDIUM_NOT_PRESENT, 0);
        hdd->status = SCSI_STATUS_CHECK_CONDITION;
    }

    hdd->state = HDD_STATE_STATUS;
}

/*
 * REQUEST SENSE (0x03)
 */
static void cmd_request_sense(SCSI_HDD *hdd)
{
    int alloc_len = hdd->cmd_buf[4];

    HDD_DEBUG("REQUEST SENSE (alloc=%d)\n", alloc_len);

    if (alloc_len == 0) alloc_len = 4;  /* Minimum for SCSI-1 */
    if (alloc_len > 18) alloc_len = 18;

    memset(hdd->data_buf, 0, alloc_len);

    /* Build sense data */
    hdd->data_buf[0] = 0x70;  /* Current errors, fixed format */
    hdd->data_buf[2] = hdd->sense_key & 0x0F;
    /* Information field (LBA where error occurred) */
    hdd->data_buf[3] = (hdd->sense_info >> 24) & 0xFF;
    hdd->data_buf[4] = (hdd->sense_info >> 16) & 0xFF;
    hdd->data_buf[5] = (hdd->sense_info >> 8) & 0xFF;
    hdd->data_buf[6] = hdd->sense_info & 0xFF;
    hdd->data_buf[7] = 10;  /* Additional sense length */
    hdd->data_buf[12] = hdd->sense_asc;
    hdd->data_buf[13] = hdd->sense_ascq;

    hdd->data_len = alloc_len;
    hdd->data_pos = 0;
    hdd->state = HDD_STATE_DATA_IN;

    /* Clear sense after reading */
    hdd_clear_sense(hdd);
}

/*
 * FORMAT UNIT (0x04)
 */
static void cmd_format_unit(SCSI_HDD *hdd)
{
    HDD_DEBUG("FORMAT UNIT\n");

    if (!hdd->unit_ready) {
        hdd_set_sense(hdd, SCSI_SENSE_NOT_READY,
            SCSI_ASC_MEDIUM_NOT_PRESENT, 0);
        hdd->status = SCSI_STATUS_CHECK_CONDITION;
    }

    /* Just return success - we don't actually format */
    hdd->state = HDD_STATE_STATUS;
}

/*
 * READ (6) (0x08)
 */
static void cmd_read_6(SCSI_HDD *hdd)
{
    DWORD lba;
    DWORD blocks;
    DWORD i;
    DWORD total_bytes;

    lba = ((hdd->cmd_buf[1] & 0x1F) << 16) |
          (hdd->cmd_buf[2] << 8) |
          hdd->cmd_buf[3];
    blocks = hdd->cmd_buf[4];
    if (blocks == 0) blocks = 256;

    HDD_DEBUG("READ(6) LBA=%d, Blocks=%d\n", lba, blocks);

    if (!hdd->unit_ready) {
        hdd_set_sense(hdd, SCSI_SENSE_NOT_READY,
            SCSI_ASC_MEDIUM_NOT_PRESENT, 0);
        hdd->status = SCSI_STATUS_CHECK_CONDITION;
        hdd->state = HDD_STATE_STATUS;
        return;
    }

    if (lba + blocks > hdd->total_sectors) {
        hdd_set_sense(hdd, SCSI_SENSE_ILLEGAL_REQUEST,
            SCSI_ASC_LBA_OUT_OF_RANGE, 0);
        hdd->sense_info = lba;
        hdd->status = SCSI_STATUS_CHECK_CONDITION;
        hdd->state = HDD_STATE_STATUS;
        return;
    }

    total_bytes = blocks * hdd->bytes_per_sector;
    if (total_bytes > HDD_BUFFER_SIZE) {
        total_bytes = HDD_BUFFER_SIZE;
        blocks = total_bytes / hdd->bytes_per_sector;
    }

    /* Read all sectors into buffer */
    for (i = 0; i < blocks; i++) {
        if (!hdd_read_sector(hdd, lba + i,
                hdd->data_buf + i * hdd->bytes_per_sector)) {
            hdd_set_sense(hdd, SCSI_SENSE_MEDIUM_ERROR,
                SCSI_ASC_NO_ADDITIONAL_SENSE, 0);
            hdd->sense_info = lba + i;
            hdd->status = SCSI_STATUS_CHECK_CONDITION;
            hdd->state = HDD_STATE_STATUS;
            return;
        }
    }

    hdd->data_len = total_bytes;
    hdd->data_pos = 0;
    hdd->current_lba = lba;
    hdd->current_blocks = blocks;
    hdd->state = HDD_STATE_DATA_IN;
}

/*
 * WRITE (6) (0x0A)
 */
static void cmd_write_6(SCSI_HDD *hdd)
{
    DWORD lba;
    DWORD blocks;
    DWORD total_bytes;

    lba = ((hdd->cmd_buf[1] & 0x1F) << 16) |
          (hdd->cmd_buf[2] << 8) |
          hdd->cmd_buf[3];
    blocks = hdd->cmd_buf[4];
    if (blocks == 0) blocks = 256;

    HDD_DEBUG("WRITE(6) LBA=%d, Blocks=%d\n", lba, blocks);

    if (!hdd->unit_ready) {
        hdd_set_sense(hdd, SCSI_SENSE_NOT_READY,
            SCSI_ASC_MEDIUM_NOT_PRESENT, 0);
        hdd->status = SCSI_STATUS_CHECK_CONDITION;
        hdd->state = HDD_STATE_STATUS;
        return;
    }

    if (lba + blocks > hdd->total_sectors) {
        hdd_set_sense(hdd, SCSI_SENSE_ILLEGAL_REQUEST,
            SCSI_ASC_LBA_OUT_OF_RANGE, 0);
        hdd->sense_info = lba;
        hdd->status = SCSI_STATUS_CHECK_CONDITION;
        hdd->state = HDD_STATE_STATUS;
        return;
    }

    total_bytes = blocks * hdd->bytes_per_sector;
    if (total_bytes > HDD_BUFFER_SIZE) {
        total_bytes = HDD_BUFFER_SIZE;
        blocks = total_bytes / hdd->bytes_per_sector;
    }

    hdd->data_len = total_bytes;
    hdd->data_pos = 0;
    hdd->current_lba = lba;
    hdd->current_blocks = blocks;
    hdd->state = HDD_STATE_DATA_OUT;
}

/*
 * SEEK (6) (0x0B)
 */
static void cmd_seek_6(SCSI_HDD *hdd)
{
    DWORD lba;

    lba = ((hdd->cmd_buf[1] & 0x1F) << 16) |
          (hdd->cmd_buf[2] << 8) |
          hdd->cmd_buf[3];

    HDD_DEBUG("SEEK(6) LBA=%d\n", lba);

    if (!hdd->unit_ready) {
        hdd_set_sense(hdd, SCSI_SENSE_NOT_READY,
            SCSI_ASC_MEDIUM_NOT_PRESENT, 0);
        hdd->status = SCSI_STATUS_CHECK_CONDITION;
    } else if (lba >= hdd->total_sectors) {
        hdd_set_sense(hdd, SCSI_SENSE_ILLEGAL_REQUEST,
            SCSI_ASC_LBA_OUT_OF_RANGE, 0);
        hdd->status = SCSI_STATUS_CHECK_CONDITION;
    }

    hdd->state = HDD_STATE_STATUS;
}

/*
 * INQUIRY (0x12)
 */
static void cmd_inquiry(SCSI_HDD *hdd)
{
    int alloc_len = hdd->cmd_buf[4];
    int len;

    HDD_DEBUG("INQUIRY (alloc=%d)\n", alloc_len);

    if (alloc_len == 0) {
        hdd->state = HDD_STATE_STATUS;
        return;
    }

    memset(hdd->data_buf, 0, alloc_len);

    /* Standard INQUIRY data */
    hdd->data_buf[0] = 0x00;  /* Direct access device (disk) */
    hdd->data_buf[1] = 0x00;  /* Not removable */
    hdd->data_buf[2] = 0x02;  /* SCSI-2 compliance */
    hdd->data_buf[3] = 0x02;  /* Response data format (SCSI-2) */
    hdd->data_buf[4] = 31;    /* Additional length */
    hdd->data_buf[5] = 0x00;
    hdd->data_buf[6] = 0x00;
    hdd->data_buf[7] = 0x00;

    /* Vendor ID (8 bytes) */
    memcpy(&hdd->data_buf[8], "SHARP   ", 8);

    /* Product ID (16 bytes) */
    memcpy(&hdd->data_buf[16], "PX68K SCSI HDD  ", 16);

    /* Product revision (4 bytes) */
    memcpy(&hdd->data_buf[32], "1.00", 4);

    len = 36;
    if (alloc_len < len) len = alloc_len;

    hdd->data_len = len;
    hdd->data_pos = 0;
    hdd->state = HDD_STATE_DATA_IN;
}

/*
 * MODE SELECT (6) (0x15)
 */
static void cmd_mode_select_6(SCSI_HDD *hdd)
{
    int param_len = hdd->cmd_buf[4];

    HDD_DEBUG("MODE SELECT(6) param_len=%d\n", param_len);

    if (param_len > 0) {
        /* Receive parameter data, but ignore it */
        hdd->data_len = param_len;
        hdd->data_pos = 0;
        hdd->state = HDD_STATE_DATA_OUT;
    } else {
        hdd->state = HDD_STATE_STATUS;
    }
}

/*
 * MODE SENSE (6) (0x1A)
 */
static void cmd_mode_sense_6(SCSI_HDD *hdd)
{
    int page = hdd->cmd_buf[2] & 0x3F;
    int alloc_len = hdd->cmd_buf[4];
    int pos = 0;

    HDD_DEBUG("MODE SENSE(6) page=0x%02X alloc=%d\n", page, alloc_len);

    if (alloc_len == 0) {
        hdd->state = HDD_STATE_STATUS;
        return;
    }

    memset(hdd->data_buf, 0, alloc_len);

    /* Mode parameter header */
    pos = 0;
    hdd->data_buf[pos++] = 0;     /* Mode data length (filled later) */
    hdd->data_buf[pos++] = 0x00;  /* Medium type */
    hdd->data_buf[pos++] = 0x00;  /* Device-specific parameter */
    hdd->data_buf[pos++] = 8;     /* Block descriptor length */

    /* Block descriptor */
    hdd->data_buf[pos++] = 0;     /* Density code */
    /* Number of blocks (3 bytes) */
    hdd->data_buf[pos++] = (hdd->total_sectors >> 16) & 0xFF;
    hdd->data_buf[pos++] = (hdd->total_sectors >> 8) & 0xFF;
    hdd->data_buf[pos++] = hdd->total_sectors & 0xFF;
    hdd->data_buf[pos++] = 0;     /* Reserved */
    /* Block length (3 bytes) */
    hdd->data_buf[pos++] = (hdd->bytes_per_sector >> 16) & 0xFF;
    hdd->data_buf[pos++] = (hdd->bytes_per_sector >> 8) & 0xFF;
    hdd->data_buf[pos++] = hdd->bytes_per_sector & 0xFF;

    /* Page data */
    if (page == 0x03 || page == 0x3F) {
        /* Format parameters page */
        hdd->data_buf[pos++] = 0x03;
        hdd->data_buf[pos++] = 0x16;
        /* Tracks per zone */
        hdd->data_buf[pos++] = (hdd->heads >> 8) & 0xFF;
        hdd->data_buf[pos++] = hdd->heads & 0xFF;
        /* Alternate sectors per zone */
        hdd->data_buf[pos++] = 0;
        hdd->data_buf[pos++] = 0;
        /* Alternate tracks per zone */
        hdd->data_buf[pos++] = 0;
        hdd->data_buf[pos++] = 0;
        /* Alternate tracks per volume */
        hdd->data_buf[pos++] = 0;
        hdd->data_buf[pos++] = 0;
        /* Sectors per track */
        hdd->data_buf[pos++] = (hdd->sectors_per_track >> 8) & 0xFF;
        hdd->data_buf[pos++] = hdd->sectors_per_track & 0xFF;
        /* Bytes per sector */
        hdd->data_buf[pos++] = (hdd->bytes_per_sector >> 8) & 0xFF;
        hdd->data_buf[pos++] = hdd->bytes_per_sector & 0xFF;
        /* Interleave */
        hdd->data_buf[pos++] = 0;
        hdd->data_buf[pos++] = 1;
        /* Track skew factor */
        hdd->data_buf[pos++] = 0;
        hdd->data_buf[pos++] = 0;
        /* Cylinder skew factor */
        hdd->data_buf[pos++] = 0;
        hdd->data_buf[pos++] = 0;
        /* Drive type flags */
        hdd->data_buf[pos++] = 0;
        /* Reserved */
        hdd->data_buf[pos++] = 0;
        hdd->data_buf[pos++] = 0;
        hdd->data_buf[pos++] = 0;
    }

    if (page == 0x04 || page == 0x3F) {
        /* Rigid disk geometry page */
        hdd->data_buf[pos++] = 0x04;
        hdd->data_buf[pos++] = 0x16;
        /* Number of cylinders (3 bytes) */
        hdd->data_buf[pos++] = (hdd->cylinders >> 16) & 0xFF;
        hdd->data_buf[pos++] = (hdd->cylinders >> 8) & 0xFF;
        hdd->data_buf[pos++] = hdd->cylinders & 0xFF;
        /* Number of heads */
        hdd->data_buf[pos++] = hdd->heads;
        /* Write precompensation cylinder (3 bytes) */
        hdd->data_buf[pos++] = 0;
        hdd->data_buf[pos++] = 0;
        hdd->data_buf[pos++] = 0;
        /* Reduced write current cylinder (3 bytes) */
        hdd->data_buf[pos++] = 0;
        hdd->data_buf[pos++] = 0;
        hdd->data_buf[pos++] = 0;
        /* Drive step rate */
        hdd->data_buf[pos++] = 0;
        hdd->data_buf[pos++] = 0;
        /* Landing zone cylinder (3 bytes) */
        hdd->data_buf[pos++] = 0;
        hdd->data_buf[pos++] = 0;
        hdd->data_buf[pos++] = 0;
        /* RPL */
        hdd->data_buf[pos++] = 0;
        /* Rotational offset */
        hdd->data_buf[pos++] = 0;
        /* Reserved */
        hdd->data_buf[pos++] = 0;
        /* Medium rotation rate */
        hdd->data_buf[pos++] = 0x1C;  /* 7200 RPM */
        hdd->data_buf[pos++] = 0x20;
        /* Reserved */
        hdd->data_buf[pos++] = 0;
        hdd->data_buf[pos++] = 0;
    }

    /* Update mode data length */
    hdd->data_buf[0] = pos - 1;

    if (pos > alloc_len) pos = alloc_len;

    hdd->data_len = pos;
    hdd->data_pos = 0;
    hdd->state = HDD_STATE_DATA_IN;
}

/*
 * START STOP UNIT (0x1B)
 */
static void cmd_start_stop_unit(SCSI_HDD *hdd)
{
    int start = hdd->cmd_buf[4] & 0x01;
    int loej = hdd->cmd_buf[4] & 0x02;

    HDD_DEBUG("START STOP UNIT start=%d loej=%d\n", start, loej);

    /* For HDD, just return success */
    hdd->state = HDD_STATE_STATUS;
}

/*
 * READ CAPACITY (0x25)
 */
static void cmd_read_capacity(SCSI_HDD *hdd)
{
    DWORD last_lba;

    HDD_DEBUG("READ CAPACITY\n");

    if (!hdd->unit_ready) {
        hdd_set_sense(hdd, SCSI_SENSE_NOT_READY,
            SCSI_ASC_MEDIUM_NOT_PRESENT, 0);
        hdd->status = SCSI_STATUS_CHECK_CONDITION;
        hdd->state = HDD_STATE_STATUS;
        return;
    }

    last_lba = hdd->total_sectors - 1;

    /* Last LBA (4 bytes, big-endian) */
    hdd->data_buf[0] = (last_lba >> 24) & 0xFF;
    hdd->data_buf[1] = (last_lba >> 16) & 0xFF;
    hdd->data_buf[2] = (last_lba >> 8) & 0xFF;
    hdd->data_buf[3] = last_lba & 0xFF;

    /* Block size (4 bytes, big-endian) */
    hdd->data_buf[4] = (hdd->bytes_per_sector >> 24) & 0xFF;
    hdd->data_buf[5] = (hdd->bytes_per_sector >> 16) & 0xFF;
    hdd->data_buf[6] = (hdd->bytes_per_sector >> 8) & 0xFF;
    hdd->data_buf[7] = hdd->bytes_per_sector & 0xFF;

    hdd->data_len = 8;
    hdd->data_pos = 0;
    hdd->state = HDD_STATE_DATA_IN;
}

/*
 * READ (10) (0x28)
 */
static void cmd_read_10(SCSI_HDD *hdd)
{
    DWORD lba;
    DWORD blocks;
    DWORD i;
    DWORD total_bytes;

    lba = (hdd->cmd_buf[2] << 24) |
          (hdd->cmd_buf[3] << 16) |
          (hdd->cmd_buf[4] << 8) |
          hdd->cmd_buf[5];
    blocks = (hdd->cmd_buf[7] << 8) | hdd->cmd_buf[8];

    HDD_DEBUG("READ(10) LBA=%d, Blocks=%d\n", lba, blocks);

    if (!hdd->unit_ready) {
        hdd_set_sense(hdd, SCSI_SENSE_NOT_READY,
            SCSI_ASC_MEDIUM_NOT_PRESENT, 0);
        hdd->status = SCSI_STATUS_CHECK_CONDITION;
        hdd->state = HDD_STATE_STATUS;
        return;
    }

    if (blocks == 0) {
        /* Zero blocks - just return success */
        hdd->state = HDD_STATE_STATUS;
        return;
    }

    if (lba + blocks > hdd->total_sectors) {
        hdd_set_sense(hdd, SCSI_SENSE_ILLEGAL_REQUEST,
            SCSI_ASC_LBA_OUT_OF_RANGE, 0);
        hdd->sense_info = lba;
        hdd->status = SCSI_STATUS_CHECK_CONDITION;
        hdd->state = HDD_STATE_STATUS;
        return;
    }

    total_bytes = blocks * hdd->bytes_per_sector;
    if (total_bytes > HDD_BUFFER_SIZE) {
        total_bytes = HDD_BUFFER_SIZE;
        blocks = total_bytes / hdd->bytes_per_sector;
    }

    /* Read sectors into buffer */
    for (i = 0; i < blocks; i++) {
        if (!hdd_read_sector(hdd, lba + i,
                hdd->data_buf + i * hdd->bytes_per_sector)) {
            hdd_set_sense(hdd, SCSI_SENSE_MEDIUM_ERROR,
                SCSI_ASC_NO_ADDITIONAL_SENSE, 0);
            hdd->sense_info = lba + i;
            hdd->status = SCSI_STATUS_CHECK_CONDITION;
            hdd->state = HDD_STATE_STATUS;
            return;
        }
    }

    hdd->data_len = total_bytes;
    hdd->data_pos = 0;
    hdd->current_lba = lba;
    hdd->current_blocks = blocks;
    hdd->state = HDD_STATE_DATA_IN;
}

/*
 * WRITE (10) (0x2A)
 */
static void cmd_write_10(SCSI_HDD *hdd)
{
    DWORD lba;
    DWORD blocks;
    DWORD total_bytes;

    lba = (hdd->cmd_buf[2] << 24) |
          (hdd->cmd_buf[3] << 16) |
          (hdd->cmd_buf[4] << 8) |
          hdd->cmd_buf[5];
    blocks = (hdd->cmd_buf[7] << 8) | hdd->cmd_buf[8];

    HDD_DEBUG("WRITE(10) LBA=%d, Blocks=%d\n", lba, blocks);

    if (!hdd->unit_ready) {
        hdd_set_sense(hdd, SCSI_SENSE_NOT_READY,
            SCSI_ASC_MEDIUM_NOT_PRESENT, 0);
        hdd->status = SCSI_STATUS_CHECK_CONDITION;
        hdd->state = HDD_STATE_STATUS;
        return;
    }

    if (blocks == 0) {
        /* Zero blocks - just return success */
        hdd->state = HDD_STATE_STATUS;
        return;
    }

    if (lba + blocks > hdd->total_sectors) {
        hdd_set_sense(hdd, SCSI_SENSE_ILLEGAL_REQUEST,
            SCSI_ASC_LBA_OUT_OF_RANGE, 0);
        hdd->sense_info = lba;
        hdd->status = SCSI_STATUS_CHECK_CONDITION;
        hdd->state = HDD_STATE_STATUS;
        return;
    }

    total_bytes = blocks * hdd->bytes_per_sector;
    if (total_bytes > HDD_BUFFER_SIZE) {
        total_bytes = HDD_BUFFER_SIZE;
        blocks = total_bytes / hdd->bytes_per_sector;
    }

    hdd->data_len = total_bytes;
    hdd->data_pos = 0;
    hdd->current_lba = lba;
    hdd->current_blocks = blocks;
    hdd->state = HDD_STATE_DATA_OUT;
}

/*
 * VERIFY (10) (0x2F)
 */
static void cmd_verify_10(SCSI_HDD *hdd)
{
    DWORD lba;
    DWORD blocks;
    int bytchk = (hdd->cmd_buf[1] >> 1) & 0x01;

    lba = (hdd->cmd_buf[2] << 24) |
          (hdd->cmd_buf[3] << 16) |
          (hdd->cmd_buf[4] << 8) |
          hdd->cmd_buf[5];
    blocks = (hdd->cmd_buf[7] << 8) | hdd->cmd_buf[8];

    HDD_DEBUG("VERIFY(10) LBA=%d, Blocks=%d, BytChk=%d\n", lba, blocks, bytchk);

    if (!hdd->unit_ready) {
        hdd_set_sense(hdd, SCSI_SENSE_NOT_READY,
            SCSI_ASC_MEDIUM_NOT_PRESENT, 0);
        hdd->status = SCSI_STATUS_CHECK_CONDITION;
        hdd->state = HDD_STATE_STATUS;
        return;
    }

    if (bytchk) {
        /* Byte check requested - not supported */
        hdd_set_sense(hdd, SCSI_SENSE_ILLEGAL_REQUEST,
            SCSI_ASC_INVALID_FIELD_IN_CDB, 0);
        hdd->status = SCSI_STATUS_CHECK_CONDITION;
    }

    /* Without BytChk, just return success */
    hdd->state = HDD_STATE_STATUS;
}
