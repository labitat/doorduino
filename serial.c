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

#define serial_init(baud, mode) do {\
	serial_baud_##baud();\
	serial_mode_##mode();\
	serial_transmitter_enable();\
	serial_receiver_enable();\
	serial_interrupt_rx_enable();\
	} while (0)

#ifndef SERIAL_INBUF
#define SERIAL_INBUF 128
#endif

#ifndef SERIAL_OUTBUF
#define SERIAL_OUTBUF 128
#endif

static volatile struct {
	uint8_t buf[SERIAL_INBUF];
	uint8_t start;
	uint8_t end;
} serial_input;

static volatile struct {
	uint8_t buf[SERIAL_OUTBUF];
	uint8_t start;
	uint8_t end;
} serial_output;

serial_interrupt_rx()
{
	uint8_t end = serial_input.end;

	serial_input.buf[end] = serial_read();
	serial_input.end = (end + 1) & (SERIAL_INBUF - 1);

	events |= EV_SERIAL;
}

serial_interrupt_dre()
{
	uint8_t start = serial_output.start;

	if (start == serial_output.end)
		serial_interrupt_dre_disable();
	else {
		serial_write(serial_output.buf[start]);
		serial_output.start = (start + 1) & (SERIAL_OUTBUF - 1);
	}
}

static uint8_t
serial_available(void)
{
	return serial_input.start != serial_input.end;
}

static char
serial_getchar(void)
{
	uint8_t start = serial_input.start;
	char r;

	if (start == serial_input.end)
		return '\0';

	r = (char)serial_input.buf[start];
	serial_input.start = (start + 1) & (SERIAL_INBUF - 1);

	return r;
}

static void
serial_print(const char *str)
{
	uint8_t end = serial_output.end;

	while ((serial_output.buf[end] = *str++))
		end = (end + 1) & (SERIAL_OUTBUF - 1);

	serial_output.end = end;
	serial_interrupt_dre_enable();
}

static char serial_hexdigit[] = {
	'0', '1', '2', '3', '4', '5', '6', '7',
	'8', '9', 'A', 'B', 'C', 'D', 'E', 'F'
};

static void
serial_hexdump(const void *data, size_t len)
{
	const uint8_t *p = data;
	uint8_t end = serial_output.end;

	for (; len > 0; len--, p++) {
		uint8_t c = *p;

		serial_output.buf[end] = serial_hexdigit[c >> 4];
		end = (end + 1) & (SERIAL_OUTBUF - 1);
		serial_output.buf[end] = serial_hexdigit[c & 0x0F];
		end = (end + 1) & (SERIAL_OUTBUF - 1);
	}

	serial_output.end = end;
	serial_interrupt_dre_enable();
}
