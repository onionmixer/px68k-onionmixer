// ---------------------------------------------------------------------------------------
//  SCC.C - Z8530 SCC (Mouse on Channel B, RS-232C on Channel A)
//
//  Enhanced implementation with:
//  - Baud rate decoding from WR12/WR13/WR4
//  - Data bits, stop bits, parity configuration
//  - 3-byte RX FIFO (like real Z8530)
//  - TX buffer empty interrupt
//  - External status change interrupt (DCD/CTS)
//  - WR0 command handling
//  - Break signal support
// ---------------------------------------------------------------------------------------

#include <stdio.h>
#include "common.h"
#include "scc.h"
#include "serial.h"
#include "m68000.h"
#include "irqh.h"
#include "mouse.h"

/* Mouse state */
signed char MouseX = 0;
signed char MouseY = 0;
BYTE MouseSt = 0;

/* Channel A (RS-232C) registers - WR0-WR15 */
BYTE SCC_RegsA[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
BYTE SCC_RegNumA = 0;
BYTE SCC_RegSetA = 0;

/* Channel B (Mouse) registers */
BYTE SCC_RegsB[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
BYTE SCC_RegNumB = 0;
BYTE SCC_RegSetB = 0;

/* Common */
BYTE SCC_Vector = 0;

/* Mouse data buffer (Channel B) */
BYTE SCC_Dat[3] = {0, 0, 0};
BYTE SCC_DatNum = 0;

/* Channel A (RS-232C) RX FIFO - 3 bytes like real Z8530 */
#define SCC_RX_FIFO_SIZE 3
static BYTE SCC_RxFifoA[SCC_RX_FIFO_SIZE];
static int SCC_RxFifoHeadA = 0;
static int SCC_RxFifoTailA = 0;
static int SCC_RxFifoCountA = 0;

/* Channel A TX state */
static BYTE SCC_TxEmptyA = 1;      /* TX buffer empty */
static BYTE SCC_TxUnderrunA = 0;   /* TX underrun/EOM */

/* External status tracking for interrupt */
static BYTE SCC_LastExtStatusA = 0;
static BYTE SCC_ExtStatusChangeA = 0;

/* Break state */
static BYTE SCC_BreakingA = 0;

/* Interrupt pending flags */
static BYTE SCC_IntPendingA = 0;
#define SCC_INT_RX      0x04    /* RX char available */
#define SCC_INT_TX      0x02    /* TX buffer empty */
#define SCC_INT_EXT     0x01    /* External/status change */

/* X68000 SCC clock is 5MHz (main clock divided) */
#define SCC_CLOCK  5000000

// -----------------------------------------------------------------------
//   RX FIFO operations
// -----------------------------------------------------------------------
static void SCC_RxFifoPush(BYTE data)
{
	if (SCC_RxFifoCountA < SCC_RX_FIFO_SIZE) {
		SCC_RxFifoA[SCC_RxFifoTailA] = data;
		SCC_RxFifoTailA = (SCC_RxFifoTailA + 1) % SCC_RX_FIFO_SIZE;
		SCC_RxFifoCountA++;
	}
	/* If FIFO full, data is lost (overrun) */
}

static BYTE SCC_RxFifoPop(void)
{
	BYTE data = 0;
	if (SCC_RxFifoCountA > 0) {
		data = SCC_RxFifoA[SCC_RxFifoHeadA];
		SCC_RxFifoHeadA = (SCC_RxFifoHeadA + 1) % SCC_RX_FIFO_SIZE;
		SCC_RxFifoCountA--;
	}
	return data;
}

static void SCC_RxFifoClear(void)
{
	SCC_RxFifoHeadA = 0;
	SCC_RxFifoTailA = 0;
	SCC_RxFifoCountA = 0;
}

// -----------------------------------------------------------------------
//   Calculate baud rate from SCC registers
//   Baud = Clock / (2 * (TC + 2) * ClockMultiplier)
//   TC = (WR13 << 8) | WR12
//   ClockMultiplier from WR4 bits 6-7: 00=x1, 01=x16, 10=x32, 11=x64
// -----------------------------------------------------------------------
static int SCC_CalcBaudRate(void)
{
	int tc = (SCC_RegsA[13] << 8) | SCC_RegsA[12];
	int clockMult = 1;
	int baud;

	switch ((SCC_RegsA[4] >> 6) & 0x03) {
		case 0: clockMult = 1; break;
		case 1: clockMult = 16; break;
		case 2: clockMult = 32; break;
		case 3: clockMult = 64; break;
	}

	if (tc == 0 && clockMult == 1) {
		/* Probably not configured yet */
		return 9600;
	}

	baud = SCC_CLOCK / (2 * (tc + 2) * clockMult);

	/* Clamp to common baud rates */
	if (baud <= 300) return 300;
	else if (baud <= 600) return 600;
	else if (baud <= 1200) return 1200;
	else if (baud <= 2400) return 2400;
	else if (baud <= 4800) return 4800;
	else if (baud <= 9600) return 9600;
	else if (baud <= 19200) return 19200;
	else if (baud <= 38400) return 38400;
	else if (baud <= 57600) return 57600;
	else return 115200;
}

// -----------------------------------------------------------------------
//   Get data bits from WR3 (RX) or WR5 (TX)
//   WR3 bits 6-7: 00=5, 01=7, 10=6, 11=8
//   WR5 bits 5-6: same encoding
// -----------------------------------------------------------------------
static int SCC_GetDataBits(void)
{
	int rxBits = (SCC_RegsA[3] >> 6) & 0x03;
	int dataBits;

	switch (rxBits) {
		case 0: dataBits = 5; break;
		case 1: dataBits = 7; break;
		case 2: dataBits = 6; break;
		case 3: dataBits = 8; break;
		default: dataBits = 8; break;
	}
	return dataBits;
}

// -----------------------------------------------------------------------
//   Get stop bits from WR4 bits 2-3
//   00=sync mode, 01=1 stop, 10=1.5 stop, 11=2 stop
// -----------------------------------------------------------------------
static int SCC_GetStopBits(void)
{
	int stopBits = (SCC_RegsA[4] >> 2) & 0x03;
	switch (stopBits) {
		case 0: return 1;  /* sync mode - use 1 */
		case 1: return 1;
		case 2: return 1;  /* 1.5 - round to 1 */
		case 3: return 2;
		default: return 1;
	}
}

// -----------------------------------------------------------------------
//   Get parity from WR4 bits 0-1
//   Bit 0: parity enable, Bit 1: even/odd (0=odd, 1=even)
// -----------------------------------------------------------------------
static int SCC_GetParity(void)
{
	if (!(SCC_RegsA[4] & 0x01)) {
		return SERIAL_PARITY_NONE;
	}
	return (SCC_RegsA[4] & 0x02) ? SERIAL_PARITY_EVEN : SERIAL_PARITY_ODD;
}

// -----------------------------------------------------------------------
//   Apply serial configuration to host port
// -----------------------------------------------------------------------
static void SCC_ApplyConfig(void)
{
	if (!Serial_IsOpen()) return;

	int baud = SCC_CalcBaudRate();
	int dataBits = SCC_GetDataBits();
	int stopBits = SCC_GetStopBits();
	int parity = SCC_GetParity();

	Serial_SetConfig(baud, dataBits, stopBits, parity);
}

// -----------------------------------------------------------------------
//   Get current external status (for change detection)
// -----------------------------------------------------------------------
static BYTE SCC_GetExtStatus(void)
{
	BYTE status = 0;
	if (Serial_IsOpen()) {
		if (Serial_GetCTS()) status |= 0x20;  /* Bit 5: CTS */
		if (Serial_GetDCD()) status |= 0x08;  /* Bit 3: DCD */
	}
	return status;
}

// -----------------------------------------------------------------------
//   Interrupt callback
// -----------------------------------------------------------------------
DWORD FASTCALL SCC_Int(BYTE irq)
{
	DWORD ret = (DWORD)(-1);
	IRQH_IRQCallBack(irq);
	if ( (irq==5)&&(!(SCC_RegsB[9]&2)) )
	{
		if (SCC_RegsB[9]&1)
		{
			/* Status affects vector - use original logic for Channel B (mouse) */
			/* Channel B has priority in original implementation */
			if (SCC_RegsB[9]&0x10)
				ret = ((DWORD)(SCC_Vector&0x8f)+0x20);
			else
				ret = ((DWORD)(SCC_Vector&0xf1)+4);
		}
		else
			ret = ((DWORD)SCC_Vector);
	}

	return ret;
}


// -----------------------------------------------------------------------
//   Check for interrupts
// -----------------------------------------------------------------------
void SCC_IntCheck(void)
{
	BYTE doInt = 0;

	/* Channel B (Mouse) interrupt check */
	if ( (SCC_DatNum) && ((SCC_RegsB[1]&0x18)==0x10) && (SCC_RegsB[9]&0x08) )
	{
		doInt = 1;
	}
	else if ( (SCC_DatNum==3) && ((SCC_RegsB[1]&0x18)==0x08) && (SCC_RegsB[9]&0x08) )
	{
		doInt = 1;
	}

	/* Channel A (RS-232C) - only process when serial port is open */
	SCC_IntPendingA = 0;

	if (Serial_IsOpen()) {
		/* Check for external status changes */
		BYTE curExtStatus = SCC_GetExtStatus();
		if (curExtStatus != SCC_LastExtStatusA) {
			SCC_ExtStatusChangeA = 1;
			SCC_LastExtStatusA = curExtStatus;
		}

		/* Read data into FIFO if space available */
		while (SCC_RxFifoCountA < SCC_RX_FIFO_SIZE) {
			BYTE data;
			if (Serial_Read(&data) == 0) {
				SCC_RxFifoPush(data);
			} else {
				break;  /* No more data */
			}
		}

		/* RX interrupt - only when serial is open */
		if (SCC_RxFifoCountA > 0) {
			BYTE rxIntMode = (SCC_RegsA[1] >> 3) & 0x03;
			if (rxIntMode == 0x01 || rxIntMode == 0x02) {
				SCC_IntPendingA |= SCC_INT_RX;
			}
		}

		/* TX interrupt - WR1 bit 1, only when serial is open */
		if ((SCC_RegsA[1] & 0x02) && SCC_TxEmptyA) {
			SCC_IntPendingA |= SCC_INT_TX;
		}

		/* External/Status interrupt - WR1 bit 0 */
		if ((SCC_RegsA[1] & 0x01) && SCC_ExtStatusChangeA) {
			SCC_IntPendingA |= SCC_INT_EXT;
		}

		/* Generate Channel A interrupt if master enable and any pending */
		if ((SCC_RegsB[9] & 0x08) && SCC_IntPendingA) {
			doInt = 1;
		}
	}

	if (doInt) {
		IRQH_Int(5, &SCC_Int);
	}
}


// -----------------------------------------------------------------------
//   Initialize
// -----------------------------------------------------------------------
void SCC_Init(void)
{
	int i;

	MouseX = 0;
	MouseY = 0;
	MouseSt = 0;

	for (i = 0; i < 16; i++) {
		SCC_RegsA[i] = 0;
		SCC_RegsB[i] = 0;
	}

	SCC_RegNumA = 0;
	SCC_RegSetA = 0;
	SCC_RegNumB = 0;
	SCC_RegSetB = 0;
	SCC_Vector = 0;
	SCC_DatNum = 0;

	SCC_RxFifoClear();
	SCC_TxEmptyA = 1;
	SCC_TxUnderrunA = 0;
	SCC_LastExtStatusA = 0;
	SCC_ExtStatusChangeA = 0;
	SCC_BreakingA = 0;
	SCC_IntPendingA = 0;
}


// -----------------------------------------------------------------------
//   Handle WR0 commands (only used when Serial is connected)
// -----------------------------------------------------------------------
static void SCC_HandleWR0Command(BYTE data)
{
	BYTE cmd = (data >> 3) & 0x07;

	switch (cmd) {
		case 0:  /* Null command */
			break;
		case 1:  /* Point high (set pointer to WR8-15) */
			SCC_RegNumA |= 0x08;
			break;
		case 2:  /* Reset Ext/Status interrupts */
			SCC_ExtStatusChangeA = 0;
			SCC_LastExtStatusA = SCC_GetExtStatus();
			break;
		case 3:  /* Send abort (SDLC) - not used for async */
			break;
		case 4:  /* Enable Int on next Rx char */
			break;
		case 5:  /* Reset Tx Int pending */
			SCC_IntPendingA &= ~SCC_INT_TX;
			SCC_TxEmptyA = 0;
			break;
		case 6:  /* Error reset */
			break;
		case 7:  /* Reset highest IUS */
			break;
	}
}


// -----------------------------------------------------------------------
//   I/O Write
// -----------------------------------------------------------------------
void FASTCALL SCC_Write(DWORD adr, BYTE data)
{
	if (adr>=0xe98008) return;

	/* Channel B Control (0xE98001) */
	if ((adr&7) == 1)
	{
		if (SCC_RegSetB)
		{
			if (SCC_RegNumB == 5)
			{
				/* Mouse data generation when TX enable goes high */
				if ( (!(SCC_RegsB[5]&2))&&(data&2)&&(SCC_RegsB[3]&1)&&(!SCC_DatNum) )
				{
					Mouse_SetData();
					SCC_DatNum = 3;
					SCC_Dat[2] = MouseSt;
					SCC_Dat[1] = MouseX;
					SCC_Dat[0] = MouseY;
				}
			}
			else if (SCC_RegNumB == 2) SCC_Vector = data;
			SCC_RegSetB = 0;
			SCC_RegsB[SCC_RegNumB] = data;
			SCC_RegNumB = 0;
		}
		else
		{
			if (!(data&0xf0))
			{
				data &= 15;
				SCC_RegSetB = 1;
				SCC_RegNumB = data;
			}
			else
			{
				SCC_RegSetB = 0;
				SCC_RegNumB = 0;
			}
		}
	}
	/* Channel A Control (0xE98005) */
	else if ((adr&7) == 5)
	{
		if (SCC_RegSetA)
		{
			SCC_RegSetA = 0;
			switch (SCC_RegNumA)
			{
			case 2:
				/* WR2: Interrupt vector (shared) */
				SCC_RegsB[2] = data;
				SCC_Vector = data;
				break;
			case 9:
				/* WR9: Master interrupt control */
				SCC_RegsB[9] = data;
				SCC_RegsA[9] = data;
				/* Handle reset commands only when serial is connected */
				if (Serial_IsOpen()) {
					if ((data & 0xC0) == 0x80) {
						/* Reset Channel A */
						SCC_RxFifoClear();
						SCC_TxEmptyA = 1;
						SCC_IntPendingA = 0;
					}
				}
				break;
			default:
				/* Store other registers for serial port use */
				SCC_RegsA[SCC_RegNumA] = data;
				/* Apply serial config only if port is open */
				if (Serial_IsOpen()) {
					if (SCC_RegNumA == 3 || SCC_RegNumA == 4 ||
					    SCC_RegNumA == 5 || SCC_RegNumA == 12 ||
					    SCC_RegNumA == 13 || SCC_RegNumA == 14) {
						SCC_ApplyConfig();
					}
					if (SCC_RegNumA == 5) {
						Serial_SetDTR((data & 0x80) ? 1 : 0);
						Serial_SetRTS((data & 0x02) ? 1 : 0);
						/* Break signal - bit 4 */
						if ((data & 0x10) && !SCC_BreakingA) {
							SCC_BreakingA = 1;
							Serial_SendBreak(250);
						} else if (!(data & 0x10)) {
							SCC_BreakingA = 0;
						}
					}
				}
				break;
			}
			SCC_RegNumA = 0;
		}
		else
		{
			/* Handle WR0 command bits only when serial is connected */
			if (Serial_IsOpen() && (data & 0x38)) {
				SCC_HandleWR0Command(data);
			}
			/* Register selection - match original behavior exactly */
			BYTE regSel = data & 15;
			if (regSel)
			{
				SCC_RegSetA = 1;
				SCC_RegNumA = regSel;
			}
			else
			{
				SCC_RegSetA = 0;
				SCC_RegNumA = 0;
			}
		}
	}
	/* Channel B Data (0xE98003) - Mouse (read-only) */
	else if ((adr&7) == 3)
	{
		/* Mouse data port - write ignored */
	}
	/* Channel A Data (0xE98007) - RS-232C TX */
	else if ((adr&7) == 7)
	{
		/* Transmit data to host serial port */
		if (Serial_IsOpen() && (SCC_RegsA[5] & 0x08)) {  /* Check TX enable (bit 3) */
			Serial_Write(data);
			/* TX buffer becomes empty after write */
			SCC_TxEmptyA = 1;
		}
	}
}


// -----------------------------------------------------------------------
//   I/O Read
// -----------------------------------------------------------------------
BYTE FASTCALL SCC_Read(DWORD adr)
{
	BYTE ret=0;

	if (adr>=0xe98008) return ret;

	/* Channel B Control (0xE98001) */
	if ((adr&7) == 1)
	{
		if (!SCC_RegNumB) {
			/* RR0: Buffer status */
			ret = ((SCC_DatNum)?1:0);  /* Bit 0: RX char available */
		}
		SCC_RegNumB = 0;
		SCC_RegSetB = 0;
	}
	/* Channel B Data (0xE98003) - Mouse RX */
	else if ((adr&7) == 3)
	{
		if (SCC_DatNum)
		{
			SCC_DatNum--;
			ret = SCC_Dat[SCC_DatNum];
		}
	}
	/* Channel A Control (0xE98005) */
	else if ((adr&7) == 5)
	{
		if (Serial_IsOpen()) {
			/* Enhanced mode when serial is connected */
			switch(SCC_RegNumA)
			{
			case 0:
				/* RR0: TX/RX buffer status, modem lines */
				ret = 0x04;  /* TX buffer empty */
				if (SCC_RxFifoCountA > 0) ret |= 0x01;
				if (Serial_GetDCD()) ret |= 0x08;
				if (Serial_GetCTS()) ret |= 0x20;
				if (SCC_TxUnderrunA) ret |= 0x40;
				break;
			case 1:
				/* RR1: Special receive condition */
				ret = 0x01;  /* All sent */
				if (SCC_RxFifoCountA >= SCC_RX_FIFO_SIZE) ret |= 0x20;  /* Overrun */
				break;
			case 2:
				/* RR2: Interrupt vector */
				ret = SCC_Vector;
				break;
			case 3:
				/* RR3: Interrupt pending bits */
				ret = ((SCC_DatNum)?4:0);  /* Channel B RX pending */
				if (SCC_IntPendingA & SCC_INT_RX) ret |= 0x04;
				if (SCC_IntPendingA & SCC_INT_TX) ret |= 0x02;
				if (SCC_IntPendingA & SCC_INT_EXT) ret |= 0x01;
				break;
			default:
				ret = SCC_RegsA[SCC_RegNumA];
				break;
			}
		} else {
			/* Original behavior when serial is not connected */
			switch(SCC_RegNumA)
			{
			case 0:
				ret = 4;  /* TX buffer empty only */
				break;
			case 3:
				ret = ((SCC_DatNum)?4:0);  /* Channel B RX pending */
				break;
			default:
				ret = SCC_RegsA[SCC_RegNumA];
				break;
			}
		}
		SCC_RegNumA = 0;
		SCC_RegSetA = 0;
	}
	/* Channel A Data (0xE98007) - RS-232C RX */
	else if ((adr&7) == 7)
	{
		/* Only return data if serial is open and data available */
		if (Serial_IsOpen() && SCC_RxFifoCountA > 0) {
			ret = SCC_RxFifoPop();
		}
	}

	return ret;
}
