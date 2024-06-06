/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2009 Sergey Kubushyn <ksi@koi8.net>
 * Copyright (C) 2009 - 2013 Heiko Schocher <hs@denx.de>
 * Changes for multibus/multiadapter I2C support.
 *
 * (C) Copyright 2001
 * Gerald Van Baren, Custom IDEAS, vanbaren@cideas.com.
 *
 * The original I2C interface was
 *   (C) 2000 by Paolo Scaffardi (arsenio@tin.it)
 *   AIRVENT SAM s.p.a - RIMINI(ITALY)
 * but has been changed substantially.
 * 
 * Copyright 2024 Varisite Ltd.
 */

#ifndef _I2C_H_
#define _I2C_H_

#include "lpi2c.h"

/*
 * For now there are essentially two parts to this file - driver model
 * here at the top, and the older code below (with CONFIG_SYS_I2C_LEGACY being
 * most recent). The plan is to migrate everything to driver model.
 * The driver model structures and API are separate as they are different
 * enough as to be incompatible for compilation purposes.
 */

enum i2c_chip_flags {
	I2C_CHIP_10BIT	= 1 << 0, /* Use 10-bit addressing */
	I2C_CHIP_RD_ADDRESS	= 1 << 1, /* Send address for each read byte */
	I2C_CHIP_WR_ADDRESS	= 1 << 2, /* Send address for each write byte */
};

/** enum i2c_speed_mode - standard I2C speed modes */
enum i2c_speed_mode {
	I2C_SPEED_MODE_STANDARD,
	I2C_SPEED_MODE_FAST,
	I2C_SPEED_MODE_FAST_PLUS,
	I2C_SPEED_MODE_HIGH,
	I2C_SPEED_MODE_FAST_ULTRA,

	I2C_SPEED_MODE_COUNT,
};

/** enum i2c_speed_rate - standard I2C speeds in Hz */
enum i2c_speed_rate {
	I2C_SPEED_STANDARD_RATE		= 100000,
	I2C_SPEED_FAST_RATE		= 400000,
	I2C_SPEED_FAST_PLUS_RATE	= 1000000,
	I2C_SPEED_HIGH_RATE		= 3400000,
	I2C_SPEED_FAST_ULTRA_RATE	= 5000000,
};

/** enum i2c_address_mode - available address modes */
enum i2c_address_mode {
	I2C_MODE_7_BIT,
	I2C_MODE_10_BIT
};

/** declaration of lpi2c_bus handle */
struct lpi2c_bus;

/**
 * struct i2c_chip - information about an i2c chip
 *
 * An I2C chip is a device on the I2C bus. It sits at a particular address
 * and normally supports 7-bit or 10-bit addressing.
 *
 * To obtain this structure, use dev_get_parent_plat(dev) where dev is
 * the chip to examine.
 *
 * @chip_addr:	Chip address on bus
 * @offset_len: Length of offset in bytes. A single byte offset can
 *		represent up to 256 bytes. A value larger than 1 may be
 *		needed for larger devices.
 * @flags:	Flags for this chip (i2c_chip_flags)
 * @chip_addr_offset_mask: Mask of offset bits within chip_addr. Used for
 *			   devices which steal addresses as part of offset.
 *			   If offset_len is zero, then the offset is encoded
 *			   completely within the chip address itself.
 *			   e.g. a devce with chip address of 0x2c with 512
 *			   registers might use the bottom bit of the address
 *			   to indicate which half of the address space is being
 *			   accessed while still only using 1 byte offset.
 *			   This means it will respond to  chip address 0x2c and
 *			   0x2d.
 *			   A real world example is the Atmel AT24C04. It's
 *			   datasheet explains it's usage of this addressing
 *			   mode.
 */
struct i2c_chip {
	u8 chip_addr;
	u8 offset_len;
	u16 flags;
	u32 chip_addr_offset_mask;
        struct lpi2c_bus *i2c_bus;
};

/*
 * Not all of these flags are implemented in the U-Boot API
 */
enum i2c_msg_flags {
	I2C_M_TEN		= 0x0010, /* ten-bit chip address */
	I2C_M_RD		= 0x0001, /* read data, from slave to master */
	I2C_M_STOP		= 0x8000, /* send stop after this message */
	I2C_M_NOSTART		= 0x4000, /* no start before this message */
	I2C_M_REV_DIR_ADDR	= 0x2000, /* invert polarity of R/W bit */
	I2C_M_IGNORE_NAK	= 0x1000, /* continue after NAK */
	I2C_M_NO_RD_ACK		= 0x0800, /* skip the Ack bit on reads */
	I2C_M_RECV_LEN		= 0x0400, /* length is first received byte */
};

/**
 * struct i2c_msg - an I2C message
 *
 * @addr:	Slave address
 * @flags:	Flags (see enum i2c_msg_flags)
 * @len:	Length of buffer in bytes, may be 0 for a probe
 * @buf:	Buffer to send/receive, or NULL if no data
 */
struct i2c_msg {
	u16 addr;
	u16 flags;
	u32 len;
	u8 *buf;
};

/**
 * i2c_init() - initialize an I2C bus
 * 
 * @i2c_bus:	Bus to initialize
 * @speed:	Speed in Hz
 * Return: 0 if OK, -ve on error
*/
int i2c_init(struct lpi2c_bus *i2c_bus, unsigned int speed);

/**
 * i2c_probe_chip() - probe for a chip on a bus
 *
 * @bus:	Bus to probe
 * @chip_addr:	Chip address to probe
 * @flags:	Flags for the chip
 * Return: 0 if found, -1 if the driver is invalid
 * does not respond to probe
 */
int i2c_probe_chip(struct lpi2c_bus *i2c_bus, uint32_t chip_addr,
			  enum i2c_chip_flags chip_flags);

/**
 * i2c_set_bus_speed() - set the speed of a bus
 *
 * @bus:	Bus to adjust
 * @speed:	Requested speed in Hz
 * Return: 0 if OK, -EINVAL for invalid values
 */
int i2c_set_bus_speed(struct lpi2c_bus *i2c_bus, unsigned int speed);

/**
 * i2c_read() - read bytes from an I2C chip
 *
 * To obtain an I2C device (called a 'chip') given the I2C bus address you
 * can use i2c_get_chip(). To obtain a bus by bus number use
 * uclass_get_device_by_seq(UCLASS_I2C, <bus number>).
 *
 * To set the address length of a devce use i2c_set_addr_len(). It
 * defaults to 1.
 *
 * @dev:	Chip to read from
 * @offset:	Offset within chip to start reading
 * @buffer:	Place to put data
 * @len:	Number of bytes to read
 *
 * Return: 0 on success, -ve on failure
 */
int i2c_read(struct i2c_chip *chip, uint32_t offset, uint8_t *buffer, uint32_t len);

/** i2c_write() - write bytes to an I2C chip
 * 
 * @chip:	Chip to write to
 * @offset:	Offset within chip to start writing
 * @buffer:	Data to write
 * @len:	Number of bytes to write
 * 
 * Return: 0 on success, -ve on failure
*/
int i2c_write(struct i2c_chip *chip, uint32_t offset, const uint8_t *buffer, uint32_t len);

/** i2c_xfer() - Transfer a list of I2C messages
 * 
 * @chip:	Chip to read from
 * @msg:	Message list to send
 * @nmsgs:	Number of messages
 * 
 * Return: 0 on success, -ve on failure
*/
int i2c_xfer(struct i2c_chip *chip, struct i2c_msg *msg, int nmsgs);

/**
 * i2c_dump_msgs() - Dump a list of I2C messages
 *
 * This may be useful for debugging.
 *
 * @msg:	Message list to dump
 * @nmsgs:	Number of messages
 */
void i2c_dump_msgs(struct i2c_msg *msg, int nmsgs);

#endif // _I2C_H_
