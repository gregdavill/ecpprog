/*
 *  ecpprog -- simple programming tool for FTDI-based JTAG programmers
 *  Based on iceprog
 *
 *  Copyright (C) 2015  Clifford Wolf <clifford@clifford.at>
 *  Copyright (C) 2018  Piotr Esden-Tempski <piotr@esden.net>
 *  Copyright (C) 2020  Gregory Davill <greg.davill@gmail.com>
 *
 *  Permission to use, copy, modify, and/or distribute this software for any
 *  purpose with or without fee is hereby granted, provided that the above
 *  copyright notice and this permission notice appear in all copies.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 *  Relevant Documents:
 *  -------------------
 *  http://www.latticesemi.com/~/media/Documents/UserManuals/EI/icestickusermanual.pdf
 *  http://www.micron.com/~/media/documents/products/data-sheet/nor-flash/serial-nor/n25q/n25q_32mb_3v_65nm.pdf
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <io.h> /* _setmode() */
#include <fcntl.h> /* _O_BINARY */
#endif

#include "jtag.h"
#include "lattice_cmds.h"

static bool verbose = false;

enum device_type {
	TYPE_NONE = 0,
	TYPE_ECP5 = 1,
	TYPE_NX = 2,
};

struct device_info {
	const char* 	 name;
	uint32_t    	 id;
	enum device_type type;
};

static struct device_info connected_device = {0};


// ---------------------------------------------------------
// FLASH definitions
// ---------------------------------------------------------

/* Flash command definitions */
/* This command list is based on the Winbond W25Q128JV Datasheet */
enum flash_cmd {
	FC_WE = 0x06, /* Write Enable */
	FC_SRWE = 0x50, /* Volatile SR Write Enable */
	FC_WD = 0x04, /* Write Disable */
	FC_RPD = 0xAB, /* Release Power-Down, returns Device ID */
	FC_MFGID = 0x90, /*  Read Manufacturer/Device ID */
	FC_JEDECID = 0x9F, /* Read JEDEC ID */
	FC_UID = 0x4B, /* Read Unique ID */
	FC_RD = 0x03, /* Read Data */
	FC_FR = 0x0B, /* Fast Read */
	FC_PP = 0x02, /* Page Program */
	FC_SE = 0x20, /* Sector Erase 4kb */
	FC_BE32 = 0x52, /* Block Erase 32kb */
	FC_BE64 = 0xD8, /* Block Erase 64kb */
	FC_CE = 0xC7, /* Chip Erase */
	FC_RSR1 = 0x05, /* Read Status Register 1 */
	FC_WSR1 = 0x01, /* Write Status Register 1 */
	FC_RSR2 = 0x35, /* Read Status Register 2 */
	FC_WSR2 = 0x31, /* Write Status Register 2 */
	FC_RSR3 = 0x15, /* Read Status Register 3 */
	FC_WSR3 = 0x11, /* Write Status Register 3 */
	FC_RSFDP = 0x5A, /* Read SFDP Register */
	FC_ESR = 0x44, /* Erase Security Register */
	FC_PSR = 0x42, /* Program Security Register */
	FC_RSR = 0x48, /* Read Security Register */
	FC_GBL = 0x7E, /* Global Block Lock */
	FC_GBU = 0x98, /* Global Block Unlock */
	FC_RBL = 0x3D, /* Read Block Lock */
	FC_RPR = 0x3C, /* Read Sector Protection Registers (adesto) */
	FC_IBL = 0x36, /* Individual Block Lock */
	FC_IBU = 0x39, /* Individual Block Unlock */
	FC_EPS = 0x75, /* Erase / Program Suspend */
	FC_EPR = 0x7A, /* Erase / Program Resume */
	FC_PD = 0xB9, /* Power-down */
	FC_QPI = 0x38, /* Enter QPI mode */
	FC_ERESET = 0x66, /* Enable Reset */
	FC_RESET = 0x99, /* Reset Device */
};


// ---------------------------------------------------------
// JTAG -> SPI functions
// ---------------------------------------------------------

/* 
 * JTAG performrs all shifts LSB first, our FLSAH is expeting bytes MSB first,
 * There are a few ways to fix this, for now we just bit-reverse all the input data to the JTAG core
 */
uint8_t bit_reverse(uint8_t in){

	uint8_t out =  (in & 0x01) ? 0x80 : 0x00;
	        out |= (in & 0x02) ? 0x40 : 0x00;
	        out |= (in & 0x04) ? 0x20 : 0x00;
	        out |= (in & 0x08) ? 0x10 : 0x00;
	        out |= (in & 0x10) ? 0x08 : 0x00;
	        out |= (in & 0x20) ? 0x04 : 0x00;
	        out |= (in & 0x40) ? 0x02 : 0x00;
	        out |= (in & 0x80) ? 0x01 : 0x00;

	return out;
}

void xfer_spi(uint8_t* data, uint32_t len){
	/* Reverse bit order of all bytes */
	for(int i = 0; i < len; i++){
		data[i] = bit_reverse(data[i]);
	}

	/* Don't switch states if we're already in SHIFT-DR */
	if(jtag_current_state() != STATE_SHIFT_DR)
		jtag_go_to_state(STATE_SHIFT_DR);
	jtag_tap_shift(data, data, len * 8, true);

	/* Reverse bit order of all return bytes */
	for(int i = 0; i < len; i++){
		data[i] = bit_reverse(data[i]);
	}
}

void send_spi(uint8_t* data, uint32_t len){
	
	/* Flip bit order of all bytes */
	for(int i = 0; i < len; i++){
		data[i] = bit_reverse(data[i]);
	}

	jtag_go_to_state(STATE_SHIFT_DR);
	/* Stay in SHIFT-DR state, this keep CS low */
	jtag_tap_shift(data, data, len * 8, false); 

		/* Flip bit order of all bytes */
	for(int i = 0; i < len; i++){
		data[i] = bit_reverse(data[i]);
	}
}


// ---------------------------------------------------------
// FLASH function implementations
// ---------------------------------------------------------

static void flash_read_id()
{
	/* JEDEC ID structure:
	 * Byte No. | Data Type
	 * ---------+----------
	 *        0 | FC_JEDECID Request Command
	 *        1 | MFG ID
	 *        2 | Dev ID 1
	 *        3 | Dev ID 2
	 *        4 | Ext Dev Str Len
	 */

	uint8_t data[260] = { FC_JEDECID };
	int len = 4; // command + 4 response bytes

	if (verbose)
		fprintf(stderr, "read flash ID..\n");

	// Write command and read first 4 bytes
	xfer_spi(data, len);

	fprintf(stderr, "flash ID:");
	for (int i = 1; i < len; i++)
		fprintf(stderr, " 0x%02X", data[i]);
	fprintf(stderr, "\n");
}

static void flash_reset()
{
	uint8_t data[8] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

	// This disables CRM is if it was enabled
	jtag_go_to_state(STATE_SHIFT_DR);
	jtag_tap_shift(data, data, 64, true);

	// This disables QPI if it was enabled
	jtag_go_to_state(STATE_SHIFT_DR);
	jtag_tap_shift(data, data, 2, true);

	// This issues a flash reset command
	jtag_go_to_state(STATE_SHIFT_DR);
	jtag_tap_shift(data, data, 8, true);
}

static uint8_t read_status_1(){
	uint8_t data[2] = { FC_RSR1 };

	xfer_spi(data, 2);

	if (verbose) {
		fprintf(stderr, "SR1: 0x%02X\n", data[1]);
		fprintf(stderr, " - SPRL: %s\n",
			((data[1] & (1 << 7)) == 0) ? 
				"unlocked" : 
				"locked");
		fprintf(stderr, " -  SPM: %s\n",
			((data[1] & (1 << 6)) == 0) ?
				"Byte/Page Prog Mode" :
				"Sequential Prog Mode");
		fprintf(stderr, " -  EPE: %s\n",
			((data[1] & (1 << 5)) == 0) ?
				"Erase/Prog success" :
				"Erase/Prog error");
		fprintf(stderr, "-  SPM: %s\n",
			((data[1] & (1 << 4)) == 0) ?
				"~WP asserted" :
				"~WP deasserted");
		fprintf(stderr, " -  SWP: ");
		switch((data[1] >> 2) & 0x3) {
			case 0:
				fprintf(stderr, "All sectors unprotected\n");
				break;
			case 1:
				fprintf(stderr, "Some sectors protected\n");
				break;
			case 2:
				fprintf(stderr, "Reserved (xxxx 10xx)\n");
				break;
			case 3:
				fprintf(stderr, "All sectors protected\n");
				break;
		}
		fprintf(stderr, " -  WEL: %s\n",
			((data[1] & (1 << 1)) == 0) ?
				"Not write enabled" :
				"Write enabled");
		fprintf(stderr, " - ~RDY: %s\n",
			((data[1] & (1 << 0)) == 0) ?
				"Ready" :
				"Busy");
	}

	return data[1];
}

static uint8_t read_status_2(){
	uint8_t data[2] = { FC_RSR2 };

	xfer_spi(data, 2);

	if (verbose) {
		fprintf(stderr, "SR2: 0x%02X\n", data[1]);
		fprintf(stderr, " - QE: %s\n",
			((data[1] & (1 << 2)) == 0) ? 
				"enabled" : 
				"disabled");

	}

	return data[1];
}

static uint8_t flash_read_status()
{
	uint8_t ret = read_status_1();
	read_status_2();

	return ret;
}


static void flash_write_enable()
{
	if (verbose) {
		fprintf(stderr, "status before enable:\n");
		flash_read_status();
	}

	if (verbose)
		fprintf(stderr, "write enable..\n");

	uint8_t data[1] = { FC_WE };
	xfer_spi(data, 1);

	if (verbose) {
		fprintf(stderr, "status after enable:\n");
		flash_read_status();
	}
}

static void flash_bulk_erase()
{
	fprintf(stderr, "bulk erase..\n");

	uint8_t data[1] = { FC_CE };
	xfer_spi(data, 1);
}

static void flash_4kB_sector_erase(int addr)
{
	fprintf(stderr, "erase 4kB sector at 0x%06X..\n", addr);

	uint8_t command[4] = { FC_SE, (uint8_t)(addr >> 16), (uint8_t)(addr >> 8), (uint8_t)addr };

	xfer_spi(command, 4);
}

static void flash_32kB_sector_erase(int addr)
{
	fprintf(stderr, "erase 64kB sector at 0x%06X..\n", addr);

	uint8_t command[4] = { FC_BE32, (uint8_t)(addr >> 16), (uint8_t)(addr >> 8), (uint8_t)addr };

	xfer_spi(command, 4);
}

static void flash_64kB_sector_erase(int addr)
{
	fprintf(stderr, "erase 64kB sector at 0x%06X..\n", addr);

	uint8_t command[4] = { FC_BE64, (uint8_t)(addr >> 16), (uint8_t)(addr >> 8), (uint8_t)addr };

	xfer_spi(command, 4);
}

static void flash_prog(int addr, uint8_t *data, int n)
{
	if (verbose)
		fprintf(stderr, "prog 0x%06X +0x%03X..\n", addr, n);

	uint8_t command[4] = { FC_PP, (uint8_t)(addr >> 16), (uint8_t)(addr >> 8), (uint8_t)addr };

	send_spi(command, 4);
	xfer_spi(data, n);
	
	if (verbose)
		for (int i = 0; i < n; i++)
			fprintf(stderr, "%02x%c", data[i], i == n - 1 || i % 32 == 31 ? '\n' : ' ');
}


static void flash_start_read(int addr)
{
	if (verbose)
		fprintf(stderr, "Start Read 0x%06X\n", addr);

	uint8_t command[4] = { FC_RD, (uint8_t)(addr >> 16), (uint8_t)(addr >> 8), (uint8_t)addr };

	send_spi(command, 4);
}

static void flash_continue_read(uint8_t *data, int n)
{
	if (verbose)
		fprintf(stderr, "Continue Read +0x%03X..\n", n);

	memset(data, 0, n);
	send_spi(data, n);
	
	if (verbose)
		for (int i = 0; i < n; i++)
			fprintf(stderr, "%02x%c", data[i], i == n - 1 || i % 32 == 31 ? '\n' : ' ');
}

static void flash_wait()
{
	if (verbose)
		fprintf(stderr, "waiting..");

	int count = 0;
	while (1)
	{
		uint8_t data[2] = { FC_RSR1 };

		xfer_spi(data, 2);

		if ((data[1] & 0x01) == 0) {
			if (count < 2) {
				count++;
				if (verbose) {
					fprintf(stderr, "r");
					fflush(stderr);
				}
			} else {
				if (verbose) {
					fprintf(stderr, "R");
					fflush(stderr);
				}
				break;
			}
		} else {
			if (verbose) {
				fprintf(stderr, ".");
				fflush(stderr);
			}
			count = 0;
		}

		usleep(1000);
	}

	if (verbose)
		fprintf(stderr, "\n");

}

static void flash_disable_protection()
{
	fprintf(stderr, "disable flash protection...\n");

	// Write Status Register 1 <- 0x00
	uint8_t data[2] = { FC_WSR1, 0x00 };
	xfer_spi(data, 2);
	
	flash_wait();
	
	// Read Status Register 1
	data[0] = FC_RSR1;

	xfer_spi(data, 2);

	if (data[1] != 0x00)
		fprintf(stderr, "failed to disable protection, SR now equal to 0x%02x (expected 0x00)\n", data[1]);

}

// ---------------------------------------------------------
// ECP5 specific JTAG functions
// ---------------------------------------------------------

static void print_idcode(uint32_t idcode){
	connected_device.id = idcode;
	
	/* ECP5 Parts */
	for(int i = 0; i < sizeof(ecp_devices)/sizeof(struct device_id_pair); i++){
		if(idcode == ecp_devices[i].device_id)
		{
			connected_device.name = ecp_devices[i].device_name;
			connected_device.type = TYPE_ECP5;
			printf("IDCODE: 0x%08x (%s)\n", idcode ,ecp_devices[i].device_name);
			return;
		}
	}

	/* NX Parts */
	for(int i = 0; i < sizeof(nx_devices)/sizeof(struct device_id_pair); i++){
		if(idcode == nx_devices[i].device_id)
		{
			connected_device.name = nx_devices[i].device_name;
			connected_device.type = TYPE_NX;
			printf("IDCODE: 0x%08x (%s)\n", idcode ,nx_devices[i].device_name);
			return;
		}
	}
	printf("IDCODE: 0x%08x does not match :(\n", idcode);
}

static void read_idcode(){

	uint8_t data[4] = {READ_ID};

	jtag_go_to_state(STATE_SHIFT_IR);
	jtag_tap_shift(data, data, 8, true);

	data[0] = 0;
	jtag_go_to_state(STATE_SHIFT_DR);
	jtag_tap_shift(data, data, 32, true);

	uint32_t idcode = 0;
	
	/* Format the IDCODE into a 32bit value */
	for(int i = 0; i< 4; i++)
		idcode = data[i] << 24 | idcode >> 8;

	print_idcode(idcode);
}

void print_ecp5_status_register(uint32_t status){	
	printf("ECP5 Status Register: 0x%08x\n", status);

	if(verbose){
		printf("  Transparent Mode:   %s\n",  status & (1 << 0)  ? "Yes" : "No" );
		printf("  Config Target:      %s\n",  status & (7 << 1)  ? "eFuse" : "SRAM" );
		printf("  JTAG Active:        %s\n",  status & (1 << 4)  ? "Yes" : "No" );
		printf("  PWD Protection:     %s\n",  status & (1 << 5)  ? "Yes" : "No" );
		printf("  Decrypt Enable:     %s\n",  status & (1 << 7)  ? "Yes" : "No" );
		printf("  DONE:               %s\n",  status & (1 << 8)  ? "Yes" : "No" );
		printf("  ISC Enable:         %s\n",  status & (1 << 9)  ? "Yes" : "No" );
		printf("  Write Enable:       %s\n",  status & (1 << 10) ? "Writable" : "Not Writable");
		printf("  Read Enable:        %s\n",  status & (1 << 11) ? "Readable" : "Not Readable");
		printf("  Busy Flag:          %s\n",  status & (1 << 12) ? "Yes" : "No" );
		printf("  Fail Flag:          %s\n",  status & (1 << 13) ? "Yes" : "No" );
		printf("  Feature OTP:        %s\n",  status & (1 << 14) ? "Yes" : "No" );
		printf("  Decrypt Only:       %s\n",  status & (1 << 15) ? "Yes" : "No" );
		printf("  PWD Enable:         %s\n",  status & (1 << 16) ? "Yes" : "No" );
		printf("  Encrypt Preamble:   %s\n",  status & (1 << 20) ? "Yes" : "No" );
		printf("  Std Preamble:       %s\n",  status & (1 << 21) ? "Yes" : "No" );
		printf("  SPIm Fail 1:        %s\n",  status & (1 << 22) ? "Yes" : "No" );
		
		uint8_t bse_error = (status & (7 << 23)) >> 23;
		switch (bse_error){
			case 0b000: printf("  BSE Error Code:     No Error (0b000)\n"); break;
			case 0b001: printf("  BSE Error Code:     ID Error (0b001)\n"); break;
			case 0b010: printf("  BSE Error Code:     CMD Error - illegal command (0b010)\n"); break;
			case 0b011: printf("  BSE Error Code:     CRC Error (0b011)\n"); break;
			case 0b100: printf("  BSE Error Code:     PRMB Error - preamble error (0b100)\n"); break;
			case 0b101: printf("  BSE Error Code:     ABRT Error - configuration aborted by the user (0b101)\n"); break;
			case 0b110: printf("  BSE Error Code:     OVFL Error - data overflow error (0b110)\n"); break;
			case 0b111: printf("  BSE Error Code:     SDM Error - bitstream pass the size of SRAM array (0b111)\n"); break;
		}

		printf("  Execution Error:    %s\n",  status & (1 << 26) ? "Yes" : "No" );
		printf("  ID Error:           %s\n",  status & (1 << 27) ? "Yes" : "No" );
		printf("  Invalid Command:    %s\n",  status & (1 << 28) ? "Yes" : "No" );
		printf("  SED Error:          %s\n",  status & (1 << 29) ? "Yes" : "No" );
		printf("  Bypass Mode:        %s\n",  status & (1 << 30) ? "Yes" : "No" );
		printf("  Flow Through Mode:  %s\n",  status & (1 << 31) ? "Yes" : "No" );
	}
}

void print_nx_status_register(uint64_t status){	
	printf("NX Status Register: 0x%016lx\n", status);

	if(verbose){
		printf("  Transparent Mode:   %s\n",  status & (1 << 0)  ? "Yes" : "No" );
		printf("  Config Target:      ");
		uint8_t config_target = status & (0b111 << 1) >> 1;
		switch (config_target){
			case 0b000: printf("SRAM (0b000)\n"); break;
			case 0b001: printf("EFUSE Normal (0b001)\n"); break;
			case 0b010: printf("EFUSE Pseudo (0b010)\n"); break;
			case 0b011: printf("EFUSE Safe (0b011)\n"); break;
			default: printf("Invalid (%u)\n", config_target); break;
		}

		printf("  JTAG Active:        %s\n",  status & (1 << 4)  ? "Yes" : "No" );
		printf("  PWD Protection:     %s\n",  status & (1 << 5)  ? "Yes" : "No" );
		printf("  OTP:                %s\n",  status & (1 << 6)  ? "Yes" : "No" );
		printf("  DONE:               %s\n",  status & (1 << 8)  ? "Yes" : "No" );
		printf("  ISC Enable:         %s\n",  status & (1 << 9)  ? "Yes" : "No" );
		printf("  Write Enable:       %s\n",  status & (1 << 10) ? "Writable" : "Not Writable");
		printf("  Read Enable:        %s\n",  status & (1 << 11) ? "Readable" : "Not Readable");
		printf("  Busy Flag:          %s\n",  status & (1 << 12) ? "Yes" : "No" );
		printf("  Fail Flag:          %s\n",  status & (1 << 13) ? "Yes" : "No" );
		printf("  Decrypt Only:       %s\n",  status & (1 << 15) ? "Yes" : "No" );
		printf("  PWD Enable:         %s\n",  status & (1 << 16) ? "Yes" : "No" );
		printf("  PWD All:            %s\n",  status & (1 << 17) ? "Yes" : "No" );
		printf("  CID EN:             %s\n",  status & (1 << 18) ? "Yes" : "No" );
		printf("  Encrypt Preamble:   %s\n",  status & (1 << 21) ? "Yes" : "No" );
		printf("  Std Preamble:       %s\n",  status & (1 << 22) ? "Yes" : "No" );
		printf("  SPIm Fail 1:        %s\n",  status & (1 << 23) ? "Yes" : "No" );
		
		uint8_t bse_error = (status & (0b1111 << 24)) >> 24;
		switch (bse_error){
			case 0b0000: printf("  BSE Error Code:     No Error (0b000)\n"); break;
			case 0b0001: printf("  BSE Error Code:     ID Error (0b001)\n"); break;
			case 0b0010: printf("  BSE Error Code:     CMD Error - illegal command (0b010)\n"); break;
			case 0b0011: printf("  BSE Error Code:     CRC Error (0b011)\n"); break;
			case 0b0100: printf("  BSE Error Code:     PRMB Error - preamble error (0b100)\n"); break;
			case 0b0101: printf("  BSE Error Code:     ABRT Error - configuration aborted by the user (0b101)\n"); break;
			case 0b0110: printf("  BSE Error Code:     OVFL Error - data overflow error (0b110)\n"); break;
			case 0b0111: printf("  BSE Error Code:     SDM Error - bitstream pass the size of SRAM array (0b111)\n"); break;
			case 0b1000: printf("  BSE Error Code:     Authentication Error (0b1000)\n"); break;
			case 0b1001: printf("  BSE Error Code:     Authentication Setup Error (0b1001)\n"); break;
			case 0b1010: printf("  BSE Error Code:     Bitstream Engine Timeout Error (0b1010) \n"); break;
		}

		printf("  Execution Error:    %s\n",  status & (1 << 28) ? "Yes" : "No" );
		printf("  ID Error:           %s\n",  status & (1 << 29) ? "Yes" : "No" );
		printf("  Invalid Command:    %s\n",  status & (1 << 30) ? "Yes" : "No" );
		printf("  WDT Busy:           %s\n",  status & (1 << 31) ? "Yes" : "No" );
		printf("  Dry Run DONE:       %s\n",  status & (1UL << 33) ? "Yes" : "No" );
		
		uint8_t bse_error1 = (status & (0b1111UL << 34)) >> 34;
		switch (bse_error1){
			case 0b0000: printf("  BSE Error 1 Code: (Previous Bitstream)  No Error (0b000)\n"); break;
			case 0b0001: printf("  BSE Error 1 Code: (Previous Bitstream)  ID Error (0b001)\n"); break;
			case 0b0010: printf("  BSE Error 1 Code: (Previous Bitstream)  CMD Error - illegal command (0b010)\n"); break;
			case 0b0011: printf("  BSE Error 1 Code: (Previous Bitstream)  CRC Error (0b011)\n"); break;
			case 0b0100: printf("  BSE Error 1 Code: (Previous Bitstream)  PRMB Error - preamble error (0b100)\n"); break;
			case 0b0101: printf("  BSE Error 1 Code: (Previous Bitstream)  ABRT Error - configuration aborted by the user (0b101)\n"); break;
			case 0b0110: printf("  BSE Error 1 Code: (Previous Bitstream)  OVFL Error - data overflow error (0b110)\n"); break;
			case 0b0111: printf("  BSE Error 1 Code: (Previous Bitstream)  SDM Error - bitstream pass the size of SRAM array (0b111)\n"); break;
			case 0b1000: printf("  BSE Error 1 Code: (Previous Bitstream)  Authentication Error (0b1000)\n"); break;
			case 0b1001: printf("  BSE Error 1 Code: (Previous Bitstream)  Authentication Setup Error (0b1001)\n"); break;
			case 0b1010: printf("  BSE Error 1 Code: (Previous Bitstream)  Bitstream Engine Timeout Error (0b1010) \n"); break;
		}

		printf("  Bypass Mode:        %s\n",  status & (1UL << 38) ? "Yes" : "No" );
		printf("  Flow Through Mode:  %s\n",  status & (1UL << 39) ? "Yes" : "No" );
		printf("  SFDP Timeout:       %s\n",  status & (1UL << 42) ? "Yes" : "No" );
		printf("  Key Destroy Pass:   %s\n",  status & (1UL << 43) ? "Yes" : "No" );
		printf("  INITN:              %s\n",  status & (1UL << 44) ? "Yes" : "No" );
		printf("  I3C Parity Error 2: %s\n",  status & (1UL << 45) ? "Yes" : "No" );
		printf("  Init Bus ID Error:  %s\n",  status & (1UL << 46) ? "Yes" : "No" );
		printf("  I3C Parity Error 1: %s\n",  status & (1UL << 47) ? "Yes" : "No" );
		
		uint8_t auth_mode = (status & (0b11UL << 48)) >> 48;
		switch (auth_mode){
			case 0b00: printf("  Authentication Mode:  No Auth (0b00)\n"); break;
			case 0b01: printf("  Authentication Mode:  ECDSA (0b01)\n"); break;
			case 0b10: printf("  Authentication Mode:  HMAC (0b10)\n"); break;
			case 0b11: printf("  Authentication Mode:  No Auth (0b11)\n"); break;
		}

		printf("  Authentication Done: %s\n",  status & (1UL << 50) ? "Yes" : "No" );
		printf("  Dry Run Authentication Done: %s\n",  status & (1UL << 51) ? "Yes" : "No" );
		printf("  JTAG Locked:         %s\n",  status & (1UL << 52) ? "Yes" : "No" );
		printf("  SSPI Locked:         %s\n",  status & (1UL << 53) ? "Yes" : "No" );
		printf("  I2C/I3C Locked:      %s\n",  status & (1UL << 54) ? "Yes" : "No" );
		printf("  PUB Read Lock:       %s\n",  status & (1UL << 55) ? "Yes" : "No" );
		printf("  PUB Write Lock:      %s\n",  status & (1UL << 56) ? "Yes" : "No" );
		printf("  FEA Read Lock:       %s\n",  status & (1UL << 57) ? "Yes" : "No" );
		printf("  FEA Write Lock:      %s\n",  status & (1UL << 58) ? "Yes" : "No" );
		printf("  AES Read Lock:       %s\n",  status & (1UL << 59) ? "Yes" : "No" );
		printf("  AES Write Lock:      %s\n",  status & (1UL << 60) ? "Yes" : "No" );
		printf("  PWD Read Lock:       %s\n",  status & (1UL << 61) ? "Yes" : "No" );
		printf("  PWD Write Lock:      %s\n",  status & (1UL << 62) ? "Yes" : "No" );
		printf("  Global Lock:         %s\n",  status & (1UL << 63) ? "Yes" : "No" );
	}

}

static void read_status_register(){

	uint8_t data[8] = {LSC_READ_STATUS};

	jtag_go_to_state(STATE_SHIFT_IR);
	jtag_tap_shift(data, data, 8, true);

	data[0] = 0;
	jtag_go_to_state(STATE_SHIFT_DR);
	//jtag_go_to_state(STATE_PAUSE_DR);
	
	if(connected_device.type == TYPE_ECP5){
		jtag_tap_shift(data, data, 32, true);
		uint32_t status = 0;
		
		/* Format the status into a 32bit value */
		for(int i = 0; i< 4; i++)
			status = data[i] << 24 | status >> 8;

		print_ecp5_status_register(status);
	}else if(connected_device.type == TYPE_NX){

		jtag_tap_shift(data, data, 64, true);

		uint64_t status = 0;
		
		/* Format the status into a 32bit value */
		for(int i = 0; i< 8; i++)
			status = (uint64_t)data[i] << 56 | status >> 8;

		print_nx_status_register(status);
	}
}



static void enter_spi_background_mode(){

	uint8_t data[4] = {0x3A};

	jtag_go_to_state(STATE_SHIFT_IR);
	jtag_tap_shift(data, data, 8, true);

	/* These bytes seem to be required to un-lock the SPI interface */
	data[0] = 0xFE;
	data[1] = 0x68;
	jtag_go_to_state(STATE_SHIFT_DR);
	jtag_tap_shift(data, data, 16, true);

	/* Entering IDLE is essential */
	jtag_go_to_state(STATE_RUN_TEST_IDLE);
}


void ecp_jtag_cmd(uint8_t cmd){
	uint8_t data[1] = {cmd};

	jtag_go_to_state(STATE_SHIFT_IR);
	jtag_tap_shift(data, data, 8, true);

	jtag_go_to_state(STATE_RUN_TEST_IDLE);
	jtag_wait_time(32);	
}

void ecp_jtag_cmd8(uint8_t cmd, uint8_t param){
	uint8_t data[1] = {cmd};

	jtag_go_to_state(STATE_SHIFT_IR);
	jtag_tap_shift(data, data, 8, true);

	data[0] = param;
	jtag_go_to_state(STATE_SHIFT_DR);
	jtag_tap_shift(data, data, 8, true);

	jtag_go_to_state(STATE_RUN_TEST_IDLE);
	jtag_wait_time(32);	
}

// ---------------------------------------------------------
// iceprog implementation
// ---------------------------------------------------------

static void help(const char *progname)
{
	fprintf(stderr, "Simple programming tool for Lattice ECP5/NX using FTDI-based JTAG programmers.\n");
	fprintf(stderr, "Usage: %s [-b|-n|-c] <input file>\n", progname);
	fprintf(stderr, "       %s -r|-R<bytes> <output file>\n", progname);
	fprintf(stderr, "       %s -S <input file>\n", progname);
	fprintf(stderr, "       %s -t\n", progname);
	fprintf(stderr, "\n");
	fprintf(stderr, "General options:\n");
	fprintf(stderr, "  -d <device string>    use the specified USB device [default: i:0x0403:0x6010 or i:0x0403:0x6014]\n");
	fprintf(stderr, "                          d:<devicenode>               (e.g. d:002/005)\n");
	fprintf(stderr, "                          i:<vendor>:<product>         (e.g. i:0x0403:0x6010)\n");
	fprintf(stderr, "                          i:<vendor>:<product>:<index> (e.g. i:0x0403:0x6010:0)\n");
	fprintf(stderr, "                          s:<vendor>:<product>:<serial-string>\n");
	fprintf(stderr, "  -I [ABCD]             connect to the specified interface on the FTDI chip\n");
	fprintf(stderr, "                          [default: A]\n");
	fprintf(stderr, "  -o <offset in bytes>  start address for read/write [default: 0]\n");
	fprintf(stderr, "                          (append 'k' to the argument for size in kilobytes,\n");
	fprintf(stderr, "                          or 'M' for size in megabytes)\n");
	fprintf(stderr, "  -k <divider>          divider for SPI clock [default: 1]\n");
	fprintf(stderr, "                          clock speed is 6MHz/divider");
	fprintf(stderr, "  -s                    slow SPI. (50 kHz instead of 6 MHz)\n");
	fprintf(stderr, "                          Equivalent to -k 30\n");
	fprintf(stderr, "  -v                    verbose output\n");
	fprintf(stderr, "  -i [4,32,64]          select erase block size [default: 64k]\n");
	fprintf(stderr, "  -a                    reinitialize the device after any operation\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Mode of operation:\n");
	fprintf(stderr, "  [default]             write file contents to flash, then verify\n");
	fprintf(stderr, "  -X                    write file contents to flash only\n");	
	fprintf(stderr, "  -r                    read first 256 kB from flash and write to file\n");
	fprintf(stderr, "  -R <size in bytes>    read the specified number of bytes from flash\n");
	fprintf(stderr, "                          (append 'k' to the argument for size in kilobytes,\n");
	fprintf(stderr, "                          or 'M' for size in megabytes)\n");
	fprintf(stderr, "  -l                    Verify every written page immediately after write\n");
	fprintf(stderr, "                          This make the total write/verify process slower but allows\n");
	fprintf(stderr, "                          discovery of flash programming/connection issues quicker\n");
	fprintf(stderr, "  -c                    do not write flash, only verify (`check')\n");
	fprintf(stderr, "  -S                    perform SRAM programming\n");
	fprintf(stderr, "  -t                    just read the flash ID sequence\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Erase mode (only meaningful in default mode):\n");
	fprintf(stderr, "  [default]             erase aligned chunks of 64kB in write mode\n");
	fprintf(stderr, "                          This means that some data after the written data (or\n");
	fprintf(stderr, "                          even before when -o is used) may be erased as well.\n");
	fprintf(stderr, "  -b                    bulk erase entire flash before writing\n");
	fprintf(stderr, "  -e <size in bytes>    erase flash as if we were writing that number of bytes\n");
	fprintf(stderr, "  -n                    do not erase flash before writing\n");
	fprintf(stderr, "  -p                    disable write protection before erasing or writing\n");
	fprintf(stderr, "                          This can be useful if flash memory appears to be\n");
	fprintf(stderr, "                          bricked and won't respond to erasing or programming.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Miscellaneous options:\n");
	fprintf(stderr, "      --help            display this help and exit\n");
	fprintf(stderr, "  --                    treat all remaining arguments as filenames\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Exit status:\n");
	fprintf(stderr, "  0 on success,\n");
	fprintf(stderr, "  1 if a non-hardware error occurred (e.g., failure to read from or\n");
	fprintf(stderr, "    write to a file, or invoked with invalid options),\n");
	fprintf(stderr, "  2 if communication with the hardware failed (e.g., cannot find the\n");
	fprintf(stderr, "    iCE FTDI USB device),\n");
	fprintf(stderr, "  3 if verification of the data failed.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "If you have a bug report, please file an issue on github:\n");
	fprintf(stderr, "  https://github.com/gregdavill/ecpprog/issues\n");
}

int main(int argc, char **argv)
{
	/* used for error reporting */
	const char *my_name = argv[0];
	for (size_t i = 0; argv[0][i]; i++)
		if (argv[0][i] == '/')
			my_name = argv[0] + i + 1;

	int read_size = 256 * 1024;
	int erase_block_size = 64;
	int erase_size = 0;
	int rw_offset = 0;
	int clkdiv = 1;

	bool reinitialize = false;
	bool read_mode = false;
	bool check_mode = false;
	bool interleaved_verify = false;
	bool erase_mode = false;
	bool bulk_erase = false;
	bool dont_erase = false;
	bool prog_sram = false;
	bool test_mode = false;
	bool disable_protect = false;
	bool disable_verify = false;
	const char *filename = NULL;
	const char *devstr = NULL;
	int ifnum = 0;

#ifdef _WIN32
	_setmode(_fileno(stdin), _O_BINARY);
	_setmode(_fileno(stdout), _O_BINARY);
#endif

	static struct option long_options[] = {
		{"help", no_argument, NULL, -2},
		{NULL, 0, NULL, 0}
	};

	/* Decode command line parameters */
	int opt;
	char *endptr;
	while ((opt = getopt_long(argc, argv, "d:i:I:rR:e:o:k:slcabnStvpX", long_options, NULL)) != -1) {
		switch (opt) {
		case 'd': /* device string */
			devstr = optarg;
			break;
		case 'i': /* block erase size */
			if (!strcmp(optarg, "4"))
				erase_block_size = 4;
			else if (!strcmp(optarg, "32"))
				erase_block_size = 32;
			else if (!strcmp(optarg, "64"))
				erase_block_size = 64;
			else {
				fprintf(stderr, "%s: `%s' is not a valid erase block size (must be `4', `32' or `64')\n", my_name, optarg);
				return EXIT_FAILURE;
			}
			break;
		case 'I': /* FTDI Chip interface select */
			if (!strcmp(optarg, "A"))
				ifnum = 0;
			else if (!strcmp(optarg, "B"))
				ifnum = 1;
			else if (!strcmp(optarg, "C"))
				ifnum = 2;
			else if (!strcmp(optarg, "D"))
				ifnum = 3;
			else {
				fprintf(stderr, "%s: `%s' is not a valid interface (must be `A', `B', `C', or `D')\n", my_name, optarg);
				return EXIT_FAILURE;
			}
			break;
		case 'r': /* Read 256 bytes to file */
			read_mode = true;
			break;
		case 'R': /* Read n bytes to file */
			read_mode = true;
			read_size = strtol(optarg, &endptr, 0);
			if (*endptr == '\0')
				/* ok */;
			else if (!strcmp(endptr, "k"))
				read_size *= 1024;
			else if (!strcmp(endptr, "M"))
				read_size *= 1024 * 1024;
			else {
				fprintf(stderr, "%s: `%s' is not a valid size\n", my_name, optarg);
				return EXIT_FAILURE;
			}
			break;
		case 'e': /* Erase blocks as if we were writing n bytes */
			erase_mode = true;
			erase_size = strtol(optarg, &endptr, 0);
			if (*endptr == '\0')
				/* ok */;
			else if (!strcmp(endptr, "k"))
				erase_size *= 1024;
			else if (!strcmp(endptr, "M"))
				erase_size *= 1024 * 1024;
			else {
				fprintf(stderr, "%s: `%s' is not a valid size\n", my_name, optarg);
				return EXIT_FAILURE;
			}
			break;
		case 'o': /* set address offset */
			rw_offset = strtol(optarg, &endptr, 0);
			if (*endptr == '\0')
				/* ok */;
			else if (!strcmp(endptr, "k"))
				rw_offset *= 1024;
			else if (!strcmp(endptr, "M"))
				rw_offset *= 1024 * 1024;
			else {
				fprintf(stderr, "%s: `%s' is not a valid offset\n", my_name, optarg);
				return EXIT_FAILURE;
			}
			break;
		case 'k': /* set clock div */
			clkdiv = strtol(optarg, &endptr, 0);
                        if (clkdiv < 1 || clkdiv > 65536) {
				fprintf(stderr, "%s: clock divider must be in range 1-65536 `%s' is not a valid divider\n", my_name, optarg);
				return EXIT_FAILURE;
                        }
			break;
		case 's': /* use slow SPI clock */
			clkdiv = 30;
			break;
		case 'l': /* Check every page after write */
			interleaved_verify = true;
			break;
		case 'c': /* do not write just check */
			check_mode = true;
			break;
		case 'a': /* reinitialize ECP5 device reading new configuration */
			reinitialize = true;
			break;
		case 'b': /* bulk erase before writing */
			bulk_erase = true;
			break;
		case 'n': /* do not erase before writing */
			dont_erase = true;
			break;
		case 'S': /* write to sram directly */
			prog_sram = true;
			break;
		case 't': /* just read flash id */
			test_mode = true;
			break;
		case 'v': /* provide verbose output */
			verbose = true;
			break;
		case 'p': /* disable flash protect before erase/write */
			disable_protect = true;
			break;
		case 'X': /* disable verification */
			disable_verify = true;
			break;
		case -2:
			help(argv[0]);
			return EXIT_SUCCESS;
		default:
			/* error message has already been printed */
			fprintf(stderr, "Try `%s --help' for more information.\n", argv[0]);
			return EXIT_FAILURE;
		}
	}

	/* Make sure that the combination of provided parameters makes sense */

	if (read_mode + erase_mode + check_mode + prog_sram + test_mode > 1) {
		fprintf(stderr, "%s: options `-r'/`-R', `-e`, `-c', `-S', and `-t' are mutually exclusive\n", my_name);
		return EXIT_FAILURE;
	}

	if (bulk_erase && dont_erase) {
		fprintf(stderr, "%s: options `-b' and `-n' are mutually exclusive\n", my_name);
		return EXIT_FAILURE;
	}

	if (disable_protect && (read_mode || check_mode || prog_sram || test_mode)) {
		fprintf(stderr, "%s: option `-p' only valid in programming mode\n", my_name);
		return EXIT_FAILURE;
	}

	if (bulk_erase && (read_mode || check_mode || prog_sram || test_mode)) {
		fprintf(stderr, "%s: option `-b' only valid in programming mode\n", my_name);
		return EXIT_FAILURE;
	}

	if (dont_erase && (read_mode || check_mode || prog_sram || test_mode)) {
		fprintf(stderr, "%s: option `-n' only valid in programming mode\n", my_name);
		return EXIT_FAILURE;
	}

	if (rw_offset != 0 && prog_sram) {
		fprintf(stderr, "%s: option `-o' not supported in SRAM mode\n", my_name);
		return EXIT_FAILURE;
	}

	if (rw_offset != 0 && test_mode) {
		fprintf(stderr, "%s: option `-o' not supported in test mode\n", my_name);
		return EXIT_FAILURE;
	}

	if (interleaved_verify && (read_mode || check_mode || prog_sram || test_mode)) {
		fprintf(stderr, "%s: option `-l' only valid in programming mode\n", my_name);
		return EXIT_FAILURE;
	}

	if (optind + 1 == argc) {
		if (test_mode) {
			fprintf(stderr, "%s: test mode doesn't take a file name\n", my_name);
			fprintf(stderr, "Try `%s --help' for more information.\n", argv[0]);
			return EXIT_FAILURE;
		}
		filename = argv[optind];
	} else if (optind != argc) {
		fprintf(stderr, "%s: too many arguments\n", my_name);
		fprintf(stderr, "Try `%s --help' for more information.\n", argv[0]);
		return EXIT_FAILURE;
	} else if (bulk_erase || disable_protect) {
		filename = "/dev/null";
	} else if (!test_mode && !erase_mode && !disable_protect) {
		fprintf(stderr, "%s: missing argument\n", my_name);
		fprintf(stderr, "Try `%s --help' for more information.\n", argv[0]);
		return EXIT_FAILURE;
	}

	/* open input/output file in advance
	   so we can fail before initializing the hardware */

	FILE *f = NULL;
	long file_size = -1;

	if (test_mode) {
		/* nop */;
	} else if (erase_mode) {
		file_size = erase_size;
	} else if (read_mode) {
		f = (strcmp(filename, "-") == 0) ? stdout : fopen(filename, "wb");
		if (f == NULL) {
			fprintf(stderr, "%s: can't open '%s' for writing: ", my_name, filename);
			perror(0);
			return EXIT_FAILURE;
		}
		file_size = read_size;
	} else {
		f = (strcmp(filename, "-") == 0) ? stdin : fopen(filename, "rb");
		if (f == NULL) {
			fprintf(stderr, "%s: can't open '%s' for reading: ", my_name, filename);
			perror(0);
			return EXIT_FAILURE;
		}

		/* For regular programming, we need to read the file
		   twice--once for programming and once for verifying--and
		   need to know the file size in advance in order to erase
		   the correct amount of memory.

		   See if we can seek on the input file.  Checking for "-"
		   as an argument isn't enough as we might be reading from a
		   named pipe, or contrarily, the standard input may be an
		   ordinary file. */

		if (!prog_sram) {
			if (fseek(f, 0L, SEEK_END) != -1) {
				file_size = ftell(f);
				if (file_size == -1) {
					fprintf(stderr, "%s: %s: ftell: ", my_name, filename);
					perror(0);
					return EXIT_FAILURE;
				}
				if (fseek(f, 0L, SEEK_SET) == -1) {
					fprintf(stderr, "%s: %s: fseek: ", my_name, filename);
					perror(0);
					return EXIT_FAILURE;
				}
			} else {
				FILE *pipe = f;

				f = tmpfile();
				if (f == NULL) {
					fprintf(stderr, "%s: can't open temporary file\n", my_name);
					return EXIT_FAILURE;
				}
				file_size = 0;

				while (true) {
					static unsigned char buffer[4096];
					size_t rc = fread(buffer, 1, 4096, pipe);
					if (rc <= 0)
						break;
					size_t wc = fwrite(buffer, 1, rc, f);
					if (wc != rc) {
						fprintf(stderr, "%s: can't write to temporary file\n", my_name);
						return EXIT_FAILURE;
					}
					file_size += rc;
				}
				fclose(pipe);

				/* now seek to the beginning so we can
				   start reading again */
				fseek(f, 0, SEEK_SET);
			}
		}
	}

	// ---------------------------------------------------------
	// Initialize USB connection to FT2232H
	// ---------------------------------------------------------

	fprintf(stderr, "init..\n");
	jtag_init(ifnum, devstr, clkdiv);

	read_idcode();
	read_status_register();

	if (test_mode)
	{
		/* Reset ECP5 to release SPI interface */
		ecp_jtag_cmd8(ISC_ENABLE,0);
		usleep(10000);
		ecp_jtag_cmd8(ISC_ERASE,0);
		usleep(10000);
		ecp_jtag_cmd(ISC_DISABLE);

		/* Put device into SPI bypass mode */
		enter_spi_background_mode();

		flash_reset();
		flash_read_id();

		flash_read_status();
	}
	else if (prog_sram)
	{
		// ---------------------------------------------------------
		// Reset
		// ---------------------------------------------------------
		fprintf(stderr, "reset..\n");

		ecp_jtag_cmd8(ISC_ENABLE, 0);
		ecp_jtag_cmd8(ISC_ERASE, 0);
		ecp_jtag_cmd8(LSC_RESET_CRC, 0);

		read_status_register();

		// ---------------------------------------------------------
		// Program
		// ---------------------------------------------------------

		fprintf(stderr, "programming..\n");
		ecp_jtag_cmd(LSC_BITSTREAM_BURST);
		while (1) {
			const uint32_t len = 16*1024;
			static unsigned char buffer[16*1024];
			int rc = fread(buffer, 1, len, f);
			if (rc <= 0)
				break;
			if (verbose)
				fprintf(stderr, "sending %d bytes.\n", rc);

			for(int i = 0; i < rc; i++){
				buffer[i] = bit_reverse(buffer[i]);
			}

			jtag_go_to_state(STATE_CAPTURE_DR);
			jtag_tap_shift(buffer, buffer, rc*8, false);
		}
	
		ecp_jtag_cmd(ISC_DISABLE);
		read_status_register();	
	}
	else /* program flash */
	{
		// ---------------------------------------------------------
		// Reset
		// ---------------------------------------------------------

		fprintf(stderr, "reset..\n");
		/* Reset ECP5 to release SPI interface */
		ecp_jtag_cmd8(ISC_ENABLE, 0);
		ecp_jtag_cmd8(ISC_ERASE, 0);
		ecp_jtag_cmd8(ISC_DISABLE, 0);

		/* Put device into SPI bypass mode */
		enter_spi_background_mode();

		flash_reset();

		flash_read_id();


		// ---------------------------------------------------------
		// Program
		// ---------------------------------------------------------
		unsigned int read_blocksize = 256 * 32;

		if (!read_mode && !check_mode)
		{
			if (disable_protect)
			{
				flash_write_enable();
				flash_disable_protection();
			}
			
			if (!dont_erase)
			{
				if (bulk_erase)
				{
					flash_write_enable();
					flash_bulk_erase();
					flash_wait();
				}
				else
				{
					fprintf(stderr, "file size: %ld\n", file_size);

					int block_size = erase_block_size << 10;
					int block_mask = block_size - 1;
					int begin_addr = rw_offset & ~block_mask;
					int end_addr = (rw_offset + file_size + block_mask) & ~block_mask;

					for (int addr = begin_addr; addr < end_addr; addr += block_size) {
						flash_write_enable();
						switch(erase_block_size) {
							case 4:
								flash_4kB_sector_erase(addr);
								break;
							case 32:
								flash_32kB_sector_erase(addr);
								break;
							case 64:
								flash_64kB_sector_erase(addr);
								break;
						}
						if (verbose) {
							fprintf(stderr, "Status after block erase:\n");
							flash_read_status();
						}
						flash_wait();
					}
				}
			}

			if (!erase_mode)
			{
				uint8_t buffer_flash[read_blocksize], buffer_file[read_blocksize], buffer_tx[256];
				for (int rc, addr = 0; true; addr += rc) {
					rc = fread(buffer_file, 1, read_blocksize, f);
					if (rc <= 0)
						break;

					for (int written, block_offset = 0; block_offset < rc; block_offset += written) {
						/* Show progress */
						fprintf(stderr, "\r\033[0Kprogramming..  %04u/%04lu", addr + block_offset, file_size);

						int write_addr = rw_offset + addr + block_offset;
						written = 256 - write_addr % 256;
						if (block_offset + written > read_blocksize) {
							written = read_blocksize - block_offset;
						}

						memcpy(buffer_tx, buffer_file + block_offset, written);

						flash_write_enable();
						flash_prog(write_addr, buffer_tx, written);
						flash_wait();
					}

					if (!disable_verify && interleaved_verify) {
						flash_start_read(rw_offset + addr);
						flash_continue_read(buffer_flash, rc);
						flash_wait();
						if (memcmp(buffer_file, buffer_flash, rc)) {
							fprintf(stderr, "Found difference between flash and file!\n");
							jtag_error(3);
						}
					}

				}

				fprintf(stderr, "\n");
				/* seek to the beginning for second pass */
				fseek(f, 0, SEEK_SET);
			}
		}

		// ---------------------------------------------------------
		// Read/Verify
		// ---------------------------------------------------------

		if (read_mode) {

			flash_start_read(rw_offset);
			for (int addr = 0; addr < read_size; addr += read_blocksize) {
				uint8_t buffer[read_blocksize];

				/* Show progress */
				fprintf(stderr, "\r\033[0Kreading..    %04u/%04u", addr + read_blocksize, read_size);

				flash_continue_read(buffer, read_blocksize);
				fwrite(buffer, read_size - addr > read_blocksize ? read_blocksize : read_size - addr, 1, f);
			}
			fprintf(stderr, "\n");
		} else if (!erase_mode && !disable_verify && !interleaved_verify) {
			
			flash_start_read(rw_offset);
			for (int addr = 0; addr < file_size; addr += read_blocksize) {
				uint8_t buffer_flash[read_blocksize], buffer_file[read_blocksize];

				int rc = fread(buffer_file, 1, read_blocksize, f);
				if (rc <= 0)
					break;
				
				flash_continue_read(buffer_flash, rc);
				
				/* Show progress */
				fprintf(stderr, "\r\033[0Kverify..       %04u/%04lu", addr + rc, file_size);
				if (memcmp(buffer_file, buffer_flash, rc)) {
					fprintf(stderr, "Found difference between flash and file!\n");
					jtag_error(3);
				}

			}
			fprintf(stderr, "  VERIFY OK\n");
		}
	}

	if (reinitialize) {
		fprintf(stderr, "rebooting ECP5...\n");
		ecp_jtag_cmd(LSC_REFRESH);
	}

	if (f != NULL && f != stdin && f != stdout)
		fclose(f);

	// ---------------------------------------------------------
	// Exit
	// ---------------------------------------------------------

	fprintf(stderr, "Bye.\n");
	jtag_deinit();
	return 0;
}
