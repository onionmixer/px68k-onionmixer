/*
 * scsi_bus.c - SCSI Bus Emulation
 *
 * Implements wired-OR bus logic for SCSI signal combination.
 *
 * Reference: MAME src/devices/machine/nscsi_bus.cpp
 */

#include "scsi_bus.h"
#include <string.h>
#include <stdio.h>

/* Debug output */
#if 0
#define SCSI_BUS_DEBUG(...) printf("[SCSI_BUS] " __VA_ARGS__)
#else
#define SCSI_BUS_DEBUG(...)
#endif

/*
 * Update combined bus signals using wired-OR logic
 * All devices' signals are OR'd together
 */
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

/*
 * Notify all devices about bus signal change
 */
static void SCSI_BUS_NotifyCtrlChanged(SCSI_BUS *bus)
{
    int i;

    /* Notify callback (for SPC) */
    if (bus->ctrl_changed_callback) {
        bus->ctrl_changed_callback(bus, bus->callback_param);
    }

    /* Notify each connected device */
    for (i = 0; i < SCSI_ID_MAX; i++) {
        if (bus->devices[i] && bus->devices[i]->bus_ctrl_changed) {
            bus->devices[i]->bus_ctrl_changed(bus->devices[i]);
        }
    }
}

/*
 * Initialize SCSI bus
 */
void SCSI_BUS_Init(SCSI_BUS *bus)
{
    int i;

    memset(bus, 0, sizeof(SCSI_BUS));

    for (i = 0; i < SCSI_ID_MAX; i++) {
        bus->devices[i] = NULL;
        bus->dev_ctrl[i] = 0;
        bus->dev_data[i] = 0;
    }

    bus->ctrl = 0;
    bus->data = 0;
    bus->device_count = 0;
    bus->ctrl_changed_callback = NULL;
    bus->callback_param = NULL;

    SCSI_BUS_DEBUG("Initialized\n");
}

/*
 * Reset SCSI bus
 */
void SCSI_BUS_Reset(SCSI_BUS *bus)
{
    int i;

    SCSI_BUS_DEBUG("Reset\n");

    /* Clear all signals */
    for (i = 0; i < SCSI_ID_MAX; i++) {
        bus->dev_ctrl[i] = 0;
        bus->dev_data[i] = 0;
    }

    bus->ctrl = 0;
    bus->data = 0;

    /* Reset all connected devices */
    for (i = 0; i < SCSI_ID_MAX; i++) {
        if (bus->devices[i] && bus->devices[i]->reset) {
            bus->devices[i]->reset(bus->devices[i]);
        }
    }
}

/*
 * Cleanup SCSI bus
 */
void SCSI_BUS_Cleanup(SCSI_BUS *bus)
{
    int i;

    for (i = 0; i < SCSI_ID_MAX; i++) {
        bus->devices[i] = NULL;
    }

    bus->device_count = 0;
    SCSI_BUS_DEBUG("Cleanup\n");
}

/*
 * Attach a device to the bus
 */
void SCSI_BUS_AttachDevice(SCSI_BUS *bus, SCSI_DEVICE *dev, int id)
{
    if (!bus || !dev) return;
    if (id < 0 || id >= SCSI_ID_MAX) return;

    /* Detach existing device at this ID */
    if (bus->devices[id]) {
        SCSI_BUS_DetachDevice(bus, id);
    }

    bus->devices[id] = dev;
    dev->id = id;
    dev->bus = bus;
    bus->device_count++;

    SCSI_BUS_DEBUG("Device attached: ID=%d, Type=%d\n", id, dev->type);
}

/*
 * Detach a device from the bus
 */
void SCSI_BUS_DetachDevice(SCSI_BUS *bus, int id)
{
    if (!bus) return;
    if (id < 0 || id >= SCSI_ID_MAX) return;

    if (bus->devices[id]) {
        bus->devices[id]->bus = NULL;
        bus->devices[id] = NULL;
        bus->dev_ctrl[id] = 0;
        bus->dev_data[id] = 0;
        bus->device_count--;

        SCSI_BUS_UpdateSignals(bus);

        SCSI_BUS_DEBUG("Device detached: ID=%d\n", id);
    }
}

/*
 * Get device at specified ID
 */
SCSI_DEVICE* SCSI_BUS_GetDevice(SCSI_BUS *bus, int id)
{
    if (!bus) return NULL;
    if (id < 0 || id >= SCSI_ID_MAX) return NULL;

    return bus->devices[id];
}

/*
 * Set control signals from a device
 * Uses wired-OR: signals from all devices are OR'd together
 */
void SCSI_BUS_SetCtrl(SCSI_BUS *bus, int id, DWORD value, DWORD mask)
{
    DWORD old_ctrl;

    if (!bus) return;
    if (id < 0 || id >= SCSI_ID_MAX) return;

    old_ctrl = bus->ctrl;

    /* Update this device's contribution */
    bus->dev_ctrl[id] = (bus->dev_ctrl[id] & ~mask) | (value & mask);

    /* Recalculate combined bus signals */
    SCSI_BUS_UpdateSignals(bus);

    /* Notify if signals changed */
    if (bus->ctrl != old_ctrl) {
        SCSI_BUS_DEBUG("Ctrl changed: ID=%d, Old=0x%03X, New=0x%03X\n",
            id, old_ctrl, bus->ctrl);
        SCSI_BUS_NotifyCtrlChanged(bus);
    }
}

/*
 * Set data bus from a device
 */
void SCSI_BUS_SetData(SCSI_BUS *bus, int id, BYTE value)
{
    if (!bus) return;
    if (id < 0 || id >= SCSI_ID_MAX) return;

    bus->dev_data[id] = value;
    SCSI_BUS_UpdateSignals(bus);
}

/*
 * Get current control signals (combined from all devices)
 */
DWORD SCSI_BUS_GetCtrl(SCSI_BUS *bus)
{
    if (!bus) return 0;
    return bus->ctrl;
}

/*
 * Get current data bus value (combined from all devices)
 */
BYTE SCSI_BUS_GetData(SCSI_BUS *bus)
{
    if (!bus) return 0;
    return bus->data;
}

/*
 * Set callback for control signal changes
 */
void SCSI_BUS_SetCtrlChangedCallback(SCSI_BUS *bus,
    void (*callback)(SCSI_BUS *bus, void *param), void *param)
{
    if (!bus) return;

    bus->ctrl_changed_callback = callback;
    bus->callback_param = param;
}

/*
 * Check if bus is free (no BSY or SEL)
 */
int SCSI_BUS_IsBusFree(SCSI_BUS *bus)
{
    if (!bus) return 1;

    return !(bus->ctrl & (SCSI_SIGNAL_BSY | SCSI_SIGNAL_SEL));
}

/*
 * Get human-readable phase name for debugging
 */
const char* SCSI_BUS_GetPhaseName(int phase)
{
    switch (phase & SCSI_PHASE_MASK) {
    case SCSI_PHASE_DATA_OUT:  return "DATA_OUT";
    case SCSI_PHASE_DATA_IN:   return "DATA_IN";
    case SCSI_PHASE_COMMAND:   return "COMMAND";
    case SCSI_PHASE_STATUS:    return "STATUS";
    case SCSI_PHASE_MSG_OUT:   return "MSG_OUT";
    case SCSI_PHASE_MSG_IN:    return "MSG_IN";
    default:                   return "UNKNOWN";
    }
}
