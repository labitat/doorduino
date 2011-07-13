CC      = avr-gcc
OBJCOPY = avr-objcopy
OBJDUMP = avr-objdump
AVRDUDE = avrdude
STTY    = stty
SED     = sed
CAT     = cat
ARDUINO_HEADERS = .

NAME = doorduino

FORMAT     = ihex
PROGRAMMER = arduino
CFLAGS     = -O2 -g -mmcu=$(MCU) -DF_CPU=$(F_CPU) -I$(ARDUINO_HEADERS) \
             -funsigned-char -fpack-struct -fshort-enums \
             -Wall -Wextra -Wno-variadic-macros -pedantic

## Duemilanove
MCU        = atmega328p
F_CPU      = 16000000UL
PORT       = /dev/ttyUSB0
PROG_BAUD  = 57600

## Uno
#MCU        = atmega328p
#F_CPU      = 16000000UL
#PORT       = /dev/ttyACM0
#PROG_BAUD  = 115200

MODE_RAW = raw -echo -hup
MODE_7   = cs7
MODE_8   = cs8
MODE_N   = -parenb
MODE_E   = parenb -parodd
MODE_O   = parenb parodd
MODE_1   = -cstopb
MODE_2   = cstopb

## Change this according to your code to make the tty and cat targets work
BAUD     = 9600
MODE     = $(MODE_RAW) $(MODE_8) $(MODE_E) $(MODE_2)# 8E2

.PHONY: all list tty cat
.PRECIOUS: %.o %.elf

all: $(NAME).hex

%.o: %.c %.h
	@echo '  CC $@'
	@$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c
	@echo '  CC $@'
	@$(CC) $(CFLAGS) -c $< -o $@

doorduino.o: doorduino.c sha1.h sha1.c serial.c
	@echo '  CC $@'
	@$(CC) $(CFLAGS) -c $< -o $@

%.elf: %.o
	@echo '  LD $@'
	@$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

%.hex: %.elf
	@echo '  OBJCOPY $@'
	@$(OBJCOPY) -O $(FORMAT) -R .eeprom -S $< $@
	@echo "  $$((0x$$($(OBJDUMP) -h $@ | $(SED) -n '6{s/^  0 \.sec1         //;s/ .*//;p}'))) bytes"


# Create extended listing file from ELF output file.
%.lss: %.elf
	@echo '  OBJDUMP > $@'
	@$(OBJDUMP) -h -S $< > $@

upload: $(NAME).hex $(PORT)
	@$(AVRDUDE) -vD -c$(PROGRAMMER) -b$(PROG_BAUD) -p$(MCU) -P$(PORT) -Uflash:w:$<:i

list: $(NAME).lss

tty: $(PORT)
	@echo '  STTY -F$(PORT) $(MODE) $(BAUD)'
	@$(STTY) -F$(PORT) $(MODE) $(BAUD)

cat: $(PORT)
	@$(CAT) $(PORT)

clean:
	rm -f *.o *.elf *.hex *.lss
