/*
 * Copyright (C) 2023 Variscite Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0+
 */
#include <asm/arch/soc_memory_map.h>
#include <asm/io.h>
#include <asm/types.h>
#include "debug.h"
#include "ddr.h"
#include "crc.h"
#include "i2c.h"
#include "eeprom.h"

#ifdef DEBUG
#define debug(fmt, args...) do { printf(fmt, ##args); } while (0)
#else
#define debug(fmt, args...)
#endif

static struct lpi2c_bus __attribute__((section(".ramdata"))) lpi2c =
{
	VAR_DART_EEPROM_I2C_BUS
};

static struct i2c_chip __attribute__((section(".ramdata"))) i2c_dev =
{
	.chip_addr = VAR_DART_EEPROM_I2C_ADDR,
	.offset_len = 1,
	.flags = 0,
	.chip_addr_offset_mask = 1,
	.i2c_bus = &lpi2c,
};

struct mx95_ddr_adjust {
	const char * name;
	union 
	{
		struct ddrc_cfg_param * cfg_param;
		struct ddrphy_cfg_param * cfg_phy_param;
	};
	unsigned int cfg_num;
};

static int var_eeprom_get_dev(struct i2c_chip **devp)
{
	int ret;
	struct lpi2c_bus *bus = i2c_dev.i2c_bus;

	ret = i2c_init(bus, I2C_SPEED_STANDARD_RATE);
	if (ret) {
		debug("%s: I2C bus init failed\n", __func__);
		return ret;
	}

	ret = i2c_probe_chip(bus, i2c_dev.chip_addr, 0);
	if (ret) {
		debug("%s: I2C EEPROM probe failed\n", __func__);
		return ret;
	}

	*devp = &i2c_dev;

	return 0;
}

int var_eeprom_read_header(struct var_eeprom *e)
{
	int ret;
	struct i2c_chip *dev;

	ret = var_eeprom_get_dev(&dev);
	if (ret) {
		debug("%s: Failed to detect I2C EEPROM\n", __func__);
		return ret;
	}

	/* Read EEPROM header to memory */
	ret = i2c_read(dev, 0, (void *)e, sizeof(*e));
	if (ret) {
		debug("%s: EEPROM read failed, ret=%d\n", __func__, ret);
		return ret;
	}

	return 0;
}

void var_eeprom_print_prod_info(struct var_eeprom *ep)
{
	if (!var_eeprom_is_valid(ep))
		return;

	printf("\nPart number: VSM-MX95-%.*s\n", (int)sizeof(ep->partnum), ep->partnum);

	printf("Assembly: AS%.*s\n", (int)sizeof(ep->assembly), (char *)ep->assembly);

	printf("Production date: %.*s %.*s %.*s\n",
			4, /* YYYY */
			(char *)ep->date,
			3, /* MMM */
			((char *)ep->date) + 4,
			2, /* DD */
			((char *)ep->date) + 4 + 3);

	printf("Serial Number: %02x:%02x:%02x:%02x:%02x:%02x\n",
		ep->mac[0], ep->mac[1], ep->mac[2], ep->mac[3], ep->mac[4], ep->mac[5]);

	debug("EEPROM version: 0x%x\n", ep->version);
	debug("SOM features: 0x%x\n", ep->features);
	printf("SOM revision: %ld.%ld\n", SOMREV_MAJOR(ep->somrev), SOMREV_MINOR(ep->somrev));
	printf("DRAM PN: VIC-%04d\n", ep->ddr_vic);
	debug("DRAM size: %d GiB\n\n", (ep->dramsize * 128) / 1024);
}

#ifdef CONFIG_EEPROM_CRC32
static int var_eeprom_crc32(struct var_eeprom *ep, const uint32_t offset,
				const uint32_t len, uint32_t * crc32_val) {
	uint32_t i;
	struct i2c_chip *dev;
	int ret;

	/* No data in EEPROM - return -1 */
	if (!var_eeprom_is_valid(ep)) {
		return -1;
	}

	ret = var_eeprom_get_dev(&dev);
	if (ret) {
		debug("%s: Failed 2 to detect I2C EEPROM\n", __func__);
		return ret;
	}

	*crc32_val = crc32(0, NULL, 0);
	for (i = 0; i < len; i++) {
		uint8_t data;
		i2c_read(dev, offset + i, &data, 1);
		*crc32_val = crc32(*crc32_val, &data, 1);
	}

	debug("%s: crc32=0x%08x (offset=%d len=%d)\n", __func__, *crc32_val, offset, len);

	return 0;
}
#else
static int var_eeprom_crc32(struct var_eeprom *ep, const uint32_t offset,
				const uint32_t len, uint32_t * crc32_val) {
	debug("%s: CRC32 not enabled\n", __func__);
	*crc32_val = 0;
	return 0;
}
#endif

/*
 * Modify DRAM table based on adjustment table in EEPROM
 *
 * Assumption: register addresses in the adjustment table
 * follow the order of register addresses in the original table
 *
 * @adj_table_offset - offset of adjustment table from start of EEPROM
 * @adj_table_size   - number of rows in adjustment table
 * @table            - pointer to DDR table
 * @table_size       - number of rows in DDR table
 */
static void adjust_dram_table(u16 adj_table_offset, u16 adj_table_size,
				struct ddrc_cfg_param *table, u16 table_size)
{
	int i, j = 0;
	u16 off = adj_table_offset;
	struct ddrc_cfg_param adj_table_row;
	int ret;
	struct i2c_chip *dev;

	/* Get EEPROM device */
	ret = var_eeprom_get_dev(&dev);
	if (ret) {
		debug("%s: Failed to detect I2C EEPROM\n", __func__);
		return;
	}

	/* Iterate over adjustment table */
	for (i = 0; i < adj_table_size; i++) {
		/* Read next entry from adjustment table */
		i2c_read(dev, off,
			(uint8_t *)&adj_table_row, sizeof(adj_table_row));

		/* Iterate over DDR table and adjust it */
		for (; j < table_size; j++) {
			if (table[j].reg == adj_table_row.reg) {
				debug("Adjusting reg=0x%x val=0x%x\n",
					adj_table_row.reg, adj_table_row.val);
				table[j].val = adj_table_row.val;
				break;
			}
		}

		off += sizeof(adj_table_row);
	}
}

/*
 * Modify DRAM tables based on adjustment tables in EEPROM
 *
 * @e - pointer to EEPROM header structure
 * @d - pointer to DRAM configuration structure
  */
void var_eeprom_adjust_dram(struct var_eeprom *ep, struct dram_timing_info *d)
{
	int i;
	u16 adj_table_size[DRAM_TABLE_NUM];
	u32 ddr_crc32, ddr_adjust_bytes = 0;

	/* Aligned with Variscite SoM EEPROM DDR Adjust Tables */
	struct mx95_ddr_adjust mx9_adjust_table[] = {
		{ .name = "DDRC",	.cfg_param = d->ddrc_cfg,		.cfg_num = d->ddrc_cfg_num },
		{ .name = "DDR PHY",	.cfg_phy_param = d->ddrphy_cfg,		.cfg_num = d->ddrphy_cfg_num },
		{ .name = "PIE",	.cfg_phy_param = d->ddrphy_pie,		.cfg_num = d->ddrphy_pie_num },
		{ .name = "FSP_CFG[0].ddrc_cfg",
			.cfg_param = d->fsp_cfg[0].ddrc_cfg,	.cfg_num =  d->fsp_cfg[0].ddrc_cfg_num},
		{ .name = "FSP_CFG[0].mr_cfg",
			.cfg_param = d->fsp_cfg[0].mr_cfg,	.cfg_num =  d->fsp_cfg[0].mr_cfg_num},
		{ .name = "FSP0",	.cfg_phy_param = d->fsp_msg[0].fsp_phy_cfg, 	.cfg_num = d->fsp_msg[0].fsp_phy_cfg_num },
		{ .name = NULL },
	};

	if (!var_eeprom_is_valid(ep))
		return;

	/* Check EEPROM version - only version 2+ has DDR adjustment tables */
	if (ep->version < 2) {
		debug("EEPROM version is %d\n", ep->version);
		return;
	}

	debug("EEPROM offset table\n");
	for (i = 0; i < DRAM_TABLE_NUM + 1; i++)
		debug("off[%d]=%d\n", i, ep->off[i]);

	/* Calculate DRAM adjustment table sizes */
	for (i = 0; i < DRAM_TABLE_NUM && ep->off[i + 1] != 0; i++) {
		adj_table_size[i] = (ep->off[i + 1] - ep->off[i]) /
				(sizeof(struct ddrc_cfg_param));

		/* Calculate the total size of the ddr adjust tables */
		ddr_adjust_bytes += ep->off[i + 1] - ep->off[i];
	}

	debug("\nSizes table\n");
	for (i = 0; i < DRAM_TABLE_NUM; i++)
		debug("sizes[%d]=%d\n", i, adj_table_size[i]);

	/* Calculate DDR Adjust table CRC32 */
	if (var_eeprom_crc32(ep, ep->off[0], ddr_adjust_bytes, &ddr_crc32)) {
		printf("%s: Error: DDR adjust table crc calculation failed\n", __func__);
		return;
	}

	/* Verify DDR Adjust table CRC32 */
	if (ddr_crc32 != ep->ddr_crc32) {
		printf("%s: Error: DDR adjust table invalid CRC "
			"eeprom=0x%08x, calculated=0x%08x, len=%d\n",
			__func__, ep->ddr_crc32, ddr_crc32, ddr_adjust_bytes);
		return;
	}
	debug("crc32: eeprom=0x%08x, calculated=0x%08x, len=%d\n", ep->ddr_crc32, ddr_crc32, ddr_adjust_bytes);

	/* Adjust all DDR Tables */
	for (i = 0; mx9_adjust_table[i].name != NULL; i++)
	{
		debug("\nAdjusting %s table: offset=%d, count=%d\n",
			mx9_adjust_table[i].name, ep->off[i], adj_table_size[i]);
		adjust_dram_table(ep->off[i], adj_table_size[i],
			mx9_adjust_table[i].cfg_param, mx9_adjust_table[i].cfg_num);
	}

	/* Adjust FSP rates and PLL bypass */
	for (i = 0; i < NUM_FSPS; i++) {
		debug("\nAdjusting fsp_msg[%d].drate to %d\n", i, ep->fsp_drate[i]);
		d->fsp_msg[i].drate = ep->fsp_drate[i];
		d->fsp_table[i] = ep->fsp_drate[i];

		if (i == 0) {
			/* Last FSP is also for the primary frequency (2d version) */
			d->fsp_table[NUM_FSPS] = ep->fsp_drate[0];
		}

		if (ep->fsp_bypass & (1 << i))
			d->fsp_cfg[i].bypass = 1;
		else
			d->fsp_cfg[i].bypass = 0;

		debug("\nAdjusting fsp_cfg[%d].bypass to %d\n", i, d->fsp_cfg[i].bypass);
	}
}
