/*
 * This file is part of doorduino.
 *
 * doorduino is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or(at your option) any later version.
 *
 * doorduino is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with doorduino.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <string.h>
#include <util/delay.h>

#include <arduino/pins.h>
#include <arduino/serial.h>
#include <arduino/timer1.h>
#include <arduino/sleep.h>

#define PIN_CLK         2
#define PIN_DATA        3
#define PIN_GREEN_LED   4
#define PIN_YELLOW_LED  5
#define PIN_OPEN_LOCK   6
#define PIN_DAYMODE     7
#define PIN_STATUS_LED  A5

static volatile char clk = 0;
static volatile uint8_t value = 0;
static volatile uint8_t cnt = 0;
static uint8_t data[256];

static volatile int second = 0;

enum events {
	EV_NONE   = 0,
	EV_SERIAL = 1 << 0,
	EV_TIME   = 1 << 1,
	EV_DATA   = 1 << 2
};

volatile uint8_t events = EV_NONE;

#define SERIAL_INBUF 64
#define SERIAL_OUTBUF 128
#include "serial.c"
#undef SERIAL_INBUF
#undef SERIAL_OUTBUF

#include "softserial.c"

#define SHA1_SHORTCODE
#include "sha1.c"
#undef SHA1_SHORTCODE

static void
data_reset(void)
{
	unsigned int i;

	for (i = 0; i < 256; i++)
		data[i] = i;

	cnt = 0;
}

/*
 * triggered when the clock signal goes high
 */
pin2_interrupt()
{
	if (pin_is_high(PIN_DATA))
		value |= 1 << (7 - clk);

	clk++;
	timer1_clock_reset();
	timer1_count_set(0);
	second = 0;
	if (clk == 8) {
		if (cnt < 255) {
			data[cnt] = value;
			cnt++;
			events |= EV_DATA;
		}
		clk = 0;
		value = 0;
	}
}

/*
 * triggered 4 times every second
 */
timer1_interrupt_a()
{
	clk = 0;
	value = 0;
	second++;
	events |= EV_TIME;
}

static void
handle_serial_input(void)
{
	while (1) {
		switch (serial_getchar()) {
		case '\0':
			cli();
			if (!serial_available())
				events &= ~EV_SERIAL;
			sei();
			return;

		case 'O': /* open */
			pin_low(PIN_GREEN_LED);
			pin_low(PIN_OPEN_LOCK);
			_delay_ms(500);
			pin_high(PIN_OPEN_LOCK);
			serial_print("OPENAKCK\n");
			pin_high(PIN_GREEN_LED);
			break;

		case 'D': /* day */
			pin_low(PIN_GREEN_LED);
			pin_low(PIN_DAYMODE);     /* day mode   */
			pin_high(PIN_STATUS_LED); /* status on  */
			break;

		case 'N': /* night */
			pin_high(PIN_GREEN_LED);
			pin_high(PIN_DAYMODE);    /* nightmode  */
			pin_low(PIN_STATUS_LED);  /* status off */
			break;

		case 'R': /* rejected */
			pin_low(PIN_YELLOW_LED);
			_delay_ms(200);
			pin_high(PIN_YELLOW_LED);
			_delay_ms(200);
			pin_low(PIN_YELLOW_LED);
			_delay_ms(200);
			pin_high(PIN_YELLOW_LED);
			break;

		case 'V': /* validated */
			pin_low(PIN_GREEN_LED);
			_delay_ms(300);
			pin_high(PIN_GREEN_LED);
			_delay_ms(200);
			pin_low(PIN_GREEN_LED);
			_delay_ms(300);
			pin_high(PIN_GREEN_LED);
			_delay_ms(200);
			pin_low(PIN_GREEN_LED);
			_delay_ms(300);
			pin_high(PIN_GREEN_LED);
			break;
		}
	}
}

static void
handle_rfid_input(void)
{
	static char buf[10];
	static uint8_t idx = 0;
	int c;
	uint8_t i;
	for (;;) {
		c = softserial_getchar();
		switch (c) {
		case SOFTSERIAL_EOF:
			return;
		case 10:
			idx = 0;
			break;
		case 13:
			if (idx == 10) {
				/*
				  We got an RFID tag.
				  Copy it into the card reader buffer to
				  emulate a read card data string.
				*/
				data_reset();
				for (i = 0; i < 10 && cnt < 255; ++i, ++cnt)
					data[cnt] = buf[i];
			}
		default:
			if (idx < 10)
				buf[idx++] = c;
			break;
		}
	}
}

int
main(void)
{
	serial_init(9600, 8e2);

	pin13_mode_output();

	pin_mode_input(PIN_CLK);         /* clk             */
	pin_mode_input(PIN_DATA);        /* data            */
	pin_mode_output(PIN_GREEN_LED);  /* green led lock  */
	pin_mode_output(PIN_YELLOW_LED); /* yellow led lock */
	pin_mode_output(PIN_OPEN_LOCK);  /* open            */
	pin_mode_output(PIN_DAYMODE);    /* stay open       */
	pin_mode_output(PIN_STATUS_LED); /* yellow status   */

	pin_high(PIN_OPEN_LOCK);
	pin_high(PIN_DAYMODE);
	pin_high(PIN_GREEN_LED);
	pin_high(PIN_YELLOW_LED);

	/* trigger pin2 interrupt when the clock
	 * signal goes high */
	pin2_interrupt_mode_rising();
	pin2_interrupt_enable();

	data_reset();

	/* setup timer1 to trigger interrupt a 4 times a second */
	timer1_mode_ctc();
	timer1_compare_a_set(62499);
	timer1_clock_d64();
	timer1_interrupt_a_enable();

        softserial_init();

	sleep_mode_idle();

	while (1) {
		/*
		 * sleep if no new events need to be handled
		 * while avoiding race conditions. see
		 * http://www.nongnu.org/avr-libc/user-manual/group__avr__sleep.html
		 */
		cli();
		if (events == EV_NONE && !ev_softserial) {
			sleep_enable();
			sei();
			sleep_cpu();
			sleep_disable();
			continue;
		}
		sei();

		if (events & EV_SERIAL) {
			handle_serial_input();
			continue;
		}

		if (ev_softserial) {
			handle_rfid_input();
		}

		events &= ~EV_DATA;
		if (cnt > 0 && data[cnt - 1] == 0xB4) {
			if (cnt >= 10) {
				struct sha1_context ctx;
				char digest[SHA1_DIGEST_LENGTH];

				sha1_init(&ctx);
				sha1_update(&ctx, (char *)data, 256);
				sha1_final(&ctx, digest);
				serial_print("HASH+");
				serial_hexdump(digest, SHA1_DIGEST_LENGTH);
				serial_print("\n");
			}
			data_reset();
			continue;
		}

		events &= ~EV_TIME;
		if (second > 10*4) {
			serial_print("ALIVE\n");
			second = 0;
			data_reset();
			continue;
		}
	}
}
