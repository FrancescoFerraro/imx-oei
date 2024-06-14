/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2023 NXP
 */

/**
 * M33 Code TCM (TCML) Free Space: 0x1FFC0000 - 0x1FFFFFFF - according to RM
 * M33 Sys  TCM (TCMU) Free Space: 0x30000000 - 0x3002DFFF - according to CM33 ROM
 * System   OCRAM      Free Space: 0x30480000 - 0x304D7FFF - according to RM
 */
MEMORY {
	s_code (rx) : ORIGIN = 0x1FFC0000, LENGTH = 0x00040000 /* 256 KB */
	s_data (rw) : ORIGIN = 0x30000000, LENGTH = 0x0002E000 /* 184 KB */
}

OUTPUT_FORMAT("elf32-littlearm", "elf32-littlearm", "elf32-littlearm")
OUTPUT_ARCH(arm) 

SECTIONS
{
	.text : {
		. = ALIGN(4);
	        KEEP(*(.entry))
		*(.text*)
	} > s_code

	.rodata : {
		. = ALIGN(4);
		*(SORT_BY_ALIGNMENT(SORT_BY_NAME(.rodata*)))
	} > s_code

	.data : {
		. = ALIGN(4);
		*(.data*)
	} > s_code

	__ramdata_rom_start__ = .;
	.ramdata : AT(__ramdata_rom_start__) {
		. = ALIGN(4);
		__ramdata_start__ = .;
		*(.ramdata*)
		. = ALIGN(4);
		__ramdata_end__ = .;
	} > s_data
	__ramdata_rom_end__ =  __ramdata_rom_start__ + (__ramdata_end__ - __ramdata_start__);

	/* . = __ramdata_rom_start__ + SIZEOF(.ramdata); */
	.dummy : {
		. = ALIGN(4);
		. += SIZEOF(.ramdata);
	} > s_code

	.end : {
		. = ALIGN(4);
		*(.__end)
	} > s_code
}
