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


static inline void jtag_pulse_clock(void)
{
	//mpsse_send_byte(MC_CLK_N);
	//mpsse_send_byte(0x00);
    mpsse_send_byte(MC_DATA_TMS | MC_DATA_IN | MC_DATA_LSB | MC_DATA_BITS);
	mpsse_send_byte(0);
    mpsse_send_byte(0);
}

static inline uint8_t jtag_pulse_clock_and_read_tdo(bool tms, bool tdi)
{
	uint8_t ret;

    mpsse_send_byte(MC_DATA_TMS | MC_DATA_IN  | MC_DATA_LSB |  MC_DATA_BITS);
	mpsse_send_byte(0);

    uint8_t data = 0;
    if(tdi)
        data |= 0x80;
    if(tms)
        data |= 0x01;
        
    mpsse_send_byte(data);
	ret = mpsse_recv_byte();
    
	return (ret >> 7) & 1;
}


void jtag_tap_shift(
	uint8_t *input_data,
	uint8_t *output_data,
	uint32_t data_bits,
	bool must_end)
{

	

	printf("jtag_tap_shift(%u)\n", data_bits);
	uint32_t bit_count = data_bits;
	uint32_t byte_count = (data_bits + 7) / 8;

	uint8_t byte_out = input_data[byte_count-1];
	uint8_t tdo_byte = 0;


	if(byte_count > 1){
		mpsse_send_byte( MC_DATA_IN | MC_DATA_OUT | MC_DATA_LSB |MC_DATA_OCN);
		mpsse_send_byte((byte_count - 2) & 0xFF);		
		mpsse_send_byte((byte_count - 2) >> 8);	

		for(int i = 0; i < byte_count-1; i++){				
		mpsse_send_byte(input_data[i]);
		output_data[i] = mpsse_recv_byte();
		bit_count -= 8;
		}

	}

	printf("loop2: %u \n", bit_count);
	for (int j = 0; j < 8 && bit_count-- > 0; ++j) {
		
		bool tms = false;
		bool tdi = false;
		if (bit_count == 0 && must_end) {
			tms = true;
			jtag_state_ack(1);
		}
		if (byte_out & 1) {
			tdi = true;
		} else {
			tdi = false;
		}
		byte_out >>= 1;
		bool tdo = jtag_pulse_clock_and_read_tdo(tms, tdi);
		tdo_byte |= tdo << j;
	
	}
	output_data[byte_count-1] = tdo_byte;
		
	
}

void jtag_state_ack(bool tms)
{
	if (tms) {
		jtag_set_current_state((tms_transitions[jtag_current_state()] >> 4) & 0xf);
	} else {
		jtag_set_current_state(tms_transitions[jtag_current_state()] & 0xf);
	}
}

void jtag_state_step(bool tms)
{
    
    //mpsse_send_byte(MC_DATA_TMS | MC_DATA_LSB | MC_DATA_BITS);
	//mpsse_send_byte(0);
	if (tms) {
    //	mpsse_send_byte(1);
	} else {
    //    mpsse_send_byte(0);
	}
    
	jtag_state_ack(tms);
}


uint8_t bit_reverse(uint8_t);

void jtag_go_to_state(unsigned state)
{
	mpsse_purge();

	if (state == STATE_TEST_LOGIC_RESET) {
		for (int i = 0; i < 5; ++i) {
			jtag_state_step(true);
		}
		mpsse_send_byte(MC_DATA_TMS | MC_DATA_LSB | MC_DATA_BITS);
		mpsse_send_byte(5 - 1);
		mpsse_send_byte(0b11111);
		
	} else {
		uint8_t d = 0;
		uint8_t count = 0;
		while (jtag_current_state() != state) {
			d = (d >> 1) & ~0x80;
			if((tms_map[jtag_current_state()] >> state) & 1){
				d |= 0x80;
			}
			count++;

			jtag_state_step((tms_map[jtag_current_state()] >> state) & 1);
		}
		mpsse_send_byte(MC_DATA_TMS | MC_DATA_LSB | MC_DATA_BITS);
		mpsse_send_byte(count - 1);
		mpsse_send_byte(d >> (8-count));
		
	}
}

void jtag_wait_time(uint32_t microseconds)
{
	while (microseconds--) {
		jtag_pulse_clock();
	}
}

