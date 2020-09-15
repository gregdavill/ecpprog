/*
 *  iceprog -- simple programming tool for FTDI-based Lattice iCE programmers
 *
 *  Copyright (C) 2015  Clifford Wolf <clifford@clifford.at>
 *  Copyright (C) 2018  Piotr Esden-Tempski <piotr@esden.net>
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
 */

#ifndef MPSSE_H
#define MPSSE_H

#include <stdint.h>



/* MPSSE engine command definitions */
enum mpsse_cmd
{
	/* Mode commands */
	MC_SETB_LOW = 0x80, /* Set Data bits LowByte */
	MC_READB_LOW = 0x81, /* Read Data bits LowByte */
	MC_SETB_HIGH = 0x82, /* Set Data bits HighByte */
	MC_READB_HIGH = 0x83, /* Read data bits HighByte */
	MC_LOOPBACK_EN = 0x84, /* Enable loopback */
	MC_LOOPBACK_DIS = 0x85, /* Disable loopback */
	MC_SET_CLK_DIV = 0x86, /* Set clock divisor */
	MC_FLUSH = 0x87, /* Flush buffer fifos to the PC. */
	MC_WAIT_H = 0x88, /* Wait on GPIOL1 to go high. */
	MC_WAIT_L = 0x89, /* Wait on GPIOL1 to go low. */
	MC_TCK_X5 = 0x8A, /* Disable /5 div, enables 60MHz master clock */
	MC_TCK_D5 = 0x8B, /* Enable /5 div, backward compat to FT2232D */
	MC_EN_3PH_CLK = 0x8C, /* Enable 3 phase clk, DDR I2C */
	MC_DIS_3PH_CLK = 0x8D, /* Disable 3 phase clk */
	MC_CLK_N = 0x8E, /* Clock every bit, used for JTAG */
	MC_CLK_N8 = 0x8F, /* Clock every byte, used for JTAG */
	MC_CLK_TO_H = 0x94, /* Clock until GPIOL1 goes high */
	MC_CLK_TO_L = 0x95, /* Clock until GPIOL1 goes low */
	MC_EN_ADPT_CLK = 0x96, /* Enable adaptive clocking */
	MC_DIS_ADPT_CLK = 0x97, /* Disable adaptive clocking */
	MC_CLK8_TO_H = 0x9C, /* Clock until GPIOL1 goes high, count bytes */
	MC_CLK8_TO_L = 0x9D, /* Clock until GPIOL1 goes low, count bytes */
	MC_TRI = 0x9E, /* Set IO to only drive on 0 and tristate on 1 */
	/* CPU mode commands */
	MC_CPU_RS = 0x90, /* CPUMode read short address */
	MC_CPU_RE = 0x91, /* CPUMode read extended address */
	MC_CPU_WS = 0x92, /* CPUMode write short address */
	MC_CPU_WE = 0x93, /* CPUMode write extended address */
};


/* Transfer Command bits */

/* All byte based commands consist of:
 * - Command byte
 * - Length lsb
 * - Length msb
 *
 * If data out is enabled the data follows after the above command bytes,
 * otherwise no additional data is needed.
 * - Data * n
 *
 * All bit based commands consist of:
 * - Command byte
 * - Length
 *
 * If data out is enabled a byte containing bitst to transfer follows.
 * Otherwise no additional data is needed. Only up to 8 bits can be transferred
 * per transaction when in bit mode.
 */

/* b 0000 0000
 *   |||| |||`- Data out negative enable. Update DO on negative clock edge.
 *   |||| ||`-- Bit count enable. When reset count represents bytes.
 *   |||| |`--- Data in negative enable. Latch DI on negative clock edge.
 *   |||| `---- LSB enable. When set clock data out LSB first.
 *   ||||
 *   |||`------ Data out enable
 *   ||`------- Data in enable
 *   |`-------- TMS mode enable
 *   `--------- Special command mode enable. See mpsse_cmd enum.
 */

#define MC_DATA_TMS  (0x40) /* When set use TMS mode */
#define MC_DATA_IN   (0x20) /* When set read data (Data IN) */
#define MC_DATA_OUT  (0x10) /* When set write data (Data OUT) */
#define MC_DATA_LSB  (0x08) /* When set input/output data LSB first. */
#define MC_DATA_ICN  (0x04) /* When set receive data on negative clock edge */
#define MC_DATA_BITS (0x02) /* When set count bits not bytes */
#define MC_DATA_OCN  (0x01) /* When set update data on negative clock edge */


void mpsse_check_rx(void);
void mpsse_error(int status);
uint8_t mpsse_recv_byte(void);
void mpsse_xfer(uint8_t* data_buffer, uint16_t send_length, uint16_t receive_length);
void mpsse_send_byte(uint8_t data);
void mpsse_send_spi(uint8_t *data, int n);
void mpsse_xfer_spi(uint8_t *data, int n);
uint8_t mpsse_xfer_spi_bits(uint8_t data, int n);
void mpsse_set_gpio(uint8_t gpio, uint8_t direction);
int mpsse_readb_low(void);
int mpsse_readb_high(void);
void mpsse_send_dummy_bytes(uint8_t n);
void mpsse_send_dummy_bit(void);
void mpsse_init(int ifnum, const char *devstr, int clkdiv);
void mpsse_close(void);

#endif /* MPSSE_H */
