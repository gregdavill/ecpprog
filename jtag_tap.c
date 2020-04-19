/**
 * Code adapted from Arduino-JTAG;
 *    portions copyright (c) 2015 Marcelo Roberto Jimenez <marcelo.jimenez (at) gmail (dot) com>.
 *    portions copyright (c) 2019 Katherine J. Temkin <kate@ktemkin.com>
 *    portions copyright (c) 2019 Great Scott Gadgets <ktemkin@greatscottgadgets.com>
 */

#include <ftdi.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

#include "mpsse.h"
#include "jtag.h"

void jtag_state_ack(bool tms);

/*
 * Low nibble : TMS == 0
 * High nibble: TMS == 1
 */

#define TMS_T(TMS_HIGH_STATE, TMS_LOW_STATE) (((TMS_HIGH_STATE) << 4) | (TMS_LOW_STATE))

static const uint8_t tms_transitions[] = {
	/* STATE_TEST_LOGIC_RESET */ TMS_T(STATE_TEST_LOGIC_RESET, STATE_RUN_TEST_IDLE),
	/* STATE_RUN_TEST_IDLE    */ TMS_T(STATE_SELECT_DR_SCAN,   STATE_RUN_TEST_IDLE),
	/* STATE_SELECT_DR_SCAN   */ TMS_T(STATE_SELECT_IR_SCAN,   STATE_CAPTURE_DR),
	/* STATE_CAPTURE_DR       */ TMS_T(STATE_EXIT1_DR,         STATE_SHIFT_DR),
	/* STATE_SHIFT_DR         */ TMS_T(STATE_EXIT1_DR,         STATE_SHIFT_DR),
	/* STATE_EXIT1_DR         */ TMS_T(STATE_UPDATE_DR,        STATE_PAUSE_DR),
	/* STATE_PAUSE_DR         */ TMS_T(STATE_EXIT2_DR,         STATE_PAUSE_DR),
	/* STATE_EXIT2_DR         */ TMS_T(STATE_UPDATE_DR,        STATE_SHIFT_DR),
	/* STATE_UPDATE_DR        */ TMS_T(STATE_SELECT_DR_SCAN,   STATE_RUN_TEST_IDLE),
	/* STATE_SELECT_IR_SCAN   */ TMS_T(STATE_TEST_LOGIC_RESET, STATE_CAPTURE_IR),
	/* STATE_CAPTURE_IR       */ TMS_T(STATE_EXIT1_IR,         STATE_SHIFT_IR),
	/* STATE_SHIFT_IR         */ TMS_T(STATE_EXIT1_IR,         STATE_SHIFT_IR),
	/* STATE_EXIT1_IR         */ TMS_T(STATE_UPDATE_IR,        STATE_PAUSE_IR),
	/* STATE_PAUSE_IR         */ TMS_T(STATE_EXIT2_IR,         STATE_PAUSE_IR),
	/* STATE_EXIT2_IR         */ TMS_T(STATE_UPDATE_IR,        STATE_SHIFT_IR),
	/* STATE_UPDATE_IR        */ TMS_T(STATE_SELECT_DR_SCAN,   STATE_RUN_TEST_IDLE),
};

#define BITSTR(A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P) ( \
	((uint16_t)(A) << 15) | \
	((uint16_t)(B) << 14) | \
	((uint16_t)(C) << 13) | \
	((uint16_t)(D) << 12) | \
	((uint16_t)(E) << 11) | \
	((uint16_t)(F) << 10) | \
	((uint16_t)(G) <<  9) | \
	((uint16_t)(H) <<  8) | \
	((uint16_t)(I) <<  7) | \
	((uint16_t)(J) <<  6) | \
	((uint16_t)(K) <<  5) | \
	((uint16_t)(L) <<  4) | \
	((uint16_t)(M) <<  3) | \
	((uint16_t)(N) <<  2) | \
	((uint16_t)(O) <<  1) | \
	((uint16_t)(P) <<  0) )

/*
 * The index of this vector is the current state. The i-th bit tells you the
 * value TMS must assume in order to go to state "i".

------------------------------------------------------------------------------------------------------------
|                        |   || F | E | D | C || B | A | 9 | 8 || 7 | 6 | 5 | 4 || 3 | 2 | 1 | 0 ||   HEX  |
------------------------------------------------------------------------------------------------------------
| STATE_TEST_LOGIC_RESET | 0 || 0 | 0 | 0 | 0 || 0 | 0 | 0 | 0 || 0 | 0 | 0 | 0 || 0 | 0 | 0 | 1 || 0x0001 |
| STATE_RUN_TEST_IDLE    | 1 || 1 | 1 | 1 | 1 || 1 | 1 | 1 | 1 || 1 | 1 | 1 | 1 || 1 | 1 | 0 | 1 || 0xFFFD |
| STATE_SELECT_DR_SCAN   | 2 || 1 | 1 | 1 | 1 || 1 | 1 | 1 | 0 || 0 | 0 | 0 | 0 || 0 | x | 1 | 1 || 0xFE03 |
| STATE_CAPTURE_DR       | 3 || 1 | 1 | 1 | 1 || 1 | 1 | 1 | 1 || 1 | 1 | 1 | 0 || x | 1 | 1 | 1 || 0xFFE7 |
| STATE_SHIFT_DR         | 4 || 1 | 1 | 1 | 1 || 1 | 1 | 1 | 1 || 1 | 1 | 1 | 0 || 1 | 1 | 1 | 1 || 0xFFEF |
| STATE_EXIT1_DR         | 5 || 1 | 1 | 1 | 1 || 1 | 1 | 1 | 1 || 0 | 0 | x | 0 || 1 | 1 | 1 | 1 || 0xFF0F |
| STATE_PAUSE_DR         | 6 || 1 | 1 | 1 | 1 || 1 | 1 | 1 | 1 || 1 | 0 | 1 | 1 || 1 | 1 | 1 | 1 || 0xFFBF |
| STATE_EXIT2_DR         | 7 || 1 | 1 | 1 | 1 || 1 | 1 | 1 | 1 || x | 0 | 0 | 0 || 1 | 1 | 1 | 1 || 0xFF0F |
| STATE_UPDATE_DR        | 8 || 1 | 1 | 1 | 1 || 1 | 1 | 1 | x || 1 | 1 | 1 | 1 || 1 | 1 | 0 | 1 || 0xFEFD |
| STATE_SELECT_IR_SCAN   | 9 || 0 | 0 | 0 | 0 || 0 | 0 | x | 1 || 1 | 1 | 1 | 1 || 1 | 1 | 1 | 1 || 0x01FF |
| STATE_CAPTURE_IR       | A || 1 | 1 | 1 | 1 || 0 | x | 1 | 1 || 1 | 1 | 1 | 1 || 1 | 1 | 1 | 1 || 0xF3FF |
| STATE_SHIFT_IR         | B || 1 | 1 | 1 | 1 || 0 | 1 | 1 | 1 || 1 | 1 | 1 | 1 || 1 | 1 | 1 | 1 || 0xF7FF |
| STATE_EXIT1_IR         | C || 1 | 0 | 0 | x || 0 | 1 | 1 | 1 || 1 | 1 | 1 | 1 || 1 | 1 | 1 | 1 || 0x87FF |
| STATE_PAUSE_IR         | D || 1 | 1 | 0 | 1 || 1 | 1 | 1 | 1 || 1 | 1 | 1 | 1 || 1 | 1 | 1 | 1 || 0xDFFF |
| STATE_EXIT2_IR         | E || 1 | x | 0 | 0 || 0 | 1 | 1 | 1 || 1 | 1 | 1 | 1 || 1 | 1 | 1 | 1 || 0x87FF |
| STATE_UPDATE_IR        | F || x | 1 | 1 | 1 || 1 | 1 | 1 | 1 || 1 | 1 | 1 | 1 || 1 | 1 | 0 | 1 || 0x7FFD |
------------------------------------------------------------------------------------------------------------

*/
static const uint16_t tms_map[] = {
/* STATE_TEST_LOGIC_RESET */ BITSTR(  0, 0, 0, 0,   0, 0, 0, 0,   0, 0, 0, 0,   0, 0, 0, 1  ),
/* STATE_RUN_TEST_IDLE    */ BITSTR(  1, 1, 1, 1,   1, 1, 1, 1,   1, 1, 1, 1,   1, 1, 0, 1  ),
/* STATE_SELECT_DR_SCAN   */ BITSTR(  1, 1, 1, 1,   1, 1, 1, 0,   0, 0, 0, 0,   0, 0, 1, 1  ),
/* STATE_CAPTURE_DR       */ BITSTR(  1, 1, 1, 1,   1, 1, 1, 1,   1, 1, 1, 0,   0, 1, 1, 1  ),
/* STATE_SHIFT_DR         */ BITSTR(  1, 1, 1, 1,   1, 1, 1, 1,   1, 1, 1, 0,   1, 1, 1, 1  ),
/* STATE_EXIT1_DR         */ BITSTR(  1, 1, 1, 1,   1, 1, 1, 1,   0, 0, 0, 0,   1, 1, 1, 1  ),
/* STATE_PAUSE_DR         */ BITSTR(  1, 1, 1, 1,   1, 1, 1, 1,   1, 0, 1, 1,   1, 1, 1, 1  ),
/* STATE_EXIT2_DR         */ BITSTR(  1, 1, 1, 1,   1, 1, 1, 1,   0, 0, 0, 0,   1, 1, 1, 1  ),
/* STATE_UPDATE_DR        */ BITSTR(  1, 1, 1, 1,   1, 1, 1, 0,   1, 1, 1, 1,   1, 1, 0, 1  ),
/* STATE_SELECT_IR_SCAN   */ BITSTR(  0, 0, 0, 0,   0, 0, 0, 1,   1, 1, 1, 1,   1, 1, 1, 1  ),
/* STATE_CAPTURE_IR       */ BITSTR(  1, 1, 1, 1,   0, 0, 1, 1,   1, 1, 1, 1,   1, 1, 1, 1  ),
/* STATE_SHIFT_IR         */ BITSTR(  1, 1, 1, 1,   0, 1, 1, 1,   1, 1, 1, 1,   1, 1, 1, 1  ),
/* STATE_EXIT1_IR         */ BITSTR(  1, 0, 0, 0,   0, 1, 1, 1,   1, 1, 1, 1,   1, 1, 1, 1  ),
/* STATE_PAUSE_IR         */ BITSTR(  1, 1, 0, 1,   1, 1, 1, 1,   1, 1, 1, 1,   1, 1, 1, 1  ),
/* STATE_EXIT2_IR         */ BITSTR(  1, 0, 0, 0,   0, 1, 1, 1,   1, 1, 1, 1,   1, 1, 1, 1  ),
/* STATE_UPDATE_IR        */ BITSTR(  0, 1, 1, 1,   1, 1, 1, 1,   1, 1, 1, 1,   1, 1, 0, 1  ),
};

static uint8_t current_state;

uint8_t jtag_current_state(void)
{
	return current_state;
}

void jtag_set_current_state(uint8_t state)
{
	current_state = state;
}


/**
 * Hook for any per-platform initialization that needs to occur.
 */
__attribute__((weak)) void jtag_platform_init(void)
{

}


/**
 * Performs any start-of-day tasks necessary to talk JTAG to our FPGA.
 */
void jtag_init(void)
{
	jtag_platform_init();
	jtag_set_current_state(STATE_TEST_LOGIC_RESET);
    jtag_go_to_state(STATE_TEST_LOGIC_RESET);
}

uint8_t data[1024];
uint8_t* ptr;
uint8_t rx_cnt;

extern struct ftdi_context mpsse_ftdic;

static inline uint8_t jtag_pulse_clock_and_read_tdo(bool tms, bool tdi)
{
	uint8_t ret;
    *ptr++ = MC_DATA_TMS | MC_DATA_IN | MC_DATA_LSB | MC_DATA_BITS;
	*ptr++ =  0;        
    *ptr++ = (tdi ? 0x80 : 0) | (tms ? 0x01 : 0);
	rx_cnt++;
}


void jtag_tap_shift(
	uint8_t *input_data,
	uint8_t *output_data,
	uint32_t data_bits,
	bool must_end)
{

	uint32_t bit_count = data_bits;
	uint32_t byte_count = (data_bits + 7) / 8;
	rx_cnt = 0;
	ptr = data;

	for (uint32_t i = 0; i < byte_count; ++i) {
		uint8_t byte_out = input_data[i];
		uint8_t tdo_byte = 0;
		for (int j = 0; j < 8 && bit_count-- > 0; ++j) {
            bool tms = false;
			if (bit_count == 0 && must_end) {
                tms = true;
				jtag_state_ack(1);
			}
			jtag_pulse_clock_and_read_tdo(tms, byte_out & 1);
			byte_out >>= 1;
		}
	}

	mpsse_xfer(data, ptr-data, rx_cnt);
	
	/* Data out from the FTDI is actually from an internal shift register
	 * Instead of reconstructing the bitpattern, we can just take every 8th byte.*/
	for(int i = 0; i < rx_cnt/8; i++)
		output_data[i] = data[7+i*8];
}

void jtag_state_ack(bool tms)
{
	if (tms) {
		jtag_set_current_state((tms_transitions[jtag_current_state()] >> 4) & 0xf);
	} else {
		jtag_set_current_state(tms_transitions[jtag_current_state()] & 0xf);
	}
}

void jtag_go_to_state(unsigned state)
{

	if (state == STATE_TEST_LOGIC_RESET) {
		for (int i = 0; i < 5; ++i) {
			jtag_state_ack(true);
		}

		uint8_t data[3] = { 
			MC_DATA_TMS | MC_DATA_LSB | MC_DATA_BITS,
			5 - 1,
			0b11111
		};
		mpsse_xfer(data, 3, 0);
		
	} else {
		uint8_t d = 0;
		uint8_t count = 0;

		uint8_t* ptr = data;

		while (jtag_current_state() != state) {
			uint8_t data[3] = {
				MC_DATA_TMS | MC_DATA_LSB | MC_DATA_ICN | MC_DATA_BITS,
				0,
				(tms_map[jtag_current_state()] >> state) & 1
			};

			jtag_state_ack((tms_map[jtag_current_state()] >> state) & 1);
			mpsse_xfer(data, 3, 0);
		}
	}
}

void jtag_wait_time(uint32_t microseconds)
{
	uint16_t bytes = microseconds / 8;
	uint8_t remain = microseconds % 8;

	uint8_t data[3] = {
		MC_CLK_N8,
		bytes & 0xFF,
		(bytes >> 8) & 0xFF
	};
	mpsse_xfer(data, 3, 0);

	if(remain){
		data[0] = MC_CLK_N;
		data[1] = remain;
		mpsse_xfer(data, 2, 0);
	}
}

