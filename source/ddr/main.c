/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2023 NXP
 */
#include "ddr.h"
#include <asm/arch/clock.h>
#include "asm/arch/soc_memory_map.h"
#include <time.h>
#include "oei.h"
#include "debug.h"
#include "i2c.h"
#include "lpuart.h"
#include "pinmux.h"
#include "build_info.h"

#ifdef  DDR_MEM_TEST
#define DDR_MEM_BASE	0x80000000
#define SIZE_1G		0x40000000
static u32 mem_test(ulong addr, u32 val, u32 index, u32 len)
{
	u32 read_data, fail = 0, i, j;
	ulong dest;

	for (i=index, j=val; i<len; i++, j++) {
		dest = addr + 0x4*i;
		W32(dest, j);
	}

	for (i=index, j=val; i<len; i++, j++) {
		dest = addr + 0x4*i;
		R32(dest, read_data);
		if (read_data != j)
			fail = fail+1;
	}

	return fail;
}
#endif

uint32_t __attribute__((section(".entry"))) oei_entry(void)
{
	int ret;
#ifdef DDR_MEM_TEST
	int fail = 0;
#endif
	u8 eeprom_buf[10];

	struct lpi2c_bus lpi2c1 =
	{
		.index = 1,
		.base = LPI2C1_RBASE,
		.speed = 100000,
	};

	struct i2c_chip eeprom =
	{
		.chip_addr = 0x52,
		.offset_len = 1,
		.flags = 0,
		.chip_addr_offset_mask = 0xff,
		.i2c_bus = &lpi2c1,
	};

	if (!timer_is_enabled())
		timer_enable();

	clock_init();
	pinmux_config();
	lpuart32_serial_init();

	printf("\n\n** DDR OEI: Booting, commit: %08x **\n", OEI_COMMIT);
	/* preset eeprom_buf */
	for (u32 i = 0; i < sizeof(eeprom_buf); i++)
		eeprom_buf[i] = (u8)i;

	/* print eeprom_buf content before */
	printf("DDR OEI: EEPROM buffer content to write: ");
	for (u32 i = 0; i < sizeof(eeprom_buf); i++)
		printf("%02x ", eeprom_buf[i]);
	printf("\n");
	ret = i2c_init(&lpi2c1, I2C_SPEED_STANDARD_RATE);
	if (ret)
		printf("DDR OEI: I2C bus init failed\n");
	else
		printf("DDR OEI: I2C bus init success\n");

	ret = i2c_probe_chip(&lpi2c1, 0x52, 0);
	if (ret)
		printf("DDR OEI: EEPROM chip probe failed\n");
	else
		printf("DDR OEI: EEPROM chip probe success\n");

	ret = i2c_write(&eeprom, 0, (uint8_t *)&eeprom_buf, sizeof(eeprom_buf));
	if (ret)
		printf("DDR OEI: EEPROM write failed\n");
	else
		printf("DDR OEI: EEPROM write success\n");

	ret = i2c_read(&eeprom, 0, (uint8_t *)&eeprom_buf, sizeof(eeprom_buf));
	if (ret)
		printf("DDR OEI: EEPROM read failed\n");
	else
		printf("DDR OEI: EEPROM read success\n");

	printf("DDR OEI: EEPROM readback: ");
	for (u32 i = 0; i < sizeof(eeprom_buf); i++)
		printf("%02x ", eeprom_buf[i]);
	printf("\n");

#ifdef	CONFIG_DDR_QBOOT
	printf("** DDR OEI: QuickBoot **\n");
#else
	printf("** DDR OEI: Training **\n");
#endif

	ret = ddr_init(&dram_timing);

#ifdef  DDR_MEM_TEST
	if (ret == 0) {
		fail = fail + mem_test(DDR_MEM_BASE, 0xfabeface, 0, 10);
		fail = fail + mem_test(DDR_MEM_BASE, 0xdeadbeef, 10, 0x100);

		fail = fail + mem_test(DDR_MEM_BASE + SIZE_1G, 0x98760000, 0, 10);
		fail = fail + mem_test(DDR_MEM_BASE + SIZE_1G, 0xabcd0000, 10, 0x100);
		if (fail)
			printf("** DDR OEI: memtest fails: %u **\n", fail);
		else
			printf("** DDR OEI: memtest pass! **\n");
	}
#endif
	printf("** DDR OEI: done, err=%d **\n", ret);

	return (ret == 0 ? OEI_SUCCESS_FREE_MEM : OEI_FAIL);
}
