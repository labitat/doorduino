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

#ifndef _SERIAL_H
#define _SERIAL_H

#define serial_init(baud, mode)\
	serial_baud_##baud();\
	serial_mode_##mode();\
	serial_transmitter_enable();\
	serial_receiver_enable();\
	serial_interrupt_rx_enable()

#ifndef ALLINONE
uint8_t serial_available();
char serial_getchar();
void serial_print(const char *str);
void serial_hexdump(const void *data, size_t len);
#endif

#endif
