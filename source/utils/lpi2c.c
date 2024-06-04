// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2016 Freescale Semiconductors, Inc.
 * Copyright 2019 NXP
 * Copyright 2024 Varisite Ltd.
 */
#include <asm/arch/clock.h>
#include <asm/arch/soc_memory_map.h>
#include <asm/io.h>
#include <asm/types.h>
#include "debug.h"
#include "lpi2c.h"
#include "iopoll.h"

#ifdef DEBUG
#define debug(fmt, args...) do { printf(fmt, ##args); } while (0)
#else
#define debug(fmt, args...) do {} while (0)
#endif

#define LPI2C_FIFO_SIZE 4
#define LPI2C_NACK_TOUT_MS 1*1000
#define LPI2C_TIMEOUT_MS 100*1000

#ifndef LPI2C_STANDARD_RATE
	#define LPI2C_STANDARD_RATE 100000
#endif

static int bus_i2c_init(struct lpi2c_bus *bus, int speed);

static int lpci2c_check_busy_bus(struct lpi2c_reg *regs)
{
	lpi2c_status_t result = LPI2C_SUCESS;
	u32 status;

	status = readl(&regs->msr);

	if ((status & LPI2C_MSR_BBF_MASK) && !(status & LPI2C_MSR_MBF_MASK))
		result = LPI2C_BUSY;

	return result;
}

static int lpci2c_check_clear_error(struct lpi2c_reg *regs)
{
	lpi2c_status_t result = LPI2C_SUCESS;
	u32 val, status;

	status = readl(&regs->msr);
	/* errors to check for */
	status &= LPI2C_MSR_NDF_MASK | LPI2C_MSR_ALF_MASK |
		LPI2C_MSR_FEF_MASK | LPI2C_MSR_PLTF_MASK;

	if (status) {
		if (status & LPI2C_MSR_PLTF_MASK)
			result = LPI2C_PIN_LOW_TIMEOUT_ERR;
		else if (status & LPI2C_MSR_ALF_MASK)
			result = LPI2C_ARB_LOST_ERR;
		else if (status & LPI2C_MSR_NDF_MASK)
			result = LPI2C_NAK_ERR;
		else if (status & LPI2C_MSR_FEF_MASK)
			result = LPI2C_FIFO_ERR;

		/* clear status flags */
		writel(0x7f00, &regs->msr);
		/* reset fifos */
		val = readl(&regs->mcr);
		val |= LPI2C_MCR_RRF_MASK | LPI2C_MCR_RTF_MASK;
		writel(val, &regs->mcr);
	}

	return result;
}

static int bus_i2c_wait_for_tx_ready(struct lpi2c_reg *regs)
{
	lpi2c_status_t result = LPI2C_SUCESS;
	u32 txcount = 0;
	u32 ts, te;
	ts = timer_get_us();

	do {
		txcount = LPI2C_MFSR_TXCOUNT(readl(&regs->mfsr));
		txcount = LPI2C_FIFO_SIZE - txcount;
		result = lpci2c_check_clear_error(regs);
		if (result) {
			debug("i2c: wait for tx ready: result 0x%x\n", result);
			return result;
		}

		te = timer_get_us() - ts;
		if (te > LPI2C_TIMEOUT_MS) {
			debug("i2c: wait for tx ready: timeout\n");
			return -1;
		}
	} while (!txcount);

	return result;
}

static int bus_i2c_send(struct lpi2c_bus *i2c_bus, u8 *txbuf, int len)
{
	struct lpi2c_reg *regs = (struct lpi2c_reg *)(i2c_bus->base);
	lpi2c_status_t result = LPI2C_SUCESS;

	/* empty tx */
	if (!len)
		return result;

	while (len--) {
		result = bus_i2c_wait_for_tx_ready(regs);
		if (result) {
			debug("i2c: send wait for tx ready: %d\n", result);
			return result;
		}
		writel(*txbuf++, &regs->mtdr);
	}

	return result;
}

static int bus_i2c_receive(struct lpi2c_bus *i2c_bus, u8 *rxbuf, int len)
{
	struct lpi2c_reg *regs = (struct lpi2c_reg *)(i2c_bus->base);
	lpi2c_status_t result = LPI2C_SUCESS;
	u32 val;
	u32 ts, te;
	ts = timer_get_us();

	/* empty read */
	if (!len)
		return result;

	result = bus_i2c_wait_for_tx_ready(regs);
	if (result) {
		debug("i2c: receive wait fot tx ready: %d\n", result);
		return result;
	}

	/* clear all status flags */
	writel(0x7f00, &regs->msr);
	/* send receive command */
	val = LPI2C_MTDR_CMD(0x1) | LPI2C_MTDR_DATA(len - 1);
	writel(val, &regs->mtdr);

	while (len--) {
		do {
			result = lpci2c_check_clear_error(regs);
			if (result) {
				debug("i2c: receive check clear error: %d\n",
				      result);
				return result;
			}

			te = timer_get_us() - ts;
			if (te > LPI2C_TIMEOUT_MS) {
				debug("i2c: receive mrdr: timeout\n");
				return -1;
			}
			val = readl(&regs->mrdr);
		} while (val & LPI2C_MRDR_RXEMPTY_MASK);
		*rxbuf++ = LPI2C_MRDR_DATA(val);
	}

	return result;
}

static int bus_i2c_start(struct lpi2c_bus *i2c_bus, u8 addr, u8 dir)
{
	lpi2c_status_t result;
	struct lpi2c_reg *regs = (struct lpi2c_reg *)(i2c_bus->base);
	u32 val;

	result = lpci2c_check_busy_bus(regs);
	if (result) {
		debug("i2c: start check busy bus: 0x%x\n", result);

		/* Try to init the lpi2c then check the bus busy again */
		bus_i2c_init(i2c_bus, LPI2C_STANDARD_RATE);
		result = lpci2c_check_busy_bus(regs);
		if (result) {
			printf("i2c: Error check busy bus: 0x%x\n", result);
			return result;
		}
	}
	/* clear all status flags */
	writel(0x7f00, &regs->msr);
	/* turn off auto-stop condition */
	val = readl(&regs->mcfgr1) & ~LPI2C_MCFGR1_AUTOSTOP_MASK;
	writel(val, &regs->mcfgr1);
	/* wait tx fifo ready */
	result = bus_i2c_wait_for_tx_ready(regs);
	if (result) {
		debug("i2c: start wait for tx ready: 0x%x\n", result);
		return result;
	}
	/* issue start command */
	val = LPI2C_MTDR_CMD(0x4) | (addr << 0x1) | dir;
	writel(val, &regs->mtdr);

	return result;
}

static int bus_i2c_stop(struct lpi2c_bus *i2c_bus)
{
	lpi2c_status_t result;
	struct lpi2c_reg *regs = (struct lpi2c_reg *)(i2c_bus->base);
	u32 status;
	u32 ts, te;

	result = bus_i2c_wait_for_tx_ready(regs);
	if (result) {
		debug("i2c: stop wait for tx ready: 0x%x\n", result);
		return result;
	}

	/* send stop command */
	writel(LPI2C_MTDR_CMD(0x2), &regs->mtdr);
	ts = timer_get_us();
	
	while (1) {
		status = readl(&regs->msr);
		result = lpci2c_check_clear_error(regs);
		/* stop detect flag */
		if (status & LPI2C_MSR_SDF_MASK) {
			/* clear stop flag */
			status &= LPI2C_MSR_SDF_MASK;
			writel(status, &regs->msr);
			break;
		}
		te = timer_get_us() - ts;
		if (te > LPI2C_NACK_TOUT_MS) {
			debug("stop timeout\n");
			return -ETIMEDOUT;
		}
	}

	return result;
}

static int bus_i2c_read(struct lpi2c_bus *i2c_bus, u32 chip, u8 *buf, int len)
{
	lpi2c_status_t result;

	result = bus_i2c_start(i2c_bus, chip, 1);
	if (result)
		return result;
	result = bus_i2c_receive(i2c_bus, buf, len);
	if (result)
		return result;

	return result;
}

static int bus_i2c_write(struct lpi2c_bus *i2c_bus, u32 chip, u8 *buf, int len)
{
	lpi2c_status_t result;

	result = bus_i2c_start(i2c_bus, chip, 0);
	if (result)
		return result;
	result = bus_i2c_send(i2c_bus, buf, len);
	if (result)
		return result;

	return result;
}

static int bus_i2c_set_bus_speed(struct lpi2c_bus *i2c_bus, int speed)
{
	struct lpi2c_reg *regs = (struct lpi2c_reg *)(i2c_bus->base);
	u32 val;
	u32 preescale = 0, best_pre = 0, clkhi = 0;
	u32 best_clkhi = 0, abs_error = 0, rate;
	u32 error = 0xffffffff;
	u32 clock_rate;
	bool mode;
	u32 i;

	clock_rate = MHZ(24);

	mode = (readl(&regs->mcr) & LPI2C_MCR_MEN_MASK) >> LPI2C_MCR_MEN_SHIFT;
	/* disable master mode */
	val = readl(&regs->mcr) & ~LPI2C_MCR_MEN_MASK;
	writel(val | LPI2C_MCR_MEN(0), &regs->mcr);

	for (preescale = 1; (preescale <= 128) &&
		(error != 0); preescale = 2 * preescale) {
		for (clkhi = 1; clkhi < 32; clkhi++) {
			if (clkhi == 1)
				rate = (clock_rate / preescale) / (1 + 3 + 2 + 2 / preescale);
			else
				rate = (clock_rate / preescale / (3 * clkhi + 2 + 2 / preescale));

			abs_error = (u32)speed > rate ? (u32)speed - rate : rate - (u32)speed;

			if (abs_error < error) {
				best_pre = preescale;
				best_clkhi = clkhi;
				error = abs_error;
				if (abs_error == 0)
					break;
			}
		}
	}

	/* Standard, fast, fast mode plus and ultra-fast transfers. */
	val = LPI2C_MCCR0_CLKHI(best_clkhi);
	if (best_clkhi < 2)
		val |= LPI2C_MCCR0_CLKLO(3) | LPI2C_MCCR0_SETHOLD(2) | LPI2C_MCCR0_DATAVD(1);
	else
		val |= LPI2C_MCCR0_CLKLO(2 * best_clkhi) | LPI2C_MCCR0_SETHOLD(best_clkhi) |
			LPI2C_MCCR0_DATAVD(best_clkhi / 2);
	writel(val, &regs->mccr0);

	for (i = 0; i < 8; i++) {
		if (best_pre == (1UL << i)) {
			best_pre = i;
			break;
		}
	}

	val = readl(&regs->mcfgr1) & ~LPI2C_MCFGR1_PRESCALE_MASK;
	writel(val | LPI2C_MCFGR1_PRESCALE(best_pre), &regs->mcfgr1);

	if (mode) {
		val = readl(&regs->mcr) & ~LPI2C_MCR_MEN_MASK;
		writel(val | LPI2C_MCR_MEN(1), &regs->mcr);
	}

	return 0;
}

static int bus_i2c_init(struct lpi2c_bus *i2c_bus, int speed)
{
	u32 val;
	int ret;

	struct lpi2c_reg *regs = (struct lpi2c_reg *)(i2c_bus->base);
	/* reset peripheral */
	writel(LPI2C_MCR_RST_MASK, &regs->mcr);
	writel(0x0, &regs->mcr);
	/* Disable Dozen mode */
	writel(LPI2C_MCR_DBGEN(0) | LPI2C_MCR_DOZEN(1), &regs->mcr);
	/* host request disable, active high, external pin */
	val = readl(&regs->mcfgr0);
	val &= (~(LPI2C_MCFGR0_HREN_MASK | LPI2C_MCFGR0_HRPOL_MASK |
				LPI2C_MCFGR0_HRSEL_MASK));
	val |= LPI2C_MCFGR0_HRPOL(0x1);
	writel(val, &regs->mcfgr0);
	/* pincfg and ignore ack */
	val = readl(&regs->mcfgr1);
	val &= ~(LPI2C_MCFGR1_PINCFG_MASK | LPI2C_MCFGR1_IGNACK_MASK);
	val |= LPI2C_MCFGR1_PINCFG(0x0); /* 2 pin open drain */
	val |= LPI2C_MCFGR1_IGNACK(0x0); /* ignore nack */
	writel(val, &regs->mcfgr1);

	ret = bus_i2c_set_bus_speed(i2c_bus, speed);

	/* enable lpi2c in master mode */
	val = readl(&regs->mcr) & ~LPI2C_MCR_MEN_MASK;
	writel(val | LPI2C_MCR_MEN(1), &regs->mcr);

	debug("i2c : controller bus %d, speed %d:\n", i2c_bus->index, speed);

	return ret;
}

int lpi2c_probe_chip(struct lpi2c_bus *i2c_bus, u32 chip, u32 chip_flags)
{
	lpi2c_status_t result;

	result = bus_i2c_start(i2c_bus, chip, 0);
	if (result) {
		bus_i2c_stop(i2c_bus);
		bus_i2c_init(i2c_bus, LPI2C_STANDARD_RATE);
		return result;
	}

	result = bus_i2c_stop(i2c_bus);
	if (result)
		bus_i2c_init(i2c_bus, LPI2C_STANDARD_RATE);

	return result;
}

int lpi2c_xfer(struct lpi2c_bus *i2c_bus, struct i2c_msg *msg, int nmsgs)
{
	int ret = 0, ret_stop;

	for (; nmsgs > 0; nmsgs--, msg++) {
		debug("i2c_xfer: chip=0x%x, len=0x%x\n", msg->addr, msg->len);
		if (msg->flags & I2C_M_RD)
			ret = bus_i2c_read(i2c_bus, msg->addr, msg->buf, msg->len);
		else {
			ret = bus_i2c_write(i2c_bus, msg->addr, msg->buf,
					    msg->len);
			if (ret)
				break;
		}
	}

	if (ret)
		debug("i2c_write: error sending\n");

	ret_stop = bus_i2c_stop(i2c_bus);
	if (ret_stop)
		debug("i2c_xfer: stop bus error\n");

	ret |= ret_stop;

	return ret;
}
