//------------------------------------------------------------------------------
// main.c
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2022 homelith
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//------------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>

#include "boards/pico.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/binary_info.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "hardware/vreg.h"
#include "dvi.h"
#include "dvi_serialiser.h"
#include "common_dvi_pin_configs.h"

#include "testcard_60x1280_rgb565.h"

#define VREG_VSEL VREG_VOLTAGE_1_20
#define DVI_TIMING dvi_timing_480x1920p_20hz

#define FRAME_WIDTH             60
#define FRAME_HEIGHT            240
#define SCROLL_HEIGHT           1280
#define SCROLL_TICK_DEFAULT     4

#define RXPOLL_INIT_MS          10000
#define RXPOLL_INTERVAL_MS      180000
#define RXPOLL_NEXT_MS          500

#define XMODEM_TRIGGER_DELAY_MS 2000

#define UART0_TX_QUEUE_SIZE     1024
#define UART1_TX_QUEUE_SIZE     128

#define SIPF_LINEBUF_SIZE       256
#define SIPF_FILENAME_MAX       64

#define ST_IDLE                 0
#define ST_RX_POLLING           1
#define ST_XMODEM_REQ_WAIT      2
#define ST_XMODEM_RECV          3

#define EOT                     0x04
#define NAK                     0x15
#define ACK                     0x06


typedef struct QUEUE_tag{
	uint8_t *buf;
	uint16_t head;
	uint16_t tail;
	uint16_t size;
} QUEUE;

struct dvi_inst dvi0;
uint8_t uart0_tx_queue_buf[UART0_TX_QUEUE_SIZE];
uint8_t uart1_tx_queue_buf[UART1_TX_QUEUE_SIZE];

bool queue_push(QUEUE *q, uint8_t data) {
	uint16_t next_head = (q->head == q->size - 1) ? 0 : q->head + 1;
	if (next_head != q->tail) {
		q->buf[q->head] = data;
		q->head = next_head;
		return true;
	} else {
		return false;
	}
}
bool queue_pop(QUEUE *q, uint8_t *data) {
	if (q->head != q->tail) {
		(*data) = q->buf[q->tail];
		q->tail = (q->tail == q->size - 1) ? 0 : q->tail + 1;
		return true;
	} else {
		(*data) = 0;
		return false;
	}
}
uint8_t queue_puts(QUEUE *q, uint8_t *str, uint16_t num, bool stop_on_null) {
	for (uint8_t i = 0; i < num; i ++) {
		if (stop_on_null && str[i] == '\0') {
			return i;
		}
		if (! queue_push(q, str[i])) {
			return i;
		}
	}
	return num;
}

uint8_t hex2char(uint8_t h) {
	if (h < 10) {
		return (uint8_t)(h + 48);
	} else if (h < 16) {
		return (uint8_t)(h + 87);
	} else {
		return '*';
	}
}
uint8_t char2hex(uint8_t c) {
	if (c < 48) {
		return 0;
	} else if (c < 58) {
		return (uint8_t)(c - 48);
	} else if (c < 65) {
		return 0;
	} else if (c < 71) {
		return (uint8_t)(c - 55);
	} else if (c < 97) {
		return 0;
	} else if (c < 103) {
		return (uint8_t)(c - 87);
	} else {
		return 0;
	}
}

void core1_main() {
	// core1 extracts pixel data from 'CPU0 -> CPU1' FIFO, encode it to tmds symbol,
	// and push tmds symbol to 'CPU1 -> PIO' FIFO
	dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);
	while (queue_is_empty(&dvi0.q_colour_valid))
		__wfe();
	dvi_start(&dvi0);
	dvi_scanbuf_x8scale_main_16bpp(&dvi0);
}


int main() {
	// queues
	QUEUE uart0_tx_queue;
	QUEUE uart1_tx_queue;
	uart0_tx_queue.buf  = uart0_tx_queue_buf;
	uart0_tx_queue.head = 0;
	uart0_tx_queue.tail = 0;
	uart0_tx_queue.size = UART0_TX_QUEUE_SIZE;
	uart1_tx_queue.buf  = uart1_tx_queue_buf;
	uart1_tx_queue.head = 0;
	uart1_tx_queue.tail = 0;
	uart1_tx_queue.size = UART1_TX_QUEUE_SIZE;

	// switch controls
	bool     sw_delayline[3] = {false, false, false};
	bool     sw = false;
	bool     sw_prev = false;
	bool     sw_rise = false;
	uint64_t sw_prev_tick = 0;

	// sipf controls
	uint8_t  sipf_state = ST_IDLE;
	uint64_t sipf_state_last_event_tick = time_us_64();
	uint8_t  sipf_filename[SIPF_FILENAME_MAX];
	uint16_t sipf_filename_len = 0;

	uint64_t sipf_rxpoll_next_tick = time_us_64() + RXPOLL_INIT_MS * 1000;

	uint8_t  sipf_rxpoll_line_cnt;
	uint8_t  sipf_rxpoll_linebuf[SIPF_LINEBUF_SIZE];
	uint16_t sipf_rxpoll_linebuf_idx;
	uint8_t  sipf_rxpoll_linebuf_accept_newline;
	bool     sipf_rxpoll_filename_available;
	bool     sipf_rxpoll_peek_next_msg;

	uint64_t sipf_xmodem_trigger_tick = 0xffffffffffffffffULL;
	uint16_t sipf_xmodem_block_cnt = 0;
	uint16_t sipf_xmodem_byte_cnt = 0;

	// dvi controls
	uint32_t dvi_update_idx = 0;
	int16_t dvi_line_offset = 0;
	uint16_t dvi_line_idx = 0;
	int16_t scroll_tick = SCROLL_TICK_DEFAULT;

	// system global setting
	vreg_set_voltage(VREG_VSEL);
	sleep_ms(10);
	set_sys_clock_khz(DVI_TIMING.bit_clk_khz, true);

	// setup LED and push button
	gpio_init(25);
	gpio_set_dir(25, GPIO_OUT);
	gpio_init(6);
	gpio_set_dir(6, GPIO_IN);
	gpio_pull_up(6);

	// set up serials
	// uart0 9600 baud may be overidden by USB UART according to CMakeLists.txt
	// uart1 (pin4 : TX, pin5 : RX) with baudrate 115200
	stdio_init_all();
	uart_init(uart1, 115200);
	gpio_set_function(4, GPIO_FUNC_UART);
	gpio_set_function(5, GPIO_FUNC_UART);

	// init bitbang DVI library
	dvi0.timing = &DVI_TIMING;
	dvi0.ser_cfg = DVI_DEFAULT_SERIAL_CONFIG;
	dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());

	// Core 1 will wait until it sees the first colour buffer, then start up the
	// DVI signalling.
	multicore_launch_core1(core1_main);

	// wait for SCM-LTEM1NRF-B booting up
	//for (uint8_t i = 0; i < 10; i ++) {
	//	gpio_put(25, true);
	//	sleep_ms(1000);
	//	gpio_put(25, false);
	//	sleep_ms(1000);
	//}

	// indicate init completed
	for (uint8_t i = 0; i < 3; i ++) {
		gpio_put(25, true);
		sleep_ms(200);
		gpio_put(25, false);
		sleep_ms(200);
	}
	queue_puts(&uart0_tx_queue, (uint8_t*)"pico_sign started\r\n", 256, true);

	while (true) {
		uint64_t curr_tick = time_us_64();

		//--------------------------------------------------------------
		// remove sw chattering and detect edge
		// 10ms sampling -> 3 stage consensus -> edge detection
		//--------------------------------------------------------------
		if (curr_tick - sw_prev_tick > 10000) {
			sw_prev_tick = curr_tick;
			sw_delayline[2] = sw_delayline[1];
			sw_delayline[1] = sw_delayline[0];
			sw_delayline[0] = !(gpio_get(6));
			if (sw_delayline[2] & sw_delayline[1] & sw_delayline[0]) {
				sw = true;
			}
			if (!(sw_delayline[2] | sw_delayline[1] | sw_delayline[0])) {
				sw = false;
			}
		}
		if (!(sw_prev) & sw) {
			sw_rise = true;
		} else {
			sw_rise = false;
		}
		sw_prev = sw;

		//--------------------------------------------------------------
		// tx queue rate limited flushing
		// (approx. 1 byte per 1 horizontal scan time == 9600 times/s)
		//--------------------------------------------------------------
		// pop from uart1_tx_queue and push to uart1
		if (uart_is_writable(uart1)) {
			uint8_t c;
			if (queue_pop(&uart1_tx_queue, &c)) {
				uart_putc(uart1, c);
			}
		}
		// pop from uart0_tx_queue and push to uart0
		if (uart_is_writable(uart0)) {
			uint8_t c;
			if (queue_pop(&uart0_tx_queue, &c)) {
				putchar(c);
			}
		}

		//--------------------------------------------------------------
		// control sipf-std-client via uart1
		//--------------------------------------------------------------
		// reset state after 30sec no activity
		if (curr_tick - sipf_state_last_event_tick > 30000000) {
			queue_puts(&uart0_tx_queue, (uint8_t*)"'\r\n", 256, true);
			queue_puts(&uart0_tx_queue, (uint8_t*)"timed out, return to initial state.\r\n", 256, true);
			gpio_put(25, false);
			sipf_rxpoll_next_tick = curr_tick + RXPOLL_INTERVAL_MS * 1000;
			sipf_state_last_event_tick = curr_tick;
			sipf_state = ST_IDLE;
		}

		if (sipf_state == ST_IDLE) {
			// drain unused input
			while (uart_is_readable(uart1)) {
				uart_getc(uart1);
			}
			// issue "$$RX" command after 180sec or rising edge of switch pressed
			// then move to ST_RX_POLLING state
			int64_t diff_tick = (int64_t)(curr_tick - sipf_rxpoll_next_tick);
			if (diff_tick > 0 || sw_rise) {
				queue_puts(&uart0_tx_queue, (uint8_t*)"send $$RX command to sipf\r\n", 256, true);
				queue_puts(&uart1_tx_queue, (uint8_t*)"$$RX\r\n", 256, true);
				gpio_put(25, true);
				sipf_rxpoll_line_cnt = 0;
				sipf_rxpoll_linebuf_idx = 0;
				sipf_rxpoll_linebuf_accept_newline = 0;
				sipf_rxpoll_filename_available = false;
				sipf_rxpoll_peek_next_msg = false;
				sipf_state = ST_RX_POLLING;
			}
			// supress resetting during IDLE state
			sipf_state_last_event_tick = curr_tick;
		} else if (sipf_state == ST_RX_POLLING) {
			// $$RX command and response example
			//
			// ```
			// $$RX
			// C6F5577AC1914E1E9AC6D53B2C38DD6C
			// 0000000000000000
			// 0000017FF9DEB4E0
			// 00
			// 01
			// 25 20 04 484F4745
			// OK
			// ```

			// read from uart1 in per-line parsing mode
			while (uart_is_readable(uart1)) {
				uint8_t c = (uint8_t)uart_getc(uart1);
				sipf_rxpoll_linebuf[sipf_rxpoll_linebuf_idx] = c;
				if (sipf_rxpoll_linebuf_accept_newline == 1 && c == '\n') {
					// dump output
					//queue_push(&uart0_tx_queue, hex2char(sipf_rxpoll_line_cnt & 0x0f));
					//queue_push(&uart0_tx_queue, ' ');
					//queue_puts(&uart0_tx_queue, sipf_rxpoll_linebuf, sipf_rxpoll_linebuf_idx + 1, false);

					// accept current message and reset state if line starts with "OK" or "NG"
					if ((sipf_rxpoll_linebuf[0] == 'O' && sipf_rxpoll_linebuf[1] == 'K') ||
						(sipf_rxpoll_linebuf[0] == 'N' && sipf_rxpoll_linebuf[1] == 'G')) {
						if (sipf_rxpoll_filename_available) {
							// print filename to be received
							queue_puts(&uart0_tx_queue, (uint8_t*)"got response with filename '", 256, true);
							queue_puts(&uart0_tx_queue, sipf_filename, sipf_filename_len, false);
							queue_puts(&uart0_tx_queue, (uint8_t*)"'\r\n", 256, true);
							// issue "$$FGET {filename}" command
							queue_puts(&uart0_tx_queue, (uint8_t*)"send $$FGET command to sipf\r\n", 256, true);
							queue_puts(&uart1_tx_queue, (uint8_t*)"$$FGET ", 256, true);
							queue_puts(&uart1_tx_queue, sipf_filename, sipf_filename_len, false);
							queue_puts(&uart1_tx_queue, (uint8_t*)"\r\n", 256, true);
							// state transition
							sipf_xmodem_trigger_tick = curr_tick + XMODEM_TRIGGER_DELAY_MS * 1000;
							sipf_state = ST_XMODEM_REQ_WAIT;
						} else {
							// if no $$FGET filename available, go to idle
							if (sipf_rxpoll_line_cnt == 1) {
								queue_puts(&uart0_tx_queue, (uint8_t*)"got response with no message\r\n", 256, true);
							} else {
								queue_puts(&uart0_tx_queue, (uint8_t*)"got response with invalid message\r\n", 256, true);
							}
							gpio_put(25, false);
							if (sipf_rxpoll_peek_next_msg) {
								sipf_rxpoll_next_tick = curr_tick + RXPOLL_NEXT_MS * 1000;
							} else {
								sipf_rxpoll_next_tick = curr_tick + RXPOLL_INTERVAL_MS * 1000;
							}
							sipf_state = ST_IDLE;
						}
						sipf_rxpoll_line_cnt = 0;
					} else if (sipf_rxpoll_line_cnt == 6) {
						// 7th line may includes first payload of sipf message object
						// {tag} {type} {length} {payload}
						// this program accepts variable length UTF-8 string payload (type == 0x20) and int16 (type == 0x03) value
						sipf_rxpoll_linebuf[SIPF_LINEBUF_SIZE-1] = '\0';
						uint32_t tmp_type;
						uint32_t tmp_len;
						sscanf((const char*)sipf_rxpoll_linebuf, "%*x %lx %lx", &tmp_type, &tmp_len);
						uint8_t type = (uint8_t)(tmp_type & 0x000000ff);
						uint8_t len = (uint8_t)(len & 0x000000ff);

						// TODO : this version use '9' magic number offset to detect payload start offset, we should not use them
						if (type == 0x20) {
							// accept UTF-8 payload as filename
							if (len > SIPF_FILENAME_MAX) {
								len = SIPF_FILENAME_MAX;
							}
							sipf_filename_len = len;
							sipf_rxpoll_filename_available = true;
							for (uint16_t i = 0; i < sipf_filename_len; i ++) {
								sipf_filename[i] = (char2hex(sipf_rxpoll_linebuf[i*2+9]) << 4) + char2hex(sipf_rxpoll_linebuf[i*2+10]);
							}
							sipf_rxpoll_peek_next_msg = true;
						} else if (type == 0x03) {
							// accept int16 value as scroll_tick
							int32_t tmp_val;
							sscanf((const char*)(sipf_rxpoll_linebuf+9), "%lx", &tmp_val);
							scroll_tick = (int16_t)tmp_val;
							sipf_rxpoll_peek_next_msg = true;
						}
						sipf_rxpoll_line_cnt ++;
					} else {
						sipf_rxpoll_line_cnt ++;
					}
					sipf_rxpoll_linebuf_accept_newline = 0;
					sipf_rxpoll_linebuf_idx = 0;
				} else if (sipf_rxpoll_linebuf_accept_newline == 0 && c == '\r') {
					sipf_rxpoll_linebuf_accept_newline = 1;
					sipf_rxpoll_linebuf_idx = (sipf_rxpoll_linebuf_idx != SIPF_LINEBUF_SIZE-2) ? sipf_rxpoll_linebuf_idx + 1 : sipf_rxpoll_linebuf_idx;
				} else {
					sipf_rxpoll_linebuf_accept_newline = 0;
					sipf_rxpoll_linebuf_idx = (sipf_rxpoll_linebuf_idx != SIPF_LINEBUF_SIZE-2) ? sipf_rxpoll_linebuf_idx + 1 : sipf_rxpoll_linebuf_idx;
				}
				sipf_state_last_event_tick = curr_tick;
			}
		} else if (sipf_state == ST_XMODEM_REQ_WAIT) {
			// wait 2 sec until transition to next state
			if (curr_tick > sipf_xmodem_trigger_tick) {
				queue_puts(&uart0_tx_queue, (uint8_t*)"send NAK to start xmodem recv\r\n", 256, true);
				queue_push(&uart1_tx_queue, NAK);

				sipf_xmodem_trigger_tick = 0xffffffffffffffffULL;
				sipf_xmodem_block_cnt = 0;
				sipf_xmodem_byte_cnt = 0;
				dvi_update_idx = 0;

				sipf_state_last_event_tick = curr_tick;
				sipf_state = ST_XMODEM_RECV;
			}
			while (uart_is_readable(uart1)) {
				// drain echo backs
				uart_getc(uart1);
			}
		} else if (sipf_state == ST_XMODEM_RECV) {
			// issue ACK command after 1 block received
			if (curr_tick > sipf_xmodem_trigger_tick) {
				queue_push(&uart0_tx_queue, '.');
				queue_push(&uart1_tx_queue, ACK);
				sipf_xmodem_trigger_tick = 0xffffffffffffffffULL;
				sipf_state_last_event_tick = curr_tick;
			}

			// read from xmodem enabled uart1 and store rgb565 data into pixel buffer
			while (uart_is_readable(uart1)) {
				uint8_t c = (uint8_t)uart_getc(uart1);
				// dump received byte to uart0
				//queue_push(&uart0_tx_queue, hex2char((c & 0xf0) >> 4));
				//queue_push(&uart0_tx_queue, hex2char(c & 0x0f));
				//queue_push(&uart0_tx_queue, ' ');
				//if (sipf_xmodem_byte_cnt % 16 == 15) {
				//	queue_puts(&uart0_tx_queue, (uint8_t*)"\r\n", 256, true);
				//}

				if (sipf_xmodem_byte_cnt == 0 && c == EOT) {
					// received EOT and return to initial state
					queue_puts(&uart0_tx_queue, (uint8_t*)"\r\n", 256, true);
					queue_puts(&uart0_tx_queue, (uint8_t*)"EOT received\r\n", 256, true);
					queue_puts(&uart0_tx_queue, (uint8_t*)"send ACK to finish xmodem recv\r\n", 256, true);
					queue_push(&uart1_tx_queue, ACK);
					gpio_put(25, false);
					sipf_rxpoll_next_tick = curr_tick + RXPOLL_NEXT_MS * 1000;
					sipf_state = ST_IDLE;
				} else if (3 <= sipf_xmodem_byte_cnt && sipf_xmodem_byte_cnt < 131) {
					if (dvi_update_idx < 153600) {
						testcard_60x1280[dvi_update_idx] = c;
					}
					dvi_update_idx ++;
					sipf_xmodem_byte_cnt ++;
				} else if (sipf_xmodem_byte_cnt == 131) {
					// received 1 block and prepare sending ACK
					sipf_xmodem_byte_cnt = 0;
					sipf_xmodem_block_cnt ++;
					sipf_xmodem_trigger_tick = curr_tick + 5000;
				} else {
					sipf_xmodem_byte_cnt ++;
				}
				sipf_state_last_event_tick = curr_tick;
			}
		}
	
		//--------------------------------------------------------------
		// feeding image line to DVI color linebuffer
		//--------------------------------------------------------------
		// push image buf to inter CPU0 -> CPU1 FIFO for 1 frame (60 x 240)
		const uint16_t *scanline = &((const uint16_t*)testcard_60x1280)[((dvi_line_idx + dvi_line_offset) % SCROLL_HEIGHT) * FRAME_WIDTH];
		queue_add_blocking_u32(&dvi0.q_colour_valid, &scanline);
		while (queue_try_remove_u32(&dvi0.q_colour_free, &scanline));

		// increment line index
		if (dvi_line_idx == (FRAME_HEIGHT - 1)) {
			dvi_line_idx = 0;

			// offset start line index
			dvi_line_offset += scroll_tick;
			if (dvi_line_offset >= SCROLL_HEIGHT) {
				dvi_line_offset -= SCROLL_HEIGHT;
			} else if (dvi_line_offset < 0) {
				dvi_line_offset += SCROLL_HEIGHT;
			}
		} else {
			dvi_line_idx ++;
		}
	}
}
