  #include <stdint.h>
#include <avr/interrupt.h>
#include <avr/io.h>

#define SHA_LITTLE_ENDIAN

// define this if you want to process more then 65535 bytes
/* #define SHA_BIG_DATA */

#include <stdint.h>
#include <string.h>

// initial values
#define init_h0  0x67452301
#define init_h1  0xEFCDAB89
#define init_h2  0x98BADCFE
#define init_h3  0x10325476
#define init_h4  0xC3D2E1F0

// bit-shift-operation
#define rol(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))

#define INIT_TIMER_COUNT 6
#define RESET_TIMER2 TCNT2 = INIT_TIMER_COUNT

#define PIN_CLK         2
#define PIN_DATA        3
#define PIN_GREEN_LED   4
#define PIN_YELLOW_LED  5
#define PIN_OPEN_LOCK   6
#define PIN_DAYMODE     7
#define PIN_STATUS_LED  19


union _message {
  unsigned char data[64];
  uint32_t w[16];
} message;

struct shastate {
  uint32_t h0,h1,h2,h3,h4;
#ifdef SHA_BIG_DATA  
  uint32_t count;
#else  
  uint16_t count;
#endif  
};

union _digest {
  unsigned char data[20];
  struct shastate state;
} shadigest;

void SHA1Init(void);
void SHA1Block(const unsigned char* data, const uint8_t len);
void SHA1Done(void);
void SHA1Once(const unsigned char* data, int len);

char clk;
char value;
unsigned char data[256];
char out[41];
char cnt;
char inc;


int int_counter = 0;
volatile int second = 0;
int incommingByte;


void setup() {
  Serial.begin(9600);
  pinMode(13, OUTPUT);
  
  pinMode(PIN_CLK, INPUT); // clk
  pinMode(PIN_DATA, INPUT); // data
  pinMode(PIN_GREEN_LED, OUTPUT); // green led lock
  pinMode(PIN_YELLOW_LED, OUTPUT); // yellow led lock
  pinMode(PIN_OPEN_LOCK, OUTPUT); // open
  pinMode(PIN_DAYMODE, OUTPUT); // stay open
  pinMode(PIN_STATUS_LED, OUTPUT);  //   yellow status
  
  EICRA = 0x03;  // INT0 rising edge on SCL
  EIMSK = 0x01;  // enable only int0
  clk = 0;
  cnt = 0;
  value = 0;
  resetData();
  second =0;
  
  TCCR2A |= (1<<CS22);
  TCCR2A &= ~((1<<CS21) | (1<<CS20));
  // Use normal mode
  TCCR2A &= ~((1<<WGM21) | (1<<WGM20));
  // Use internal clock – external clock not used in Arduino
  ASSR |= (0<<AS2);
  //Timer2 Overflow Interrupt Enable
  TIMSK2 |= (1<<TOIE2) | (0<<OCIE2A);              
  sei();
  
  digitalWrite(PIN_OPEN_LOCK, HIGH);
  digitalWrite(PIN_DAYMODE, HIGH);
  digitalWrite(PIN_GREEN_LED, HIGH);
  digitalWrite(PIN_YELLOW_LED, HIGH);

}

void loop() {
  
    if (data[cnt-1] == (unsigned) 0xB4) {
       if(cnt >= 10){
         SHA1Once(data,256);
         sprintf(out,"%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
         shadigest.data[0],shadigest.data[1],shadigest.data[2],shadigest.data[3],shadigest.data[4],shadigest.data[5],shadigest.data[6],shadigest.data[7],
         shadigest.data[8],shadigest.data[9],shadigest.data[10],shadigest.data[11],shadigest.data[12],shadigest.data[13],shadigest.data[14],shadigest.data[15],
         shadigest.data[16],shadigest.data[17],shadigest.data[18],shadigest.data[19]);
         Serial.print("HASH+");
         Serial.println(out);
       }
       resetData();
       cnt = 0;
    }
    
    if(second > 10*4)
    {
       Serial.println("ALIVE");
       second=0;
       resetData();
       cnt = 0;
    }
    
  if (Serial.available() > 0) {
        inc = Serial.read();
        if(inc == 'O') // open
        {
          digitalWrite(PIN_GREEN_LED, LOW);
          digitalWrite(PIN_OPEN_LOCK, LOW);
          delay(500);
          digitalWrite(PIN_OPEN_LOCK,HIGH);
          Serial.println("OPENAKCK");
          digitalWrite(PIN_GREEN_LED, HIGH);
        }
        else if(inc == 'D') // day
        {
          digitalWrite(PIN_GREEN_LED, LOW);
          digitalWrite(PIN_DAYMODE, LOW); // day mode
          digitalWrite(PIN_STATUS_LED, HIGH); // status on
        }
        else if(inc == 'N') // night 
        {
          digitalWrite(PIN_GREEN_LED, HIGH);
          digitalWrite(PIN_DAYMODE, HIGH); // nightmode
          digitalWrite(PIN_STATUS_LED, LOW);  // status off
        }
        else if(inc == 'R') // rejected
        {
           digitalWrite(PIN_YELLOW_LED, LOW);
           delay(200);
           digitalWrite(PIN_YELLOW_LED, HIGH);
           delay(200);
           digitalWrite(PIN_YELLOW_LED, LOW);
           delay(200);
           digitalWrite(PIN_YELLOW_LED, HIGH);
          
        }
        else if(inc == 'V') // validated
        {
           digitalWrite(PIN_GREEN_LED, LOW);
           delay(300);
           digitalWrite(PIN_GREEN_LED, HIGH);
           delay(200);
           digitalWrite(PIN_GREEN_LED, LOW);
           delay(300);
           digitalWrite(PIN_GREEN_LED, HIGH);
           delay(200);
           digitalWrite(PIN_GREEN_LED, LOW);
           delay(300);
           digitalWrite(PIN_GREEN_LED, HIGH);          
        }

  }

  delayMicroseconds(20);
}

void resetData(){
  for(unsigned int i =0; i < 256; i++)
  {
     data[i] = i;
  }
}

ISR(INT0_vect) {
  value |= digitalRead(PIN_DATA) << (7-clk);
  clk++;
  int_counter = 0;
  second = 0;
  if(clk == 8) {
     if (cnt < 255){
        data[cnt] = value;
        cnt++;
     }
     clk = 0;
     value = 0;
  }

}

// Aruino runs at 16 Mhz, so we have 1000 Overflows per second...
// 1/ ((16000000 / 64) / 256) = 1 / 1000
ISR(TIMER2_OVF_vect) {
  RESET_TIMER2;
  int_counter += 1;
  if (int_counter == 250) {
    clk = 0;
    value = 0;
    second++;
    int_counter = 0;
  } 
};


// processes one endianess-corrected block, provided in message
void SHA1(void) {
  uint8_t i;
  uint32_t a,b,c,d,e,f,k,t;
  
  a = shadigest.state.h0;
  b = shadigest.state.h1;
  c = shadigest.state.h2;
  d = shadigest.state.h3;
  e = shadigest.state.h4;  
  
  // main loop: 80 rounds
  for (i=0; i<=79; i++) {
    if (i<=19) {
      f = d ^ (b & (c ^ d));
      k = 0x5A827999;
    } else if (i<=39) {
      f = b ^ c ^ d;
      k = 0x6ED9EBA1;
    } else if (i<=59) {
      f = (b & c) | (d & (b | c));
      k = 0x8F1BBCDC;
    } else {
      f = b ^ c ^ d;
      k = 0xCA62C1D6;
    }

    // blow up to 80 dwords while in the loop, save some RAM  
    if (i>=16) {
      t = rol(message.w[(i+13)&15] ^ message.w[(i+8)&15] ^ message.w[(i+2)&15] ^ message.w[i&15], 1);
      message.w[i&15] = t;
    }
    
    t = rol(a, 5) + f + e + k + message.w[i&15];
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

void SHA1Init(void) {
  shadigest.state.h0 = init_h0;
  shadigest.state.h1 = init_h1;
  shadigest.state.h2 = init_h2;
  shadigest.state.h3 = init_h3;
  shadigest.state.h4 = init_h4;
  shadigest.state.count = 0;
}

// Hashes blocks of 64 bytes of data.
// Only the last block *must* be smaller than 64 bytes.
void SHA1Block(const unsigned char* data, const uint8_t len) {
  uint8_t i;
  
  // clear all bytes in data block that are not overwritten anyway
  for (i=len>>2;i<=15;i++) {
    message.w[i] = 0;
  }
  
#ifdef SHA_LITTLE_ENDIAN
  // swap bytes
  for (i=0;i<len;i+=4) {
    message.data[i] = data[i+3];
    message.data[i+1] = data[i+2];
    message.data[i+2] = data[i+1];
    message.data[i+3] = data[i];    
  }
#else
  memcpy(message.data, data, len);
#endif  

  // remember number of bytes processed for final block
  shadigest.state.count += len;

  if (len<64) {
    // final block: mask bytes accidentally copied by for-loop
    // and do the padding
    message.w[len >> 2] &= 0xffffffffL << (((~len & 3)*8)+8);
    message.w[len >> 2] |= 0x80L << ((~len & 3)*8);
    // there is space for a qword containing the size at the end
    // of the block
    if (len<=55) {
      message.w[15] = (uint32_t)(shadigest.state.count) * 8;
    }
  }
  
  SHA1();
  
  // was last data block, but there wasn't space for the size: 
  // process another block
  if ((len>=56) && (len<64)) {
    for (i=0; i<=14; i++) {
      message.w[i] = 0;
    }
    message.w[15] = (uint32_t)(shadigest.state.count) * 8;
    SHA1();
  }
}

// Correct the endianess if needed  
void SHA1Done(void) {
#ifdef SHA_LITTLE_ENDIAN
  uint8_t i;
  unsigned char j;
  // swap bytes
  for (i=0;i<=4;i++) {
    j = shadigest.data[4*i];
    shadigest.data[4*i] = shadigest.data[4*i+3];
    shadigest.data[4*i+3] = j;
    j = shadigest.data[4*i+1];
    shadigest.data[4*i+1] = shadigest.data[4*i+2];
    shadigest.data[4*i+2] = j;  
  }
#endif 
}

// Hashes just one arbitrarily sized chunk of data
void SHA1Once(const unsigned char* data, int len) {
  SHA1Init();
  while (len>=0) {
    SHA1Block(data, len>64?64:len);
    len -= 64;
    data += 64;
  }
  SHA1Done();
}

