/*
 * Copyright (C) 2024 Variscite Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#ifndef _EEPROM_H_
#define _EEPROM_H_

#define VAR_DART_EEPROM_MAGIC	0x4454 /* == HEX("DT") */

#define VAR_DART_EEPROM_I2C_BUS \
	.index = 1, \
	.base = LPI2C1_RBASE, \
	.speed = 100000, /* initial speed */

#define VAR_DART_EEPROM_I2C_ADDR	0x52

/* Optional SOM features */
#define VAR_EEPROM_F_WIFI		BIT(0)
#define VAR_EEPROM_F_ETH		BIT(1)
#define VAR_EEPROM_F_AUDIO		BIT(2)
#define VAR_EEPROM_F_WBE		BIT(3)

/* Helpers to extract the major and minor versions from somrev */
#define SOMREV_MINOR(val) ((val) & GENMASK(4, 0))
#define SOMREV_MAJOR(val) (1 + (((val) >> 5) & GENMASK(2, 0)))

/* SOM storage types */
enum som_storage {
	SOM_STORAGE_EMMC,
	SOM_STORAGE_NAND,
	SOM_STORAGE_UNDEFINED,
};

/* Number of DRAM adjustment tables */
#define DRAM_TABLE_NUM 13
#define NUM_FSPS 1

struct __attribute__((packed)) var_eeprom
{
	u16 magic;			/* 00-0x00 - magic number       */
	u8 partnum[8];			/* 02-0x02 - part number        */
	u8 assembly[10];		/* 10-0x0a - assembly number    */
	u8 date[9];			/* 20-0x14 - build date         */
	u8 mac[6];			/* 29-0x1d - MAC address        */
	u8 somrev;			/* 35-0x23 - SOM revision       */
	u8 version;			/* 36-0x24 - EEPROM version     */
	u8 features;			/* 37-0x25 - SOM features       */
	u8 dramsize;			/* 38-0x26 - DRAM size          */
	u8 reserved[5];			/* 39 0x27 - reserved           */
	u32 ddr_crc32;			/* 44-0x2c - CRC32 of DDR DATAi */
	u16 ddr_vic;			/* 48-0x30 - DDR VIC PN         */
	u16 off[DRAM_TABLE_NUM+1];	/* 50-0x32 - DRAM table offsets */
	u16 fsp_drate[NUM_FSPS];	/* 78-0x4e - ddr_dram_fsp_msg[i].drate */
	u8 fsp_bypass;			/* 84-0x54 - Bitfield for ddr_dram_fsp_cfg[i].bypass */
};

#define VAR_EEPROM_DATA ((struct var_eeprom *)VAR_EEPROM_DRAM_START)

static inline uint16_t htons(uint16_t hostshort) {
    // Swap the bytes
    return (hostshort >> 8) | (hostshort << 8);
}

static inline int var_eeprom_is_valid(struct var_eeprom *ep)
{
	if (htons(ep->magic) != VAR_DART_EEPROM_MAGIC) {
		printf("Invalid EEPROM magic 0x%04x\n", htons(ep->magic));
		return 0;
	}
	return 1;
}

int var_eeprom_read_header(struct var_eeprom *e);
void var_eeprom_print_prod_info(struct var_eeprom *e);
void var_eeprom_adjust_dram(struct var_eeprom *e, struct dram_timing_info *d);

#endif /* _EEPROM_H_ */
