#include <arduino/timer0.h>

/* Also need to change the interrupt references if changing pin */
#define SOFTSERIAL_RX_PIN 12

static uint8_t ev_softserial = 0;
static uint8_t softserial_pin_oldstate;
static uint8_t softserial_state = 0;
static uint8_t softserial_data;
static uint8_t softserial_bit_count = 0;
static uint8_t softserial_startbit_time;

#define SOFTSERIAL_EOF -256
static volatile struct {
  uint8_t buf[16];
  uint8_t start;
  uint8_t end;
} softserial_input;

static void
softserial_put_in_fifo(void)
{
  uint8_t end = softserial_input.end;
  softserial_input.buf[end] = softserial_data;
  softserial_input.end = (end + 1) % sizeof(softserial_input.buf);
  ev_softserial = 1;
}

static int
softserial_getchar(void)
{
  uint8_t start = softserial_input.start;
  int r;

  cli();
  if (start == softserial_input.end)
  {
    ev_softserial = 0;
    sei();
    return SOFTSERIAL_EOF;
  }
  sei();

  r = (char)softserial_input.buf[start];
  softserial_input.start = (start + 1) % sizeof(softserial_input.buf);

  return r;
}

pin_8to13_interrupt()
{
  uint8_t state;
  uint8_t current, delta;
  state = pin_is_high(SOFTSERIAL_RX_PIN);
  if (state == softserial_pin_oldstate)
    return;                                     /* Not for us... */
  softserial_pin_oldstate = state;

  switch (softserial_state) {
  case 0:  /* Waiting for start bit. */
  start_bit:
    if (state)
      return;
    softserial_startbit_time = timer0_count();
    /* Trigger a timeout interrupt in 11 bits -> 11/9600/16usec -> 72 */
    timer0_compare_a_set(softserial_startbit_time + 72);
    timer0_flags_clear();
    timer0_interrupt_a_enable();
    softserial_bit_count = 0;
    softserial_data = 0;
    softserial_state = 1;
    break;

  case 1:  /* Collecting bits */
    current = timer0_count();
    delta = current - softserial_startbit_time;
    while (softserial_bit_count < 8)
    {
      // delta * 154 <= 154 * 16e6 Hz / 256 clocks/timertick / 9600 baud * (count+1.5)
      if ((uint16_t)delta * 154 <= (uint16_t)softserial_bit_count*1000 + 1500)
        break;
      /*
        Add any bits since last; note that those bits had the reverse state of
        what we currently have.
      */
      if (!state)
        softserial_data |= (1 << softserial_bit_count);
      ++softserial_bit_count;
    }
    if (softserial_bit_count >= 8)
    {
      timer0_interrupt_a_disable();
      softserial_state = 0;
      softserial_put_in_fifo();
      goto start_bit;
    }
    break;
  }
}

timer0_interrupt_a()
{
  /*
    Handle the timeout we get if the last bit(s) are zero (so we do not get a
    transition on the stop bit) and there is no immediately following new
    start bit.
  */
  timer0_interrupt_a_disable();
  while (softserial_bit_count < 8)
    softserial_data |= (1 << softserial_bit_count++);
  softserial_state = 0;
  softserial_bit_count = 8;
  softserial_put_in_fifo();
}

static void
softserial_init(void)
{
  timer0_clock_off();
  timer0_interrupt_a_disable();
  timer0_mode_normal();
  timer0_clock_d256();  /* 16MHz / 256 -> 16usec / tick. */

  pin_mode_input(SOFTSERIAL_RX_PIN);
  softserial_pin_oldstate = pin_is_high(SOFTSERIAL_RX_PIN);
  pin12_interrupt_mask();
  pin_8to13_interrupt_enable();
}
