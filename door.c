#include <stdint.h>
#include <string.h>
#include <util/delay.h>

#include <arduino/pins.h>
#include <arduino/serial.h>
#include <arduino/timer2.h>

#define SHA_LITTLE_ENDIAN

/* define this if you want to process more then 65535 bytes */
/* #define SHA_BIG_DATA */

/* initial values */
#define init_h0  0x67452301
#define init_h1  0xEFCDAB89
#define init_h2  0x98BADCFE
#define init_h3  0x10325476
#define init_h4  0xC3D2E1F0

/* bit-shift operation */
#define rol(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))

#define INIT_TIMER_COUNT 6
#define RESET_TIMER2 TCNT2 = INIT_TIMER_COUNT

#define PIN_CLK         2
#define PIN_DATA        3
#define PIN_GREEN_LED   4
#define PIN_YELLOW_LED  5
#define PIN_OPEN_LOCK   6
#define PIN_DAYMODE     7
#define PIN_STATUS_LED  a5
/* #define PIN_STATUS_LED  19 */

union _message {
	unsigned char data[64];
	uint32_t w[16];
} message;

struct shastate {
	uint32_t h0;
	uint32_t h1;
	uint32_t h2;
	uint32_t h3;
	uint32_t h4;
#ifdef SHA_BIG_DATA
	uint32_t count;
#else
	uint16_t count;
#endif
};

union _digest {
	uint8_t data[20];
	struct shastate state;
} shadigest;

static void SHA1Init();
static void SHA1Block(const unsigned char *data, const uint8_t len);
static void SHA1Done();
static void SHA1Once(const unsigned char *data, int len);

static volatile char clk;
static volatile uint8_t value;
static volatile uint8_t cnt;
static uint8_t data[256];
static char hash_string[] = "HASH+0000000000000000000000000000000000000000\n";

static volatile int int_counter = 0;
static volatile int second = 0;

#define SERIAL_BUFSIZE 64

struct serial_ringbuf {
	uint8_t buf[SERIAL_BUFSIZE];
	uint8_t start;
	uint8_t end;
};

static volatile struct serial_ringbuf serial_input;

serial_interrupt_rx()
{
	uint8_t end = serial_input.end;

	serial_input.buf[end] = serial_read();
	serial_input.end = (end + 1) & (SERIAL_BUFSIZE - 1);
}

static char
serial_getchar()
{
	uint8_t start = serial_input.start;
	char r;

	if (start == serial_input.end)
		return '\0';

	r = serial_input.buf[start];
	serial_input.start = (start + 1) & (SERIAL_BUFSIZE - 1);

	return r;
}

static void
serial_print(const char *str)
{
	uint8_t c;

	for (c = *str; c; c = *++str) {
		while (!serial_writeable());

		serial_write(c);
	}
}

static void
data_reset()
{
	unsigned int i;

	for (i = 0; i < 256; i++)
		data[i] = i;
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
	RESET_TIMER2;
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
digest_to_hex(const uint8_t data[20], char *out)
{
	uint8_t i;

	for (i = 0; i < 20; i++) {
		*out++ = hex_digit[data[i] >> 4];
		*out++ = hex_digit[data[i] & 0x0F];
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
	clk = 0;
	cnt = 0;
	value = 0;
	data_reset();
	second = 0;

	/* setup timer2 to trigger interrupt a
	 * once every millisecond */
	timer2_mode_ctc();
	timer2_compare_a_set(124);
	timer2_clock_d128();
	timer2_interrupt_a_enable();

	sei();

	while (1) {
		if (data[cnt - 1] == (uint8_t)0xB4) {
			if (cnt >= 10) {
				SHA1Once(data, 256);
				digest_to_hex(shadigest.data, hash_string + 5);
				serial_print(hash_string);
			}
			data_reset();
			cnt = 0;
		}

		if (second > 10*4) {
			serial_print("ALIVE\n");
			second = 0;
			data_reset();
			cnt = 0;
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

/* processes one endianess-corrected block, provided in message */
static void
SHA1()
{
	uint8_t i;
	uint32_t a,b,c,d,e,f,k,t;

	a = shadigest.state.h0;
	b = shadigest.state.h1;
	c = shadigest.state.h2;
	d = shadigest.state.h3;
	e = shadigest.state.h4;

	/* main loop: 80 rounds */
	for (i = 0; i <= 79; i++) {
		if (i <= 19) {
			f = d ^ (b & (c ^ d));
			k = 0x5A827999;
		} else if (i <= 39) {
			f = b ^ c ^ d;
			k = 0x6ED9EBA1;
		} else if (i <= 59) {
			f = (b & c) | (d & (b | c));
			k = 0x8F1BBCDC;
		} else {
			f = b ^ c ^ d;
			k = 0xCA62C1D6;
		}

		/* blow up to 80 dwords while in the loop, save some RAM */
		if (i >= 16) {
			t = rol(message.w[(i+13) & 15] ^
			        message.w[(i+8) & 15] ^
			        message.w[(i+2)&15] ^
				message.w[i & 15], 1);
			message.w[i & 15] = t;
		}

		t = rol(a, 5) + f + e + k + message.w[i & 15];
		e = d;
		d = c;
		c = rol(b, 30);
		b = a;
		a = t;
	}

	shadigest.state.h0 += a;
	shadigest.state.h1 += b;
	shadigest.state.h2 += c;
	shadigest.state.h3 += d;
	shadigest.state.h4 += e;
}

static void
SHA1Init()
{
	shadigest.state.h0 = init_h0;
	shadigest.state.h1 = init_h1;
	shadigest.state.h2 = init_h2;
	shadigest.state.h3 = init_h3;
	shadigest.state.h4 = init_h4;
	shadigest.state.count = 0;
}

/*
 * Hashes blocks of 64 bytes of data.
 * Only the last block *must* be smaller than 64 bytes.
 */
static void
SHA1Block(const unsigned char *data, const uint8_t len)
{
	uint8_t i;

	/* clear all bytes in data block that are not overwritten anyway */
	for (i = len >> 2; i <= 15; i++)
		message.w[i] = 0;

#ifdef SHA_LITTLE_ENDIAN
	/* swap bytes */
	for (i = 0; i < len; i += 4) {
		message.data[i] = data[i+3];
		message.data[i+1] = data[i+2];
		message.data[i+2] = data[i+1];
		message.data[i+3] = data[i];
	}
#else
	memcpy(message.data, data, len);
#endif

	/* remember number of bytes processed for final block */
	shadigest.state.count += len;

	if (len < 64) {
		/* final block: mask bytes accidentally copied by for-loop
		 * and do the padding */
		message.w[len >> 2] &= 0xffffffffL << (((~len & 3) * 8) + 8);
		message.w[len >> 2] |= 0x80L << ((~len & 3) * 8);
		/* there is space for a qword containing the size at the end
		 * of the block */
		if (len <= 55)
			message.w[15] = (uint32_t)(shadigest.state.count) * 8;
	}

	SHA1();

	/* was last data block, but there wasn't space for the size:
	 * process another block */
	if (len >= 56 && len < 64) {
		for (i = 0; i <= 14; i++)
			message.w[i] = 0;

		message.w[15] = (uint32_t)(shadigest.state.count) * 8;
		SHA1();
	}
}

/* Correct the endianess if needed */
static void
SHA1Done()
{
#ifdef SHA_LITTLE_ENDIAN
	uint8_t i;
	unsigned char j;
	/* swap bytes */
	for (i = 0; i <= 4; i++) {
		j = shadigest.data[4*i];
		shadigest.data[4*i] = shadigest.data[4*i + 3];
		shadigest.data[4*i + 3] = j;
		j = shadigest.data[4*i + 1];
		shadigest.data[4*i + 1] = shadigest.data[4*i + 2];
		shadigest.data[4*i + 2] = j;
	}
#endif
}

/* Hashes just one arbitrarily sized chunk of data */
static void
SHA1Once(const unsigned char* data, int len)
{
	SHA1Init();
	while (len >= 0) {
		SHA1Block(data, len > 64 ? 64 : len);
		len -= 64;
		data += 64;
	}
	SHA1Done();
}
