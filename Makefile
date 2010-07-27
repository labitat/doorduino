CC = avr-gcc
OBJCOPY = avr-objcopy
OBJDUMP = avr-objdump
AVRDUDE = avrdude
STTY = stty
SED = sed

NAME = doorduino

MCU = atmega328p
F_CPU = 16000000UL
FORMAT = ihex
PORT = /dev/ttyUSB0
BAUD_RATE = 57600
PROGRAMMER = arduino
ARDUINO_HEADERS = ..
CFLAGS = -Os -g -mmcu=$(MCU) -DF_CPU=$(F_CPU) \
         -funsigned-char -funsigned-bitfields -fpack-struct -fshort-enums \
         -Wall -Wextra -pedantic -I$(ARDUINO_HEADERS)

.PHONY: all list tty
.PRECIOUS: %.o %.elf

all: $(NAME).hex

%.o: %.c %.h
	@echo '  CC $@'
	@$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c
	@echo '  CC $@'
	@$(CC) $(CFLAGS) -c $< -o $@

door.o: door.c sha1.h sha1.c serial.c
	@echo '  CC $@'
	@$(CC) $(CFLAGS) -c $< -o $@

%.elf: %.o
	@echo '  LD $@'
	@$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

%.hex: %.elf
	@echo '  OBJCOPY $@'
	@$(OBJCOPY) -O $(FORMAT) -R .eeprom -S $< $@ && \
	  echo "  $$((0x$$($(OBJDUMP) -h $@ | \
	    $(SED) -n '6{s/^  0 \.sec1         //;s/ .*//;p}'))) bytes"

# Create extended listing file from ELF output file.
%.lss: %.elf
	@echo '  OBJDUMP > $@'
	@$(OBJDUMP) -h -S $< > $@

upload: $(NAME).hex
	@$(AVRDUDE) -vD -c$(PROGRAMMER) -b$(BAUD_RATE) -p$(MCU) -P$(PORT) -Uflash:w:$<:i

list: $(NAME).lss

tty:
	@echo '  STTY -F$(PORT) cs8 parenb -parodd cstopb raw -echo 9600'
	@$(STTY) -F$(PORT) cs8 parenb -parodd cstopb raw -echo 9600

tty2:
	@echo '  STTY -F$(PORT) cs8 -parenb -cstopb raw -echo 9600'
	@$(STTY) -F$(PORT) cs8 -parenb -cstopb raw -echo 9600

clean:
	rm -f *.o *.elf *.hex *.lss
