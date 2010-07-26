#include <stdint.h>
#include <string.h>
#include <util/delay.h>

#include <arduino/pins.h>
#include <arduino/serial.h>
#include <arduino/timer2.h>

#define PIN_CLK         2
#define PIN_DATA        3
#define PIN_GREEN_LED   4
#define PIN_YELLOW_LED  5
#define PIN_OPEN_LOCK   6
#define PIN_DAYMODE     7
#define PIN_STATUS_LED  a5
/* #define PIN_STATUS_LED  19 */

static volatile char clk = 0;
static volatile uint8_t value = 0;
static volatile uint8_t cnt = 0;
static uint8_t data[256];
static char hash_string[] = "HASH+0000000000000000000000000000000000000000\n";

static volatile int int_counter = 0;
static volatile int second = 0;

#define ALLINONE
#define EXPORT static

#define SHA1_SHORTCODE
#include "sha1.c"
#undef SHA1_SHORTCODE

#define SERIAL_BUFSIZE 64
#include "serial.c"
#undef SERIAL_BUFSIZE

#undef EXPORT
#undef ALLINONE

static void
data_reset()
{
	unsigned int i;

	for (i = 0; i < 256; i++)
		data[i] = i;

	cnt = 0;
}

ISR(INT0_vect)
{
	if (pin_is_high(PIN_DATA))
		value |= 1 << (7 - clk);

	clk++;
	int_counter = 0;
	second = 0;
	if (clk == 8) {
		if (cnt < 255) {
			data[cnt] = value;
			cnt++;
		}
		clk = 0;
		value = 0;
	}
}

/*
 * triggered every millisecond
 */
timer2_interrupt_a()
{
	int_counter += 1;
	if (int_counter == 250) {
		clk = 0;
		value = 0;
		second++;
		int_counter = 0;
	}
}

static char hex_digit[] = {
	'0', '1', '2', '3', '4', '5', '6', '7',
	'8', '9', 'A', 'B', 'C', 'D', 'E', 'F'
};

static void
digest_to_hex(const char digest[SHA1_DIGEST_LENGTH], char *out)
{
	uint8_t i;

	for (i = 0; i < 20; i++) {
		*out++ = hex_digit[(uint8_t)digest[i] >> 4];
		*out++ = hex_digit[(uint8_t)digest[i] & 0x0F];
	}
}

int
main()
{
	serial_baud_9600();
	serial_mode_8e2();
	serial_rxtx();
	serial_interrupt_rx_enable();

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

	EICRA = 0x03; /* INT0 rising edge on SCL */
	EIMSK = 0x01; /* enable only int0        */

	data_reset();

	/* setup timer2 to trigger interrupt a
	 * once every millisecond */
	timer2_mode_ctc();
	timer2_compare_a_set(124);
	timer2_clock_d128();
	timer2_interrupt_a_enable();

	sei();

	while (1) {
		if (data[cnt - 1] == 0xB4) {
			if (cnt >= 10) {
				struct sha1_context ctx;
				char digest[SHA1_DIGEST_LENGTH];

				sha1_init(&ctx);
				sha1_update(&ctx, (char *)data, 256);
				sha1_final(&ctx, digest);
				digest_to_hex(digest, hash_string + 5);
				serial_print(hash_string);
			}
			data_reset();
		}

		if (second > 10*4) {
			serial_print("ALIVE\n");
			second = 0;
			data_reset();
		}

		switch (serial_getchar()) {
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

		_delay_ms(20);
	}

	return 0;
}
