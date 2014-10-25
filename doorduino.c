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

#include "tools/mfrc522.h"

#define PIN_CLK         2
#define PIN_DATA        3
#define PIN_GREEN_LED   4
#define PIN_YELLOW_LED  5
#define PIN_OPEN_LOCK   6
#define PIN_DAYMODE     7
#define PIN_RFID_ENABLE 9
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
#include "tools/serial.c"
#undef SERIAL_INBUF
#undef SERIAL_OUTBUF

#include "tools/softserial.c"

#define SHA1_SHORTCODE
#include "tools/sha1.c"
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

static uint8_t
hex2int(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	else if (c >= 'a' && c <= 'f')
		return c - ('a' - 10);
	else if (c >= 'A' && c <= 'F')
		return c - ('A' - 10);
	else
		return 0xff;
}


static void
copy_card_data_to_buffer(char *buf, uint8_t len)
{
  uint8_t i;
  /*
    We got an RFID tag.
    Copy it into the card reader buffer to
    emulate a read card data string.
  */
  for (i = 0; i < len && cnt < 255; ++i, ++cnt)
    data[cnt] = buf[i];
  for (i = 0; i < 3; i++) {
    pin_low(PIN_YELLOW_LED);
    _delay_ms(80);
    pin_high(PIN_YELLOW_LED);
    _delay_ms(80);
  }
}

static void
handle_rfid_input(void)
{
	static char buf[14];
	static uint8_t idx = 0;
	int c;
	uint8_t checksum;
	uint8_t i;

	for (;;) {
		c = softserial_getchar();
		switch (c) {
		case SOFTSERIAL_EOF:
			return;
		case 2:
			idx = 0;
			break;
		case 3:
			if (idx == 14 && cnt == 0) {
				/* Check for correct checksum and CR / LF */
				checksum = 0;
				for (i = 0; i < 12; i += 2)
					checksum ^= ((hex2int(buf[i]) << 4) |
						     hex2int(buf[i+1]));
				if (checksum)
					break;
				if (buf[12] != 13 || buf[13] != 10)
					break;
                                copy_card_data_to_buffer(buf, 10);
			}
		default:
			if (idx < 14)
			{
				buf[idx++] = c;
				second = 0;
			}
			break;
		}
	}
}


static void
handle_mfr_input(char *data, uint8_t len)
{
  if (len == 0)
    return;                                     /* No data */
  /*
    The MFR sends card data continously while the card is close to the reader.
    We only process it when the buffer is empty; this ensures that we only
    get the card data once. An idle timeout elsewhere clears the buffer after
    some time of inactivity.
  */
  if (cnt !=0)
    return;
  copy_card_data_to_buffer(data, len);
}


int
door_main(void)
{
	serial_init(9600, 8e2);

	pin_mode_output(PIN_RFID_ENABLE);

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
	pin_mode_output(PIN_RFID_ENABLE);
	pin_low(PIN_RFID_ENABLE);

        init_mfrc522();

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

                if (events & EV_TIME)
                {
                  char buf[20];
                  uint8_t len;

                  len = check_mfrc522(buf, sizeof(buf));
                  handle_mfr_input(buf, len);
                }

		events &= ~EV_TIME;

                /*
                  This code can be used during development, to simulate the
                  press of the '#' button 8 seconds after every idle timeout:

                if (second == 32 && cnt < 255)
                {
                  data[cnt] = 0xB4;
                  cnt++;
                  events |= EV_DATA;
                }
                */

		if (second > 10*4) {
			serial_print("ALIVE\n");
			second = 0;
			data_reset();
			continue;
		}
	}
}

#ifndef ARDUINO
int
main(int argc __attribute__((unused)), char *argv[] __attribute__((unused)))
{
  door_main();
}
#endif
