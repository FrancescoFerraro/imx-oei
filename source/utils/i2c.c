// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2014 Google, Inc
 * Copyright 2024 Varisite Ltd.
 */

#include <asm/arch/clock.h>
#include <asm/arch/soc_memory_map.h>
#include <asm/io.h>
#include <asm/types.h>
#include "debug.h"
#include "i2c.h"

#define I2C_MAX_OFFSET_LEN	4

/* Useful debugging function */
void i2c_dump_msgs(struct i2c_msg *msg, int nmsgs)
{
	int i;

	for (i = 0; i < nmsgs; i++) {
		struct i2c_msg *m = &msg[i];
		printf("   %s %x len=%x", m->flags & I2C_M_RD ? "R" : "W",
		       m->addr, m->len);
		if (!(m->flags & I2C_M_RD))
			printf(": %x", m->buf[0]);
		printf("\n");
	}
}

/**
 * i2c_setup_offset() - Set up a new message with a chip offset
 *
 * @chip:	Chip to use
 * @offset:	Byte offset within chip
 * @offset_buf:	Place to put byte offset
 * @msg:	Message buffer
 * Return: 0 if OK, -1 if the offset length is 0. In that case the
 * message is still set up but will not contain an offset.
 */
static int i2c_setup_offset(struct i2c_chip *chip, uint32_t offset,
			    uint8_t offset_buf[], struct i2c_msg *msg)
{
	int offset_len = chip->offset_len;

	msg->addr = chip->chip_addr;
	if (chip->chip_addr_offset_mask)
		msg->addr |= (offset >> (8 * offset_len)) &
			chip->chip_addr_offset_mask;
	msg->flags = chip->flags & I2C_CHIP_10BIT ? I2C_M_TEN : 0;
	msg->len = chip->offset_len;
	msg->buf = offset_buf;
	if (!offset_len)
		return -1;

	while (offset_len--)
		*offset_buf++ = offset >> (8 * offset_len);

	return 0;
}

static int i2c_read_bytewise(struct i2c_chip *chip, uint32_t offset,
			     uint8_t *buffer, int len)
{
	struct i2c_msg msg[2], *ptr;
	uint8_t offset_buf[I2C_MAX_OFFSET_LEN];
	int ret;
	int i;

	for (i = 0; i < len; i++) {
		if (i2c_setup_offset(chip, offset + i, offset_buf, msg))
			return -1;
		ptr = msg + 1;
		ptr->addr = msg->addr;
		ptr->flags = msg->flags | I2C_M_RD;
		ptr->len = 1;
		ptr->buf = &buffer[i];
		ptr++;

		ret = lpi2c_xfer(chip->i2c_bus, msg, ptr - msg);
		if (ret)
			return ret;
	}

	return 0;
}

static int i2c_write_bytewise(struct i2c_chip *chip, uint32_t offset,
			     const uint8_t *buffer, int len)
{
	struct i2c_msg msg[1];
	uint8_t buf[I2C_MAX_OFFSET_LEN + 1];
	int ret;
	int i;

	for (i = 0; i < len; i++) {
		if (i2c_setup_offset(chip, offset + i, buf, msg))
			return -1;
		buf[msg->len++] = buffer[i];

		ret = lpi2c_xfer(chip->i2c_bus, msg, 1);
		if (ret)
			return ret;
	}

	return 0;
}

int i2c_init(struct lpi2c_bus *i2c_bus, unsigned int speed)
{
	int ret;
	i2c_bus->speed = speed;
	ret = lpi2c_init(i2c_bus);
	return ret;
}

int i2c_read(struct i2c_chip *chip, uint32_t offset, uint8_t *buffer, uint32_t len)
{
	struct i2c_msg msg[2], *ptr;
	uint8_t offset_buf[I2C_MAX_OFFSET_LEN];
	int msg_count;

	if (chip->flags & I2C_CHIP_RD_ADDRESS)
		return i2c_read_bytewise(chip, offset, buffer, len);
	ptr = msg;
	if (!i2c_setup_offset(chip, offset, offset_buf, ptr))
		ptr++;

	if (len) {
		ptr->addr = msg->addr;
		ptr->flags = chip->flags & I2C_CHIP_10BIT ? I2C_M_TEN : 0;
		ptr->flags |= I2C_M_RD;
		ptr->len = len;
		ptr->buf = buffer;
		ptr++;
	}
	msg_count = ptr - msg;

	return i2c_xfer(chip, msg, msg_count);
}

int i2c_write(struct i2c_chip *chip, uint32_t offset, const uint8_t *buffer,
		uint32_t len)
{
	struct i2c_msg msg[1];
	uint8_t _buf[I2C_MAX_OFFSET_LEN + 64];
	uint8_t *buf = _buf;
	int ret;

	if (chip->flags & I2C_CHIP_WR_ADDRESS)
		return i2c_write_bytewise(chip, offset, buffer, len);
	/*
	 * The simple approach would be to send two messages here: one to
	 * set the offset and one to write the bytes. However some drivers
	 * will not be expecting this, and some chips won't like how the
	 * driver presents this on the I2C bus.
	 *
	 * The API does not support separate offset and data. We could extend
	 * it with a flag indicating that there is data in the next message
	 * that needs to be processed in the same transaction. We could
	 * instead add an additional buffer to each message. For now, handle
	 * this in the uclass since it isn't clear what the impact on drivers
	 * would be with this extra complication. Unfortunately this means
	 * copying the message.
	 *
	 * Use the stack for small messages, malloc() for larger ones. We
	 * need to allow space for the offset (up to 4 bytes) and the message
	 * itself.
	 */
	if (len > sizeof(_buf) - I2C_MAX_OFFSET_LEN) {
#ifdef IC2_USE_MALLOC
		buf = malloc(I2C_MAX_OFFSET_LEN + len);
		if (!buf)
			return -1;
#else
		return -1;
#endif
	}

	i2c_setup_offset(chip, offset, buf, msg);
	msg->len += len;
	/* memcpy(buf + chip->offset_len, buffer, len); */
	uint8_t *dest = buf + chip->offset_len;
	const uint8_t *src = buffer;
	for (uint32_t i = 0; i < len; i++) {
		dest[i] = src[i];
	}

	ret = i2c_xfer(chip, msg, 1);
#ifdef IC2_USE_MALLOC
	if (buf != _buf)
		free(buf);
#endif
	return ret;
}

int i2c_xfer(struct i2c_chip *chip, struct i2c_msg *msg, int nmsgs)
{
#ifdef DEBUG
	printf("xfer to chip %x, %d messages:\n", chip->chip_addr, nmsgs);
	i2c_dump_msgs(msg, nmsgs);
#endif
	return lpi2c_xfer(chip->i2c_bus, msg, nmsgs);
}

/**
 * i2c_probe_chip() - probe for a chip on a bus
 *
 * @bus:	Bus to probe
 * @chip_addr:	Chip address to probe
 * @flags:	Flags for the chip
 * Return: 0 if found, -ENOSYS if the driver is invalid, -EREMOTEIO if the chip
 * does not respond to probe
 */
int i2c_probe_chip(struct lpi2c_bus *i2c_bus, uint32_t chip_addr,
			  enum i2c_chip_flags chip_flags)
{
	return lpi2c_probe_chip(i2c_bus, chip_addr, chip_flags);
}

int i2c_set_bus_speed(struct lpi2c_bus *i2c_bus, unsigned int speed)
{
	int ret;

	/*
	 * If we have a method, call it. If not then the driver probably wants
	 * to deal with speed changes on the next transfer. It can easily read
	 * the current speed from this uclass
	 */

	ret = lpi2c_set_bus_speed(i2c_bus, speed);
	if (ret)
		return ret;
	i2c_bus->speed = speed;
	return 0;
}
