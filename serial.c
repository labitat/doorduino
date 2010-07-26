#ifndef ALLINONE
#include <arduino/serial.h>

#define EXPORT
#endif

static volatile struct {
	uint8_t buf[SERIAL_BUFSIZE];
	uint8_t start;
	uint8_t end;
} serial_input;

serial_interrupt_rx()
{
	uint8_t end = serial_input.end;

	serial_input.buf[end] = serial_read();
	serial_input.end = (end + 1) & (SERIAL_BUFSIZE - 1);
}

EXPORT char
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

EXPORT void
serial_print(const char *str)
{
	uint8_t c;

	for (c = *str; c; c = *++str) {
		while (!serial_writeable());

		serial_write(c);
	}
}
