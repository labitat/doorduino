#include <stdio.h>

#include <arduino/pins.h>
#include <arduino/spi.h>

#include "tools/serial.h"


/* Pinout */
#define MFRC522_NRSTPD A0
#define MFRC522_SS 10
#define MFRC522_MOSI 11
#define MFRC522_MISO 12
#define MFRC522_SCK 13


/* MFRC522 registers. */
#define REG_Command     0x01
#define REG_ComIEn      0x02
#define REG_ComIrq      0x04
#define REG_DivIrq      0x05
#define REG_Error       0x06
#define REG_FIFOData    0x09
#define REG_FIFOLevel   0x0a
#define REG_Control     0x0c
#define REG_BitFraming  0x0d
#define REG_Mode        0x11
#define REG_TxControl   0x14
#define REG_TxAuto      0x15
#define REG_CRCResult_H 0x21
#define REG_CRCResult_L 0x22
#define REG_TMode       0x2a
#define REG_TPrescaler  0x2b
#define REG_TReload_H   0x2c
#define REG_TReload_L   0x2d

/* MFRC522 commands (REG_Command) */
#define CMD_Idle        0
#define CMD_CalcCRC     3
#define CMD_Transceive 12
#define CMD_SoftReset  15


/* Select the MFRC522 active. */
static void
mfrc522_slave_select(void)
{
  pin_low(MFRC522_SS);
}


/* Deselect the MFRC522. */
static void
mfrc522_slave_deselect(void)
{
  pin_high(MFRC522_SS);
}


static uint8_t
mfrc522_read_reg(uint8_t reg)
{
  uint8_t v;

  mfrc522_slave_select();
  spi_write(0x80 | ((reg<<1) & 0x7f));
  while (!spi_interrupt_flag())
    ;
  spi_write(0);
  while (!spi_interrupt_flag())
    ;
  v = spi_read();
  mfrc522_slave_deselect();
  return v;
}


static void
mfrc522_write_reg(uint8_t reg, uint8_t val)
{
  mfrc522_slave_select();
  spi_write((reg<<1) & 0x7f);
  while (!spi_interrupt_flag())
    ;
  spi_write(val);
  while (!spi_interrupt_flag())
    ;
  mfrc522_slave_deselect();
}


static void
mfrc522_reg_set_bits(uint8_t reg, uint8_t bits)
{
  mfrc522_write_reg(reg, mfrc522_read_reg(reg) | bits);
}


static void
mfrc522_reg_clear_bits(uint8_t reg, uint8_t bits)
{
  mfrc522_write_reg(reg, mfrc522_read_reg(reg) & ~bits);
}


__attribute__((unused))
static void
get_crc(uint8_t *buf, uint8_t len, uint8_t *out_crc)
{
  uint8_t v, i;

  mfrc522_reg_clear_bits(REG_DivIrq, 0x4);
  mfrc522_reg_set_bits(REG_FIFOLevel, 0x80);

  for (i = 0; i < len; ++i)
    mfrc522_write_reg(REG_FIFOData, buf[i]);
  mfrc522_write_reg(REG_Command, CMD_CalcCRC);

  for (i = 0; i < 0xff; ++i)
  {
    v = mfrc522_read_reg(REG_DivIrq);
    if (v & 0x04)
      break;
  }

  out_crc[0] = mfrc522_read_reg(REG_CRCResult_L);
  out_crc[1] = mfrc522_read_reg(REG_CRCResult_H);
}


static uint8_t
mfrc522_trx(uint8_t *txbuf, uint8_t txbuflen,
            uint8_t *rxbuf, uint8_t rxbuflen, uint8_t *rx_bits)
{
  uint8_t v, v2, avail;
  uint16_t i;

  /* Enable interrupt requests. */
  mfrc522_write_reg(REG_ComIEn, 0xf7);
  mfrc522_reg_clear_bits(REG_ComIrq, 0x80);
  /* Flush FIFO. */
  mfrc522_reg_set_bits(REG_FIFOLevel, 0x80);

  mfrc522_write_reg(REG_Command, CMD_Idle);
  for (i = 0; i < txbuflen; ++i)
    mfrc522_write_reg(REG_FIFOData, txbuf[i]);
  mfrc522_write_reg(REG_Command, CMD_Transceive);
  /* Set the "start transmission of data" bit. */
  mfrc522_reg_set_bits(REG_BitFraming, 0x80);

  /* Wait for read complete. */
  for (i = 0; i < 2000; ++i)
  {
    v2 = mfrc522_read_reg(REG_ComIrq);
    if ((v2 & 0x31))
      break;
  }

  /* Clear the transmit-start bit. */
  mfrc522_reg_clear_bits(REG_BitFraming, 0x80);

  if (i == 2000)
    return 1;

  v = mfrc522_read_reg(REG_Error);
  /* Return error in case of BufferOvfl, CollErr, ParityErr, or ProtocolErr. */
  if (v & 0x1B)
    return 1;

  avail = mfrc522_read_reg(REG_FIFOLevel);
  /* Check number of bits in last byte. */
  v = mfrc522_read_reg(REG_Control);
  *rx_bits = (v & 7) ? (avail-1)*8 + (v & 7) : avail*8;
  if (avail == 0)
    avail = 1;
  else if (avail > rxbuflen)
    avail = rxbuflen;

  for (i = 0; i < avail; ++i)
    rxbuf[i] = mfrc522_read_reg(REG_FIFOData);

  if (v2 & 1)
    return 1;
  else
    return 0;
}


static void
mfrc522_init(void)
{
  uint8_t v;
  int i;

  /* Do a reset. */
  mfrc522_write_reg(REG_Command, CMD_SoftReset);
  /* A delay after reset seems to be required. */
  for (i = 0; i < 1000; ++i)
    asm volatile ("nop");

  /* Set Tauto flag, and set prescaler high bits to 0xd. */
  mfrc522_write_reg(REG_TMode, 0x8D);
  /* Set low bits of prescaler to 0x3e. */
  mfrc522_write_reg(REG_TPrescaler, 0x3e);

  mfrc522_write_reg(REG_TReload_L, 30);
  mfrc522_write_reg(REG_TReload_H, 0);

  mfrc522_write_reg(REG_TxAuto, 0x40);
  mfrc522_write_reg(REG_Mode, 0x3D);

  /* Enable the antenna. */
  v = mfrc522_read_reg(REG_TxControl);
  if (!(v & 0x03))
    mfrc522_reg_set_bits(REG_TxControl, 0x03);
}


/*
  Check for the presence of a card.

  If found, and if supplied buffer is sufficiently large, fill in buffer with
  a binary ID string, and return the length of it.

  Else, return 0.
*/
uint8_t
check_mfrc522(char *outbuf, uint8_t outbufsize)
{
  uint8_t buf1[4];
  uint8_t buf2[2];
  uint8_t buf3[5];
  uint8_t rx_bits;
  uint8_t err;
  uint8_t i, outlen = 0;
  // char txtbuf[32];

  if (outbufsize < 10)
    return 0;

  /* Check for card type. */
  mfrc522_write_reg(REG_BitFraming, 7);
  buf1[0] = 0x26;
  err = mfrc522_trx(buf1, 1, buf2, 2, &rx_bits);
  if (err)
    goto end;
  if (rx_bits != 16)
    goto end;

  // sprintf(txtbuf, "Typ: %02x %02x\n", buf2[0], buf2[1]);
  // serial_print(txtbuf);

  /* Read card ID number. */
  mfrc522_write_reg(REG_BitFraming, 0);
  buf1[0] = 0x93;
  buf1[1] = 0x20;
  err = mfrc522_trx(buf1, 2, buf3, 5, &rx_bits);
  if (err)
    goto end;
  /* Checksum. */
  if (buf3[0] ^ buf3[1] ^ buf3[2] ^ buf3[3] ^ buf3[4])
    goto end;

  // sprintf(txtbuf, "Num: %02x %02x %02x %02x %02x\n",
  //         buf3[0], buf3[1], buf3[2], buf3[3], buf3[4]);
  // serial_print(txtbuf);

  /* We saw a card; format a 10-byte ID string from type / serial number. */
  outbuf[outlen++] = 'M';
  outbuf[outlen++] = 'F';
  outbuf[outlen++] = 'R';
  for (i = 0; i < 2; ++i)
    outbuf[outlen++] = buf2[i];
  for (i = 0; i < 5; ++i)
    outbuf[outlen++] = buf3[i];

end:
  /* Go to halt/hibernation. */
  buf1[0] = 0x50;
  buf1[1] = 0x0;
  get_crc(buf1, 2, buf1+2);
  mfrc522_trx(buf1, 4, buf3, 5, &rx_bits);

  return outlen;
}


void
init_mfrc522(void)
{
  pin_mode_output(MFRC522_NRSTPD);
  pin_high(MFRC522_NRSTPD);
  /* Slave select pin, high (deselected) initially. */
  pin_mode_output(MFRC522_SS);
  pin_high(MFRC522_SS);
  pin_mode_output(MFRC522_MOSI);
  pin_low(MFRC522_MOSI);
  pin_mode_input(MFRC522_MISO);
  pin_mode_output(MFRC522_SCK);
  pin_low(MFRC522_SCK);

  spi_mode_master();
  spi_enable();

  mfrc522_init();
}
