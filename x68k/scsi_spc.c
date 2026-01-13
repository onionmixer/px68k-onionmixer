/*
 * scsi_spc.c - Fujitsu MB89352 SCSI Protocol Controller Emulation
 *
 * Reference: MAME src/devices/machine/mb87030.cpp
 */

#include "scsi_spc.h"
#include <string.h>
#include <stdio.h>

/* Debug output */
#if 0
#define SPC_DEBUG(...) printf("[SPC] " __VA_ARGS__)
#else
#define SPC_DEBUG(...)
#endif

/* Timer constants (in clock cycles, assuming 10MHz) */
#define SPC_DELAY_SHORT     10
#define SPC_DELAY_MEDIUM    32
#define SPC_DELAY_LONG      100
#define SPC_BUS_FREE_DELAY  100000  /* Bus free detection delay */

/*
 * FIFO Operations
 */
static void fifo_clear(MB89352 *spc)
{
    spc->fifo_read_pos = 0;
    spc->fifo_write_pos = 0;
    spc->fifo_count = 0;
}

static int fifo_empty(MB89352 *spc)
{
    return spc->fifo_count == 0;
}

static int fifo_full(MB89352 *spc)
{
    return spc->fifo_count >= SPC_FIFO_SIZE;
}

static void fifo_push(MB89352 *spc, BYTE data)
{
    if (!fifo_full(spc)) {
        spc->fifo[spc->fifo_write_pos] = data;
        spc->fifo_write_pos = (spc->fifo_write_pos + 1) % SPC_FIFO_SIZE;
        spc->fifo_count++;
    }
}

static BYTE fifo_pop(MB89352 *spc)
{
    BYTE data = 0;
    if (!fifo_empty(spc)) {
        data = spc->fifo[spc->fifo_read_pos];
        spc->fifo_read_pos = (spc->fifo_read_pos + 1) % SPC_FIFO_SIZE;
        spc->fifo_count--;
    }
    return data;
}

static BYTE fifo_peek(MB89352 *spc)
{
    if (!fifo_empty(spc)) {
        return spc->fifo[spc->fifo_read_pos];
    }
    return 0;
}

/*
 * Get state name for debugging
 */
const char* SPC_GetStateName(SPC_STATE state)
{
    switch (state) {
    case SPC_STATE_IDLE:                     return "IDLE";
    case SPC_STATE_ARBITRATION_WAIT_BUS_FREE: return "ARB_WAIT_FREE";
    case SPC_STATE_ARBITRATION_ASSERT_BSY:   return "ARB_ASSERT_BSY";
    case SPC_STATE_ARBITRATION_WAIT:         return "ARB_WAIT";
    case SPC_STATE_ARBITRATION_ASSERT_SEL:   return "ARB_ASSERT_SEL";
    case SPC_STATE_SELECTION_WAIT_BUS_FREE:  return "SEL_WAIT_FREE";
    case SPC_STATE_SELECTION_ASSERT_ID:      return "SEL_ASSERT_ID";
    case SPC_STATE_SELECTION_ASSERT_SEL:     return "SEL_ASSERT_SEL";
    case SPC_STATE_SELECTION_WAIT_BSY:       return "SEL_WAIT_BSY";
    case SPC_STATE_SELECTION:                return "SELECTION";
    case SPC_STATE_TRANSFER_WAIT_REQ:        return "XFER_WAIT_REQ";
    case SPC_STATE_TRANSFER_SEND_DATA:       return "XFER_SEND_DATA";
    case SPC_STATE_TRANSFER_RECV_DATA:       return "XFER_RECV_DATA";
    case SPC_STATE_TRANSFER_SEND_ACK:        return "XFER_SEND_ACK";
    case SPC_STATE_TRANSFER_WAIT_DEASSERT_REQ: return "XFER_WAIT_DEREQ";
    case SPC_STATE_TRANSFER_DEASSERT_ACK:    return "XFER_DEASSERT_ACK";
    case SPC_STATE_TRANSFER_WAIT_FIFO_EMPTY: return "XFER_WAIT_FIFO";
    default:                                 return "UNKNOWN";
    }
}

/*
 * Update SSTS register based on current state
 */
static void update_ssts(MB89352 *spc)
{
    /* TC zero flag */
    if (spc->tc == 0)
        spc->ssts |= SSTS_TC_ZERO;
    else
        spc->ssts &= ~SSTS_TC_ZERO;

    /* FIFO status */
    if (fifo_empty(spc))
        spc->ssts |= SSTS_DREQ_EMPTY;
    else
        spc->ssts &= ~SSTS_DREQ_EMPTY;

    if (fifo_full(spc))
        spc->ssts |= SSTS_DREQ_FULL;
    else
        spc->ssts &= ~SSTS_DREQ_FULL;
}

/*
 * Update interrupt state and call callback
 */
static void update_ints(MB89352 *spc)
{
    int new_irq_state = (spc->sctl & SCTL_INT_ENABLE) &&
                        (spc->ints || (spc->serr & SERR_XFER_OUT));

    if (new_irq_state != spc->irq_state) {
        spc->irq_state = new_irq_state;
        SPC_DEBUG("IRQ: %s\n", new_irq_state ? "assert" : "deassert");
        if (spc->irq_callback) {
            spc->irq_callback(new_irq_state, spc->irq_param);
        }
    }
}

/*
 * Set control signals on the bus
 */
static void scsi_set_ctrl(MB89352 *spc, DWORD value, DWORD mask)
{
    if (spc->sctl & SCTL_DIAG_MODE) {
        /* Diagnostic mode: modify local copy only */
        spc->scsi_ctrl = (spc->scsi_ctrl & ~mask) | (value & mask);
    } else {
        /* Normal mode: update bus */
        if (spc->bus) {
            SCSI_BUS_SetCtrl(spc->bus, spc->bus_id, value, mask);
        }
    }
}

/*
 * Get control signals from the bus
 */
static DWORD scsi_get_ctrl(MB89352 *spc)
{
    if (spc->sctl & SCTL_DIAG_MODE) {
        /* Diagnostic mode: combine local and sdgc */
        DWORD ret = 0;
        if ((spc->sdgc & SDGC_DIAG_IO) || (spc->scsi_ctrl & SCSI_SIGNAL_IO))
            ret |= SCSI_SIGNAL_IO;
        if ((spc->sdgc & SDGC_DIAG_CD) || (spc->scsi_ctrl & SCSI_SIGNAL_CD))
            ret |= SCSI_SIGNAL_CD;
        if ((spc->sdgc & SDGC_DIAG_MSG) || (spc->scsi_ctrl & SCSI_SIGNAL_MSG))
            ret |= SCSI_SIGNAL_MSG;
        if ((spc->sdgc & SDGC_DIAG_BSY) || (spc->scsi_ctrl & SCSI_SIGNAL_BSY))
            ret |= SCSI_SIGNAL_BSY;
        if ((spc->sdgc & SDGC_DIAG_ACK) || (spc->scsi_ctrl & SCSI_SIGNAL_ACK))
            ret |= SCSI_SIGNAL_ACK;
        if ((spc->sdgc & SDGC_DIAG_REQ) || (spc->scsi_ctrl & SCSI_SIGNAL_REQ))
            ret |= SCSI_SIGNAL_REQ;
        if (spc->scsi_ctrl & SCSI_SIGNAL_SEL)
            ret |= SCSI_SIGNAL_SEL;
        if (spc->scsi_ctrl & SCSI_SIGNAL_ATN)
            ret |= SCSI_SIGNAL_ATN;
        return ret;
    } else {
        /* Normal mode: read from bus */
        if (spc->bus) {
            return SCSI_BUS_GetCtrl(spc->bus);
        }
        return 0;
    }
}

/*
 * Update state with optional delay
 */
static void update_state(MB89352 *spc, SPC_STATE new_state, int delay)
{
    SPC_DEBUG("State: %s -> %s (delay=%d)\n",
        SPC_GetStateName(spc->state), SPC_GetStateName(new_state), delay);

    if (delay > 0) {
        spc->delay_state = new_state;
        spc->state = SPC_STATE_IDLE;  /* Wait in idle-like state */
        spc->delay_timer_count = 0;
        spc->delay_timer_target = delay;
    } else {
        spc->state = new_state;
        spc->delay_timer_target = 0;
    }
}

/*
 * Command complete handler
 */
static void scsi_command_complete(MB89352 *spc)
{
    SPC_DEBUG("Command complete\n");
    spc->ints |= INTS_COMMAND_COMPLETE;
    spc->ssts &= ~(SSTS_SPC_BUSY | SSTS_XFER_IN_PROGRESS);
    update_ints(spc);
    update_state(spc, SPC_STATE_IDLE, 0);
}

/*
 * Disconnect handler
 */
static void scsi_disconnect(MB89352 *spc)
{
    SPC_DEBUG("Disconnect, tc=%d\n", spc->tc);
    spc->ssts &= ~(SSTS_INIT_CONNECTED | SSTS_TARG_CONNECTED |
                   SSTS_SPC_BUSY | SSTS_XFER_IN_PROGRESS);
    spc->ints = INTS_DISCONNECTED;
    update_ints(spc);
    update_state(spc, SPC_STATE_IDLE, 0);
}

/*
 * Main state machine step
 */
static void spc_step(MB89352 *spc, int timeout)
{
    DWORD ctrl = scsi_get_ctrl(spc);
    BYTE data = spc->bus ? SCSI_BUS_GetData(spc->bus) : 0;

    SPC_DEBUG("Step: state=%s, ctrl=0x%03X, data=0x%02X, tc=%d\n",
        SPC_GetStateName(spc->state), ctrl, data, spc->tc);

    /* Check for bus free */
    if ((ctrl & (SCSI_SIGNAL_BSY | SCSI_SIGNAL_SEL)) == 0) {
        if ((spc->ssts & SSTS_INIT_CONNECTED) && spc->bus_free_timer == 0) {
            spc->bus_free_timer = SPC_BUS_FREE_DELAY;
        }
    } else {
        spc->bus_free_timer = 0;
    }

    /* Handle reset and disable */
    if ((spc->sctl & SCTL_RESET_AND_DISABLE) && spc->state != SPC_STATE_IDLE) {
        scsi_set_ctrl(spc, 0, SCSI_SIGNAL_ALL);
        spc->ssts &= ~SSTS_SPC_BUSY;
        fifo_clear(spc);
        update_state(spc, SPC_STATE_IDLE, 0);
        return;
    }

    switch (spc->state) {
    case SPC_STATE_IDLE:
        if (ctrl == 0 && (spc->pctl & PCTL_BUS_FREE_IE)) {
            spc->ints |= INTS_DISCONNECTED;
            update_ints(spc);
        }
        break;

    case SPC_STATE_ARBITRATION_WAIT_BUS_FREE:
        if (!(ctrl & (SCSI_SIGNAL_BSY | SCSI_SIGNAL_SEL))) {
            update_state(spc, SPC_STATE_ARBITRATION_ASSERT_BSY, SPC_DELAY_SHORT);
        }
        break;

    case SPC_STATE_ARBITRATION_ASSERT_BSY:
        scsi_set_ctrl(spc, SCSI_SIGNAL_BSY, SCSI_SIGNAL_BSY);
        if (spc->bus) {
            SCSI_BUS_SetData(spc->bus, spc->bus_id, 1 << spc->bdid);
        }
        update_state(spc, SPC_STATE_ARBITRATION_WAIT, SPC_DELAY_MEDIUM);
        break;

    case SPC_STATE_ARBITRATION_WAIT:
        {
            /* Check if we won arbitration (no higher ID) */
            int id;
            int won = 1;
            for (id = spc->bdid + 1; id < 8; id++) {
                if (data & (1 << id)) {
                    won = 0;
                    SPC_DEBUG("Arbitration lost to ID %d\n", id);
                    break;
                }
            }
            if (won) {
                SPC_DEBUG("Arbitration won\n");
                update_state(spc, SPC_STATE_ARBITRATION_ASSERT_SEL, SPC_DELAY_SHORT);
            } else {
                scsi_set_ctrl(spc, 0, SCSI_SIGNAL_BSY);
                if (spc->bus) {
                    SCSI_BUS_SetData(spc->bus, spc->bus_id, 0);
                }
                spc->ssts &= ~SSTS_SPC_BUSY;
                update_state(spc, SPC_STATE_IDLE, 0);
            }
        }
        break;

    case SPC_STATE_ARBITRATION_ASSERT_SEL:
        scsi_set_ctrl(spc, SCSI_SIGNAL_SEL, SCSI_SIGNAL_SEL);
        update_state(spc, SPC_STATE_SELECTION_ASSERT_ID, SPC_DELAY_SHORT);
        break;

    case SPC_STATE_SELECTION_WAIT_BUS_FREE:
        if (!(ctrl & (SCSI_SIGNAL_BSY | SCSI_SIGNAL_SEL))) {
            update_state(spc, SPC_STATE_SELECTION_ASSERT_ID, SPC_DELAY_SHORT);
        }
        break;

    case SPC_STATE_SELECTION_ASSERT_ID:
        spc->ssts |= SSTS_INIT_CONNECTED;
        spc->ssts &= ~SSTS_TARG_CONNECTED;
        if (spc->bus) {
            SCSI_BUS_SetData(spc->bus, spc->bus_id, spc->temp);
        }
        update_state(spc, SPC_STATE_SELECTION_ASSERT_SEL, SPC_DELAY_SHORT);
        break;

    case SPC_STATE_SELECTION_ASSERT_SEL:
        {
            DWORD sel_ctrl = SCSI_SIGNAL_SEL;
            if (spc->send_atn_during_selection) {
                sel_ctrl |= SCSI_SIGNAL_ATN;
            }
            scsi_set_ctrl(spc, sel_ctrl, SCSI_SIGNAL_ATN | SCSI_SIGNAL_SEL | SCSI_SIGNAL_BSY);
            spc->timer_count = 0;
            spc->timer_target = ((spc->tc & ~0xFF) + 15) * 2;  /* Selection timeout */
            update_state(spc, SPC_STATE_SELECTION_WAIT_BSY, 0);
        }
        break;

    case SPC_STATE_SELECTION_WAIT_BSY:
        if (timeout || (spc->ints & INTS_SPC_TIMEOUT)) {
            SPC_DEBUG("Selection timeout\n");
            spc->tc = 0;
            spc->ints = INTS_SPC_TIMEOUT;
            update_ints(spc);
            scsi_set_ctrl(spc, 0, SCSI_SIGNAL_ALL);
            if (spc->bus) {
                SCSI_BUS_SetData(spc->bus, spc->bus_id, 0);
            }
            spc->ssts &= ~(SSTS_INIT_CONNECTED | SSTS_SPC_BUSY);
            update_state(spc, SPC_STATE_IDLE, 0);
            break;
        }
        spc->timer_target = 0;
        if ((ctrl & (SCSI_SIGNAL_REQ | SCSI_SIGNAL_BSY | SCSI_SIGNAL_MSG |
                     SCSI_SIGNAL_CD | SCSI_SIGNAL_IO)) == SCSI_SIGNAL_BSY) {
            update_state(spc, SPC_STATE_SELECTION, SPC_DELAY_SHORT);
        }
        break;

    case SPC_STATE_SELECTION:
        /* Avoid duplicate completion from SEL deassertion */
        if (!(ctrl & SCSI_SIGNAL_SEL))
            break;
        SPC_DEBUG("Selection success\n");
        scsi_set_ctrl(spc, 0, SCSI_SIGNAL_SEL);
        scsi_command_complete(spc);
        break;

    case SPC_STATE_TRANSFER_WAIT_REQ:
        if (!(ctrl & SCSI_SIGNAL_REQ)) {
            break;
        }

        /* Check for phase change */
        if (spc->scsi_phase != (int)(ctrl & SCSI_PHASE_MASK)) {
            SPC_DEBUG("Phase change during transfer\n");
            spc->ints |= INTS_SERVICE_REQUIRED;
            spc->ssts &= ~SSTS_SPC_BUSY;
            if (spc->bus) {
                SCSI_BUS_SetData(spc->bus, spc->bus_id, 0);
            }
            update_ints(spc);
            update_state(spc, SPC_STATE_IDLE, 0);
            break;
        }

        /* Size 0 transfer just checks phase */
        if (!spc->tc && !(spc->scmd & SCMD_TERM_MODE)) {
            if (spc->bus) {
                SCSI_BUS_SetData(spc->bus, spc->bus_id, 0);
            }
            scsi_command_complete(spc);
            break;
        }

        if (ctrl & SCSI_SIGNAL_IO) {
            update_state(spc, SPC_STATE_TRANSFER_RECV_DATA, 0);
        } else {
            update_state(spc, SPC_STATE_TRANSFER_SEND_DATA, 0);
        }

        /* Request DMA if needed */
        if (spc->dma_transfer && spc->tc && !(ctrl & SCSI_SIGNAL_IO) && !fifo_full(spc)) {
            if (spc->drq_callback) {
                spc->drq_callback(1, spc->drq_param);
            }
        }
        spc_step(spc, 0);  /* Continue processing */
        break;

    case SPC_STATE_TRANSFER_RECV_DATA:
        if (!spc->tc && (spc->scmd & SCMD_TERM_MODE)) {
            update_state(spc, SPC_STATE_TRANSFER_SEND_ACK, SPC_DELAY_SHORT);
            break;
        }

        if (!spc->tc || fifo_full(spc)) {
            break;
        }

        SPC_DEBUG("FIFO push: 0x%02X\n", data);
        fifo_push(spc, data);

        if (spc->sdgc & SDGC_XFER_ENABLE) {
            spc->serr |= SERR_XFER_OUT;
            update_ints(spc);
        }

        update_state(spc, SPC_STATE_TRANSFER_SEND_ACK, SPC_DELAY_SHORT);

        if (spc->dma_transfer && spc->drq_callback) {
            spc->drq_callback(1, spc->drq_param);
        }
        break;

    case SPC_STATE_TRANSFER_SEND_DATA:
        if (spc->tc && fifo_empty(spc) && (spc->sdgc & SDGC_XFER_ENABLE)) {
            spc->serr |= SERR_XFER_OUT;
            update_ints(spc);
            break;
        }

        if (spc->tc && !fifo_empty(spc)) {
            BYTE out_data = fifo_pop(spc);
            SPC_DEBUG("FIFO pop: 0x%02X\n", out_data);
            if (spc->bus) {
                SCSI_BUS_SetData(spc->bus, spc->bus_id, out_data);
            }
            update_state(spc, SPC_STATE_TRANSFER_SEND_ACK, SPC_DELAY_SHORT);
            break;
        }

        if (!spc->tc && (spc->scmd & SCMD_TERM_MODE)) {
            if (spc->bus) {
                SCSI_BUS_SetData(spc->bus, spc->bus_id, spc->temp);
            }
            update_state(spc, SPC_STATE_TRANSFER_SEND_ACK, SPC_DELAY_SHORT);
            break;
        }
        break;

    case SPC_STATE_TRANSFER_SEND_ACK:
        if (!(spc->scmd & SCMD_TERM_MODE) && !(ctrl & SCSI_SIGNAL_IO)) {
            spc->temp = data;
        }
        scsi_set_ctrl(spc, SCSI_SIGNAL_ACK, SCSI_SIGNAL_ACK);
        update_state(spc, SPC_STATE_TRANSFER_WAIT_DEASSERT_REQ, SPC_DELAY_SHORT);
        break;

    case SPC_STATE_TRANSFER_WAIT_DEASSERT_REQ:
        if (!(ctrl & SCSI_SIGNAL_REQ)) {
            update_state(spc, SPC_STATE_TRANSFER_DEASSERT_ACK, SPC_DELAY_SHORT);
        }
        break;

    case SPC_STATE_TRANSFER_DEASSERT_ACK:
        spc->tc--;
        if (spc->tc) {
            update_state(spc, SPC_STATE_TRANSFER_WAIT_REQ, SPC_DELAY_SHORT);
        } else {
            update_state(spc, SPC_STATE_TRANSFER_WAIT_FIFO_EMPTY, SPC_DELAY_SHORT);
        }

        /* Deassert ATN after last byte of MSG OUT */
        if (!spc->tc && (ctrl & SCSI_PHASE_MASK) == SCSI_PHASE_MSG_OUT &&
            spc->send_atn_during_selection) {
            scsi_set_ctrl(spc, 0, SCSI_SIGNAL_ATN | SCSI_SIGNAL_ACK);
        } else if (spc->tc || (ctrl & SCSI_PHASE_MASK) != SCSI_PHASE_MSG_IN) {
            scsi_set_ctrl(spc, 0, SCSI_SIGNAL_ACK);
        }
        break;

    case SPC_STATE_TRANSFER_WAIT_FIFO_EMPTY:
        if (!fifo_empty(spc)) {
            break;
        }
        if (spc->bus) {
            SCSI_BUS_SetData(spc->bus, spc->bus_id, 0);
        }
        scsi_command_complete(spc);
        break;
    }
}

/*
 * Initialize SPC
 */
void SPC_Init(MB89352 *spc)
{
    memset(spc, 0, sizeof(MB89352));
    spc->sctl = SCTL_RESET_AND_DISABLE;
    spc->state = SPC_STATE_IDLE;
    spc->bus_id = 7;  /* Default initiator ID */
    SPC_DEBUG("Initialized\n");
}

/*
 * Reset SPC
 */
void SPC_Reset(MB89352 *spc)
{
    SPC_DEBUG("Reset\n");

    spc->bdid = 0;
    spc->sctl = SCTL_RESET_AND_DISABLE;
    spc->scmd = 0;
    spc->ints = 0;
    spc->sdgc = 0;
    spc->ssts = 0;
    spc->serr = 0;
    spc->pctl = 0;
    spc->mbc = 0;
    spc->dreg = 0;
    spc->temp = 0;
    spc->tc = 0;

    spc->state = SPC_STATE_IDLE;
    spc->delay_state = SPC_STATE_IDLE;
    spc->scsi_phase = 0;
    spc->scsi_ctrl = 0;

    fifo_clear(spc);

    spc->send_atn_during_selection = 0;
    spc->dma_transfer = 0;
    spc->irq_state = 0;

    spc->timer_count = 0;
    spc->timer_target = 0;
    spc->delay_timer_count = 0;
    spc->delay_timer_target = 0;
    spc->bus_free_timer = 0;

    /* Clear signals on bus */
    if (spc->bus) {
        SCSI_BUS_SetCtrl(spc->bus, spc->bus_id, 0, SCSI_SIGNAL_ALL);
        SCSI_BUS_SetData(spc->bus, spc->bus_id, 0);
    }

    /* Deassert IRQ and DRQ */
    if (spc->drq_callback) {
        spc->drq_callback(0, spc->drq_param);
    }

    update_ssts(spc);
    update_ints(spc);
}

/*
 * Cleanup SPC
 */
void SPC_Cleanup(MB89352 *spc)
{
    spc->bus = NULL;
    SPC_DEBUG("Cleanup\n");
}

/*
 * Connect to SCSI bus
 */
void SPC_ConnectBus(MB89352 *spc, SCSI_BUS *bus, int id)
{
    spc->bus = bus;
    spc->bus_id = id;
    SPC_DEBUG("Connected to bus, ID=%d\n", id);
}

/*
 * Set IRQ callback
 */
void SPC_SetIRQCallback(MB89352 *spc, void (*callback)(int, void*), void *param)
{
    spc->irq_callback = callback;
    spc->irq_param = param;
}

/*
 * Set DRQ callback
 */
void SPC_SetDRQCallback(MB89352 *spc, void (*callback)(int, void*), void *param)
{
    spc->drq_callback = callback;
    spc->drq_param = param;
}

/*
 * Read register
 */
BYTE SPC_ReadReg(MB89352 *spc, DWORD reg)
{
    BYTE ret = 0;

    switch (reg) {
    case SPC_REG_BDID:
        ret = 1 << spc->bdid;
        break;

    case SPC_REG_SCTL:
        ret = spc->sctl;
        break;

    case SPC_REG_SCMD:
        ret = spc->scmd;
        break;

    case SPC_REG_INTS:
        ret = spc->ints;
        break;

    case SPC_REG_PSNS:
        {
            DWORD ctrl = scsi_get_ctrl(spc);
            ret = ((ctrl & SCSI_SIGNAL_REQ) ? 0x80 : 0) |
                  ((ctrl & SCSI_SIGNAL_ACK) ? 0x40 : 0) |
                  ((ctrl & SCSI_SIGNAL_ATN) ? 0x20 : 0) |
                  ((ctrl & SCSI_SIGNAL_SEL) ? 0x10 : 0) |
                  ((ctrl & SCSI_SIGNAL_BSY) ? 0x08 : 0) |
                  ((ctrl & SCSI_SIGNAL_MSG) ? 0x04 : 0) |
                  ((ctrl & SCSI_SIGNAL_CD)  ? 0x02 : 0) |
                  ((ctrl & SCSI_SIGNAL_IO)  ? 0x01 : 0);
        }
        break;

    case SPC_REG_SSTS:
        update_ssts(spc);
        ret = spc->ssts;
        break;

    case SPC_REG_SERR:
        ret = spc->serr;
        break;

    case SPC_REG_PCTL:
        ret = spc->pctl;
        break;

    case SPC_REG_MBC:
        ret = spc->mbc;
        break;

    case SPC_REG_DREG:
        if (!fifo_empty(spc)) {
            spc->dreg = fifo_pop(spc);
        }
        if (spc->serr & SERR_XFER_OUT) {
            spc->serr &= ~SERR_XFER_OUT;
            update_ints(spc);
        }
        spc_step(spc, 0);
        ret = spc->dreg;
        break;

    case SPC_REG_TEMP:
        spc_step(spc, 0);
        ret = spc->temp;
        break;

    case SPC_REG_TCH:
        ret = (spc->tc >> 16) & 0xFF;
        break;

    case SPC_REG_TCM:
        ret = (spc->tc >> 8) & 0xFF;
        break;

    case SPC_REG_TCL:
        ret = spc->tc & 0xFF;
        break;

    default:
        ret = 0xFF;
        break;
    }

    SPC_DEBUG("Read reg 0x%02X = 0x%02X\n", reg, ret);
    return ret;
}

/*
 * Write register
 */
void SPC_WriteReg(MB89352 *spc, DWORD reg, BYTE data)
{
    SPC_DEBUG("Write reg 0x%02X = 0x%02X\n", reg, data);

    switch (reg) {
    case SPC_REG_BDID:
        spc->bdid = data & 0x07;
        break;

    case SPC_REG_SCTL:
        spc->sctl = data;
        update_ints(spc);
        break;

    case SPC_REG_SCMD:
        spc->scmd = data;

        if (!(spc->sctl & SCTL_RESET_AND_DISABLE)) {
            scsi_set_ctrl(spc, (data & SCMD_RST_OUT) ? SCSI_SIGNAL_RST : 0, SCSI_SIGNAL_RST);
        }

        switch (data & SCMD_CMD_MASK) {
        case SCMD_CMD_BUS_RELEASE:
            SPC_DEBUG("CMD: Bus Release\n");
            spc->send_atn_during_selection = 0;
            if (spc->state == SPC_STATE_SELECTION_WAIT_BUS_FREE) {
                spc->ssts &= ~(SSTS_INIT_CONNECTED | SSTS_TARG_CONNECTED | SSTS_SPC_BUSY);
                update_state(spc, SPC_STATE_IDLE, 0);
            }
            break;

        case SCMD_CMD_SELECT:
            SPC_DEBUG("CMD: Select\n");
            spc->ssts |= SSTS_SPC_BUSY;
            if (spc->sctl & SCTL_ARBITRATION_ENABLE) {
                update_state(spc, SPC_STATE_ARBITRATION_WAIT_BUS_FREE, SPC_DELAY_SHORT);
            } else {
                update_state(spc, SPC_STATE_SELECTION_WAIT_BUS_FREE, SPC_DELAY_SHORT);
            }
            spc_step(spc, 0);
            break;

        case SCMD_CMD_RESET_ATN:
            SPC_DEBUG("CMD: Reset ATN\n");
            spc->send_atn_during_selection = 0;
            scsi_set_ctrl(spc, 0, SCSI_SIGNAL_ATN);
            break;

        case SCMD_CMD_SET_ATN:
            SPC_DEBUG("CMD: Set ATN\n");
            if (spc->state == SPC_STATE_IDLE) {
                spc->send_atn_during_selection = 1;
            } else {
                scsi_set_ctrl(spc, SCSI_SIGNAL_ATN, SCSI_SIGNAL_ATN);
            }
            break;

        case SCMD_CMD_TRANSFER:
            if (!(spc->ssts & (SSTS_INIT_CONNECTED | SSTS_TARG_CONNECTED))) {
                break;
            }
            spc->dma_transfer = !(data & SCMD_PRG_XFER);
            SPC_DEBUG("CMD: %s Transfer\n", spc->dma_transfer ? "DMA" : "Program");
            if (!spc->dma_transfer && spc->drq_callback) {
                spc->drq_callback(0, spc->drq_param);
            }
            spc->ssts |= SSTS_SPC_BUSY | SSTS_XFER_IN_PROGRESS;
            update_state(spc, SPC_STATE_TRANSFER_WAIT_REQ, SPC_DELAY_SHORT);
            break;

        case SCMD_CMD_TRANSFER_PAUSE:
            SPC_DEBUG("CMD: Transfer Pause\n");
            break;

        case SCMD_CMD_RESET_ACK_REQ:
            SPC_DEBUG("CMD: Reset ACK/REQ\n");
            if (spc->ssts & SSTS_INIT_CONNECTED) {
                scsi_set_ctrl(spc, 0, SCSI_SIGNAL_ACK);
            }
            if (spc->ssts & SSTS_TARG_CONNECTED) {
                scsi_set_ctrl(spc, 0, SCSI_SIGNAL_REQ);
            }
            break;

        case SCMD_CMD_SET_ACK_REQ:
            SPC_DEBUG("CMD: Set ACK/REQ\n");
            if (spc->ssts & SSTS_INIT_CONNECTED) {
                DWORD ctrl = scsi_get_ctrl(spc);
                if (ctrl & SCSI_SIGNAL_IO) {
                    spc->temp = spc->bus ? SCSI_BUS_GetData(spc->bus) : 0;
                } else {
                    if (spc->bus) {
                        SCSI_BUS_SetData(spc->bus, spc->bus_id, spc->temp);
                    }
                }
                scsi_set_ctrl(spc, SCSI_SIGNAL_ACK, SCSI_SIGNAL_ACK);
            }
            if (spc->ssts & SSTS_TARG_CONNECTED) {
                scsi_set_ctrl(spc, SCSI_SIGNAL_REQ, SCSI_SIGNAL_REQ);
            }
            break;
        }
        break;

    case SPC_REG_INTS:
        if (spc->state == SPC_STATE_SELECTION_WAIT_BSY && (spc->ints & data & INTS_SPC_TIMEOUT)) {
            if (!spc->tc) {
                /* Terminate selection */
                spc->ssts &= ~(SSTS_INIT_CONNECTED | SSTS_TARG_CONNECTED | SSTS_SPC_BUSY);
                scsi_set_ctrl(spc, 0, SCSI_SIGNAL_ALL);
                if (spc->bus) {
                    SCSI_BUS_SetData(spc->bus, spc->bus_id, 0);
                }
                update_state(spc, SPC_STATE_IDLE, 0);
            } else {
                /* Restart selection */
                update_state(spc, SPC_STATE_SELECTION_ASSERT_ID, SPC_DELAY_SHORT);
            }
        }
        spc->ints &= ~data;
        update_ints(spc);
        break;

    case SPC_REG_SDGC:
        spc->sdgc = data;
        spc_step(spc, 0);
        update_ints(spc);
        break;

    case SPC_REG_PCTL:
        spc->pctl = data;
        spc->scsi_phase = data & PCTL_PHASE_MASK;
        if (data & PCTL_BUS_FREE_IE) {
            spc_step(spc, 0);
        }
        break;

    case SPC_REG_DREG:
        spc->dreg = data;
        if (!fifo_full(spc)) {
            fifo_push(spc, data);
        }
        if (spc->serr & SERR_XFER_OUT) {
            spc->serr &= ~SERR_XFER_OUT;
            update_ints(spc);
        }
        spc_step(spc, 0);
        break;

    case SPC_REG_TEMP:
        spc->temp = data;
        spc_step(spc, 0);
        break;

    case SPC_REG_TCH:
        spc->tc = (spc->tc & 0x00FFFF) | ((DWORD)data << 16);
        update_ssts(spc);
        break;

    case SPC_REG_TCM:
        spc->tc = (spc->tc & 0xFF00FF) | ((DWORD)data << 8);
        update_ssts(spc);
        break;

    case SPC_REG_TCL:
        spc->tc = (spc->tc & 0xFFFF00) | data;
        update_ssts(spc);
        break;
    }
}

/*
 * DMA read (for DMA controller)
 */
BYTE SPC_DMA_Read(MB89352 *spc)
{
    BYTE data;

    if (!fifo_empty(spc)) {
        data = fifo_pop(spc);
    } else {
        data = spc->dreg;
    }

    SPC_DEBUG("DMA Read: 0x%02X\n", data);

    if (fifo_empty(spc) && spc->drq_callback) {
        spc->drq_callback(0, spc->drq_param);
    }

    spc_step(spc, 0);
    return data;
}

/*
 * DMA write (from DMA controller)
 */
void SPC_DMA_Write(MB89352 *spc, BYTE data)
{
    SPC_DEBUG("DMA Write: 0x%02X\n", data);

    spc->dreg = data;
    if (!fifo_full(spc)) {
        fifo_push(spc, data);
        if (fifo_full(spc) && spc->drq_callback) {
            spc->drq_callback(0, spc->drq_param);
        }
    }

    spc_step(spc, 0);
}

/*
 * Bus control changed notification
 */
void SPC_BusCtrlChanged(MB89352 *spc)
{
    spc_step(spc, 0);
}

/*
 * Execute cycles (for timer handling)
 */
void SPC_Exec(MB89352 *spc, int cycles)
{
    /* Handle delay timer */
    if (spc->delay_timer_target > 0) {
        spc->delay_timer_count += cycles;
        if (spc->delay_timer_count >= spc->delay_timer_target) {
            spc->delay_timer_target = 0;
            spc->state = spc->delay_state;
            spc_step(spc, 0);
        }
    }

    /* Handle selection timeout timer */
    if (spc->timer_target > 0) {
        spc->timer_count += cycles;
        if (spc->timer_count >= spc->timer_target) {
            spc->timer_target = 0;
            spc_step(spc, 1);  /* timeout = 1 */
        }
    }

    /* Handle bus free detection */
    if (spc->bus_free_timer > 0) {
        if (spc->bus_free_timer <= (DWORD)cycles) {
            spc->bus_free_timer = 0;
            if (spc->ssts & SSTS_INIT_CONNECTED) {
                SPC_DEBUG("Bus free detected, disconnect\n");
                scsi_disconnect(spc);
                scsi_set_ctrl(spc, 0, SCSI_SIGNAL_ALL);
            }
        } else {
            spc->bus_free_timer -= cycles;
        }
    }
}
