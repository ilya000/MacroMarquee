// Change this to be at least as long as your pixel string (too long will work fine, just be a little slower)

#define PIXELS 96*4  // Number of pixels in the string. I am using 4 meters of 96LED/M

// These values depend on which pins your 8 strings are connected to and what board you are using 
// More info on how to find these at http://www.arduino.cc/en/Reference/PortManipulation

// PORTD controls Digital Pins 0-7 on the Uno

// You'll need to look up the port/bit combination for other boards. 

// Note that you could also include the DigitalWriteFast header file to not need to to this lookup.

#define PIXEL_PORT  PORTD  // Port of the pin the pixels are connected to
#define PIXEL_DDR   DDRD   // Port of the pin the pixels are connected to

#define PIXEL_BITMASK 0b11111110  // If you do not want to use all 8 pins, you can mask off the ones you don't want
                                  // Note that these will still get 0 written to them when we send pixels
                                  // TODO: If we have time, we could even add a variable that will and/or into the bits before writing to the port to support any combination of bits/values

static const uint8_t onBits=0xfe;        // Bit pattern to write to port to turn on all pins connected to LED strips. 
                                  

// These are the timing constraints taken mostly from the WS2812 datasheets 
// These are chosen to be conservative and avoid problems rather than for maximum throughput 

#define T1H  900    // Width of a 1 bit in ns
#define T1L  600    // Width of a 1 bit in ns

#define T0H  400    // Width of a 0 bit in ns
#define T0L  900    // Width of a 0 bit in ns

#define RES 50000   // Width of the low gap between bits to cause a frame to latch

// Here are some convience defines for using nanoseconds specs to generate actual CPU delays

#define NS_PER_SEC (1000000000L)          // Note that this has to be SIGNED since we want to be able to check for negative values of derivatives

#define CYCLES_PER_SEC (F_CPU)

#define NS_PER_CYCLE ( NS_PER_SEC / CYCLES_PER_SEC )

#define NS_TO_CYCLES(n) ( (n) / NS_PER_CYCLE )

// Actually send the next set of 8 bits to the 8 pins.
// We must to drop to asm to enusre that the complier does
// not reorder things and make it so the delay happens in the wrong place.

// OnBits is the mask of which bits are being used (PIXEL_BITMASK). We pass it on so that we
// do not turn on unused pins becuae this would enable the pullup. Also, hopefully passing this
// will cause the compiler to allocate a Register for it and avoid a reload every pass.

// TODO: We could actually compute the next color byte translation while the inital bit phase is bring transmitted to do some pipelining....

// Send a full 8 bits down all the pins, represening a single color of 1 pixel
// We walk though the 8 bits in colorbyte one at a time. If the bit is 1 then we send the 8 bits of row out. Otherwise we send 0. 
// We send onBits at the first phase of the signal generation. We could just send 0xff, but that mught enable pull-ups on pins that we are not using. 

/// Unforntunately we have to drop to ASM for this so we can interleave the computaions durring the delays, otherwise things get too slow.

static inline void sendBitx8(  const uint8_t row , const uint8_t colorbyte , const uint8_t onBits ) {  
              
    asm volatile (


      "L_%=: \n\r"  
      
      "out %[port], %[onBits] \n\t"                // (1 cycles) - send T0H high (all bits high)
      
      "mov r0, %[bitwalker] \n\t"                  // (1 cycles) 
      "and r0, %[colorbyte] \n\t"                  // (1 cycles) - is the current bit in the color byte set?
      "mov r0, %[row]       \n\t"                  // (1 cycles) - get possible output byte ready (does not update Z)
      "brne ON_%= \n\t"                            // (1 cycles) - if zero after the and, then send full zero row
      "mov r0,r1  \n\r"                            // (1 cycles) - bit in colorbyte was zero, so send all 0's. 
      "ON_%=: \n\r"                                //              Note that if we land here becuase of brne, it takes 2 cycles, but it still takes 2 if the brne fell though to the mov
      
      // No extra delay here since the above calculation takes 7 cycles, using up the T0H of 350ns (https://www.google.com/webhp?sourceid=chrome-instant&ion=1&espv=2&ie=UTF-8#q=(350ns)%2F(1%2F(16mhz)))
            
      "out %[port], r0 \n\t"                       // (cycles 1) - set the output bits to [row] or 0x00 based on the bit in colorbyte. This is phase for T0H-T1H

      // We get T1H-T0H here, which is 350ns (6 cycles at 16mhz)

      "ror %[bitwalker] \n\t"                      // (1 cycles) - get ready for next pass. On last pass, the bit will end up in C flag

      "nop \n\t nop \n\t nop \n\t "                // (3 cycles) - this is phase #2 of the signal, the actual data values
            
      "out %[port],__zero_reg__  \n\t"             // (1 cycles) last step - T1L all bits low
      
      "brcs DONE_%= \n\t"                          // (1 cycles) Exit if carry bit is set as a result of us walking all 8 bits. We assume that the process around us will tak long enough to cover the phase 3 delay
            
      "jmp L_%= \n\t"                              // (3 cycles) 
            
      "DONE_%=: \n\t"

      // Don't need an explicit delay here since the overhead that follows will always be long enough
    
      ::
      [port]    "I" (_SFR_IO_ADDR(PIXEL_PORT)),
      [row]   "d" (row),
      [onBits]   "d" (onBits),
      [colorbyte]   "d" (colorbyte ),     // Phase 2 of the signal where the actual data bits show up.                
      [bitwalker] "r" (0x80)                      // Alocate a register to hold a bit that we will walk down though the color byte

    );
                                  

    // Note that the inter-bit gap can be as long as you want as long as it doesn't exceed the 5us reset timeout (which is A long time)
    // Here I have been generous and not tried to squeeze the gap tight but instead erred on the side of lots of extra time.
    // This has thenice side effect of avoid glitches on very long strings becuase 
    
} 


// Just wait long enough without sending any bots to cause the pixels to latch and display the last sent frame

void show() {
  delayMicroseconds( (RES / 1000UL) + 1);       // Round up since the delay must be _at_least_ this long (too short might not work, too long not a problem)
}


static inline void sendRowRGB( uint8_t row ,  uint8_t r,  uint8_t g,  uint8_t b ) {

  sendBitx8( row , g , onBits);    // WS2812 takes colors in GRB order
  sendBitx8( row , r , onBits);    // WS2812 takes colors in GRB order
  sendBitx8( row , b , onBits);    // WS2812 takes colors in GRB order
  
}

static inline void clear() {

  cli();
  for( unsigned int i=0; i< PIXELS; i++ ) {

    sendRowRGB( 0 , 0 , 0 , 0 );
  }
  sei();
  show(); 
}

// This nice 5x7 font from here...
// http://sunge.awardspace.com/glcd-sd/node4.html


// Font details:
// 1) Each char is fixed 5x7 pixels. 
// 2) Each byte is one column.
// 3) Columns are left to right order, leftmost byte is leftmost column of pixels.
// 4) Each column is 8 bits high.
// 5) Bit #7 is top line of char, Bit #1 is bottom.
// 6) Bit #0 is always 0, becuase this pin is used as serial input and setting to 1 would enable the pull-up.

// defines ascii characters 0x20-0x7F (32-127)
// PROGMEM after variable name as per https://www.arduino.cc/en/Reference/PROGMEM

#define FONT_WIDTH 5      
#define INTERCHAR_SPACE 1
#define ASCII_OFFSET 0x20    // ASSCI code of 1st char in font array

const uint8_t Font5x7[] PROGMEM = {
0x00,0x00,0x00,0x00,0x00,//  
0x00,0x00,0xfa,0x00,0x00,// !
0x00,0xe0,0x00,0xe0,0x00,// "
0x28,0xfe,0x28,0xfe,0x28,// #
0x24,0x54,0xfe,0x54,0x48,// $
0xc4,0xc8,0x10,0x26,0x46,// %
0x6c,0x92,0xaa,0x44,0x0a,// &
0x00,0xa0,0xc0,0x00,0x00,// '
0x00,0x38,0x44,0x82,0x00,// (
0x00,0x82,0x44,0x38,0x00,// )
0x10,0x54,0x38,0x54,0x10,// *
0x10,0x10,0x7c,0x10,0x10,// +
0x00,0x0a,0x0c,0x00,0x00,// ,
0x10,0x10,0x10,0x10,0x10,// -
0x00,0x06,0x06,0x00,0x00,// .
0x04,0x08,0x10,0x20,0x40,// /
0x7c,0x8a,0x92,0xa2,0x7c,// 0
0x00,0x42,0xfe,0x02,0x00,// 1
0x42,0x86,0x8a,0x92,0x62,// 2
0x84,0x82,0xa2,0xd2,0x8c,// 3
0x18,0x28,0x48,0xfe,0x08,// 4
0xe4,0xa2,0xa2,0xa2,0x9c,// 5
0x3c,0x52,0x92,0x92,0x0c,// 6
0x80,0x8e,0x90,0xa0,0xc0,// 7
0x6c,0x92,0x92,0x92,0x6c,// 8
0x60,0x92,0x92,0x94,0x78,// 9
0x00,0x6c,0x6c,0x00,0x00,// :
0x00,0x6a,0x6c,0x00,0x00,// ;
0x00,0x10,0x28,0x44,0x82,// <
0x28,0x28,0x28,0x28,0x28,// =
0x82,0x44,0x28,0x10,0x00,// >
0x40,0x80,0x8a,0x90,0x60,// ?
0x4c,0x92,0x9e,0x82,0x7c,// @
0x7e,0x88,0x88,0x88,0x7e,// A
0xfe,0x92,0x92,0x92,0x6c,// B
0x7c,0x82,0x82,0x82,0x44,// C
0xfe,0x82,0x82,0x44,0x38,// D
0xfe,0x92,0x92,0x92,0x82,// E
0xfe,0x90,0x90,0x80,0x80,// F
0x7c,0x82,0x82,0x8a,0x4c,// G
0xfe,0x10,0x10,0x10,0xfe,// H
0x00,0x82,0xfe,0x82,0x00,// I
0x04,0x02,0x82,0xfc,0x80,// J
0xfe,0x10,0x28,0x44,0x82,// K
0xfe,0x02,0x02,0x02,0x02,// L
0xfe,0x40,0x20,0x40,0xfe,// M
0xfe,0x20,0x10,0x08,0xfe,// N
0x7c,0x82,0x82,0x82,0x7c,// O
0xfe,0x90,0x90,0x90,0x60,// P
0x7c,0x82,0x8a,0x84,0x7a,// Q
0xfe,0x90,0x98,0x94,0x62,// R
0x62,0x92,0x92,0x92,0x8c,// S
0x80,0x80,0xfe,0x80,0x80,// T
0xfc,0x02,0x02,0x02,0xfc,// U
0xf8,0x04,0x02,0x04,0xf8,// V
0xfe,0x04,0x18,0x04,0xfe,// W
0xc6,0x28,0x10,0x28,0xc6,// X
0xc0,0x20,0x1e,0x20,0xc0,// Y
0x86,0x8a,0x92,0xa2,0xc2,// Z
0x00,0x00,0xfe,0x82,0x82,// [
0x40,0x20,0x10,0x08,0x04,// (backslash)
0x82,0x82,0xfe,0x00,0x00,// ]
0x20,0x40,0x80,0x40,0x20,// ^
0x02,0x02,0x02,0x02,0x02,// _
0x00,0x80,0x40,0x20,0x00,// `
0x04,0x2a,0x2a,0x2a,0x1e,// a
0xfe,0x12,0x22,0x22,0x1c,// b
0x1c,0x22,0x22,0x22,0x04,// c
0x1c,0x22,0x22,0x12,0xfe,// d
0x1c,0x2a,0x2a,0x2a,0x18,// e
0x10,0x7e,0x90,0x80,0x40,// f
0x10,0x28,0x2a,0x2a,0x3c,// g
0xfe,0x10,0x20,0x20,0x1e,// h
0x00,0x22,0xbe,0x02,0x00,// i
0x04,0x02,0x22,0xbc,0x00,// j
0x00,0xfe,0x08,0x14,0x22,// k
0x00,0x82,0xfe,0x02,0x00,// l
0x3e,0x20,0x18,0x20,0x1e,// m
0x3e,0x10,0x20,0x20,0x1e,// n
0x1c,0x22,0x22,0x22,0x1c,// o
0x3e,0x28,0x28,0x28,0x10,// p
0x10,0x28,0x28,0x18,0x3e,// q
0x3e,0x10,0x20,0x20,0x10,// r
0x12,0x2a,0x2a,0x2a,0x04,// s
0x20,0xfc,0x22,0x02,0x04,// t
0x3c,0x02,0x02,0x04,0x3e,// u
0x38,0x04,0x02,0x04,0x38,// v
0x3c,0x02,0x0c,0x02,0x3c,// w
0x22,0x14,0x08,0x14,0x22,// x
0x30,0x0a,0x0a,0x0a,0x3c,// y
0x22,0x26,0x2a,0x32,0x22,// z
0x00,0x10,0x6c,0x82,0x00,// {
0x00,0x00,0xfe,0x00,0x00,// |
0x00,0x82,0x6c,0x10,0x00,// }
0x10,0x10,0x54,0x38,0x10,// ~
0x10,0x38,0x54,0x10,0x10,// 
};

// Send the pixels to form the specified char, not including intercase space
// skip is the number of pixels to skip at the begining to enable sub-char smooth scrolling

// TODO: Subtract the offset from the char before starting the send sequence to save time if nessisary
// TODO: Also could pad the begining of the font table to aovid the offset subtraction at the cost of 20*8 bytes of progmem

static inline void sendChar( uint8_t c ,  uint8_t skip , uint8_t r,  uint8_t g,  uint8_t b ) {

  const uint8_t *charbase = Font5x7 + (( c -' ')* FONT_WIDTH ) ; 

  uint8_t col=FONT_WIDTH; 

  while (skip--) {
      charbase++;
      col--;    
  }
  
  while (col--) {
      sendRowRGB( pgm_read_byte_near( charbase++ ) , r , g , b );
  }    

  // TODO: FLexible interchar spacing

  sendRowRGB( 0 , r , g , b );    // Interchar space
  
}


// Show the passed string. The last letter of the string will be in the rightmost pixels of the display.
// Skip is how many cols of the 1st char to skip for smooth scrolling

static inline void sendString( const char *s , uint8_t skip ,  const uint8_t r,  const uint8_t g,  const uint8_t b ) {

  unsigned int l=PIXELS/(FONT_WIDTH+INTERCHAR_SPACE); 

  sendChar( *s , skip ,  r , g , b );   // First char is special case becuase it can be stepped for smooth scrolling
  
  while ( *(++s) && l--) {

    sendChar( *s , 0,  r , g , b );

  }
}

#define ALTFONT_WIDTH 8

const uint8_t altfont[] PROGMEM = {
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,//  
  0x06,0x06,0x30,0x30,0x60,0xc0,0xc0,0x00,// !
  0xe0,0xe0,0x00,0xe0,0xe0,0x00,0x00,0x00,// "
  0x28,0xfe,0xfe,0x28,0xfe,0xfe,0x28,0x00,// #
  0xf6,0xf6,0xd6,0xd6,0xd6,0xde,0xde,0x00,// $
  0xc6,0xce,0x1c,0x38,0x70,0xe6,0xc6,0x00,// %
  0xfe,0xfe,0xd6,0xc6,0x16,0x1e,0x1e,0x00,// &
  0xe0,0xe0,0x00,0x00,0x00,0x00,0x00,0x00,// '
  0xfe,0xfe,0x00,0x00,0x00,0x00,0x00,0x00,// (
  0x00,0xfe,0xfe,0x00,0x00,0x00,0x00,0x00,// )
  0x6c,0x10,0xfe,0xfe,0xfe,0x10,0x6c,0x00,// *
  0x10,0x10,0x7c,0x10,0x10,0x00,0x00,0x00,// +
  0x06,0x06,0x00,0x00,0x00,0x00,0x00,0x00,// ,
  0x10,0x10,0x10,0x10,0x10,0x00,0x00,0x00,// -
  0x06,0x06,0x00,0x00,0x00,0x00,0x00,0x00,// .
  0x0e,0x38,0xe0,0x00,0x00,0x00,0x00,0x00,// /
  0xfe,0xfe,0xc6,0xc6,0xc6,0xfe,0xfe,0x00,// 0
  0x06,0x66,0x66,0xfe,0xfe,0x06,0x06,0x00,// 1
  0xde,0xde,0xd6,0xd6,0xd6,0xf6,0xf6,0x00,// 2
  0xc6,0xc6,0xd6,0xd6,0xd6,0xfe,0xfe,0x00,// 3
  0xf8,0xf8,0x18,0x18,0x18,0x7e,0x7e,0x00,// 4
  0xf6,0xf6,0xd6,0xd6,0xd6,0xde,0xde,0x00,// 5
  0xfe,0xfe,0x36,0x36,0x36,0x3e,0x3e,0x00,// 6
  0xc2,0xc6,0xce,0xdc,0xf8,0xf0,0xe0,0x00,// 7
  0xfe,0xfe,0xd6,0xd6,0xd6,0xfe,0xfe,0x00,// 8
  0xf8,0xf8,0xd8,0xd8,0xd8,0xfe,0xfe,0x00,// 9
  0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00,// :
  0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00,// ;
  0x10,0x28,0x44,0x44,0x00,0x00,0x00,0x00,// <
  0x28,0x28,0x28,0x28,0x28,0x00,0x00,0x00,// =
  0x44,0x44,0x28,0x10,0x00,0x00,0x00,0x00,// >
  0xc0,0xc0,0xda,0xda,0xd0,0xf0,0xf0,0x00,// ?
  0xfe,0xfe,0xc6,0xf6,0xd6,0xf6,0xf6,0x00,// @
  0xfe,0xfe,0xd8,0xd8,0xd8,0xfe,0xfe,0x00,// A
  0xfe,0xfe,0xd6,0xd6,0xf6,0x7e,0x3e,0x00,// B
  0xfe,0xfe,0xc6,0xc6,0xc6,0xc6,0xc6,0x00,// C
  0xfe,0xfe,0xc6,0xc6,0xe6,0x7e,0x3e,0x00,// D
  0xfe,0xfe,0xd6,0xd6,0xd6,0xd6,0xd6,0x00,// E
  0xfe,0xfe,0xd0,0xd0,0xd0,0xc0,0xc0,0x00,// F
  0xfe,0xfe,0xc6,0xc6,0xd6,0xde,0xde,0x00,// G
  0xfe,0xfe,0x18,0x18,0x18,0xfe,0xfe,0x00,// H
  0xc6,0xc6,0xfe,0xfe,0xc6,0xc6,0xc6,0x00,// I
  0x06,0x06,0x06,0x06,0x06,0xfe,0xfc,0x00,// J
  0xfe,0xfe,0x18,0x18,0x78,0xfe,0x9e,0x00,// K
  0xfe,0xfe,0x06,0x06,0x06,0x06,0x06,0x00,// L
  0xfe,0xfe,0xc0,0x60,0xc0,0xfe,0xfe,0x00,// M
  0xfe,0xfe,0x70,0x38,0x1c,0xfe,0xfe,0x00,// N
  0xfe,0xfe,0xc6,0xc6,0xc6,0xfe,0xfe,0x00,// O
  0xfe,0xfe,0xd8,0xd8,0xd8,0xf8,0xf8,0x00,// P
  0xfe,0xfe,0xc6,0xce,0xce,0xfe,0xfe,0x00,// Q
  0xfe,0xfe,0xd8,0xdc,0xde,0xfe,0xfa,0x00,// R
  0xf6,0xf6,0xd6,0xd6,0xd6,0xde,0xde,0x00,// S
  0xc0,0xc0,0xfe,0xfe,0xc0,0xc0,0xc0,0x00,// T
  0xfe,0xfe,0x06,0x06,0x06,0xfe,0xfe,0x00,// U
  0xf8,0xfc,0x0e,0x06,0x0e,0xfc,0xf8,0x00,// V
  0xfc,0xfe,0x06,0x0c,0x06,0xfe,0xfc,0x00,// W
  0xee,0xfe,0x38,0x10,0x38,0xfe,0xee,0x00,// X
  0xe0,0xf0,0x3e,0x1e,0x3e,0xf0,0xe0,0x00,// Y
  0xce,0xde,0xd6,0xd6,0xd6,0xf6,0xe6,0x00,// Z
  0xfe,0xfe,0x00,0x00,0x00,0x00,0x00,0x00,// [
  0xe0,0x38,0x0e,0x00,0x00,0x00,0x00,0x00,// \
  0x00,0xfe,0xfe,0x00,0x00,0x00,0x00,0x00,// ]
  0x00,0xfe,0x02,0xfe,0x00,0x00,0x00,0x00,// ^
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,// _
  0x00,0xfe,0x02,0xfe,0x00,0x00,0x00,0x00,// `
  0xfe,0xfe,0xd8,0xd8,0xd8,0xfe,0xfe,0x00,// a
  0xfe,0xfe,0xd6,0xd6,0xf6,0x7e,0x3e,0x00,// b
  0xfe,0xfe,0xc6,0xc6,0xc6,0xc6,0xc6,0x00,// c
  0xfe,0xfe,0xc6,0xc6,0xe6,0x7e,0x3e,0x00,// d
  0xfe,0xfe,0xd6,0xd6,0xd6,0xd6,0xd6,0x00,// e
  0xfe,0xfe,0xd0,0xd0,0xd0,0xc0,0xc0,0x00,// f
  0xfe,0xfe,0xc6,0xc6,0xd6,0xde,0xde,0x00,// g
  0xfe,0xfe,0x18,0x18,0x18,0xfe,0xfe,0x00,// h
  0xc6,0xc6,0xfe,0xfe,0xc6,0xc6,0xc6,0x00,// i
  0x06,0x06,0x06,0x06,0x06,0xfe,0xfc,0x00,// j
  0xfe,0xfe,0x18,0x18,0x78,0xfe,0x9e,0x00,// k
  0xfe,0xfe,0x06,0x06,0x06,0x06,0x06,0x00,// l
  0xfe,0xfe,0xc0,0x60,0xc0,0xfe,0xfe,0x00,// m
  0xfe,0xfe,0x70,0x38,0x1c,0xfe,0xfe,0x00,// n
  0xfe,0xfe,0xc6,0xc6,0xc6,0xfe,0xfe,0x00,// o
  0xfe,0xfe,0xd8,0xd8,0xd8,0xf8,0xf8,0x00,// p
  0xfe,0xfe,0xc6,0xce,0xce,0xfe,0xfe,0x00,// q
  0xfe,0xfe,0xd8,0xdc,0xde,0xfe,0xfa,0x00,// r
  0xf6,0xf6,0xd6,0xd6,0xd6,0xde,0xde,0x00,// s
  0xc0,0xc0,0xfe,0xfe,0xc0,0xc0,0xc0,0x00,// t
  0xfe,0xfe,0x06,0x06,0x06,0xfe,0xfe,0x00,// u
  0xf8,0xfc,0x0e,0x06,0x0e,0xfc,0xf8,0x00,// v
  0xfc,0xfe,0x06,0x0c,0x06,0xfe,0xfc,0x00,// w
  0xee,0xfe,0x38,0x10,0x38,0xfe,0xee,0x00,// x
  0xe0,0xf0,0x3e,0x1e,0x3e,0xf0,0xe0,0x00,// y
  0xce,0xde,0xd6,0xd6,0xd6,0xf6,0xe6,0x00,// z
  0x38,0xfe,0xfe,0x00,0x00,0x00,0x00,0x00,// {
  0xfe,0x00,0x00,0x00,0x00,0x00,0x00,0x00,// |
  0x00,0xfe,0xfe,0x38,0x00,0x00,0x00,0x00,// }
  0x00,0xfe,0x02,0xfe,0x00,0x00,0x00,0x00,// ~
};

// Special all-your-base font 6x7

static uint8_t altbright =0; 

static inline void sendCharAlt( uint8_t c ) {

  const uint8_t *charbase = altfont + (( c -' ')* ALTFONT_WIDTH) ; 

  uint8_t col=ALTFONT_WIDTH; 
  
  while (col--) {

      sendRowRGB(  pgm_read_byte_near( charbase++ ) , altbright , 0 , 0x80  );
   
      altbright+=10;
  }

  sendRowRGB( 0 ,0, 0 , 0 );
  altbright+=10;

}

// Show the passed string. The last letter of the string will be in the rightmost pixels of the display.
// Skip is how many cols of the 1st char to skip for smooth scrolling

static inline void sendStringAlt( const char *s  ) {

  unsigned int l=PIXELS/(ALTFONT_WIDTH+INTERCHAR_SPACE); 

  while ( l--) {

    char c;  

    c =   *s++;

    if (!c) break;
    
    sendCharAlt( c  );

  }
}


/*

  The following three functions are the public API:
  
  ledSetup() - set up the pin that is connected to the string. Call once at the begining of the program.  
  sendPixel( r g , b ) - send a single pixel to the string. Call this once for each pixel in a frame.
  show() - show the recently sent pixel on the LEDs . Call once per frame. 
  
*/


// Set the specified pins up as digital out

void ledsetup() {

  PIXEL_DDR = 0xff; // TODO: FIX PIXEL_BITMASK;    // Set all used pins to output
  
  
}

/*

  That is the whole API. What follows are some demo functions rewriten from the AdaFruit strandtest code...
  
  https://github.com/adafruit/Adafruit_NeoPixel/blob/master/examples/strandtest/strandtest.ino
  
  Note that we always turn off interrupts while we are sending pixels becuase an interupt
  could happen just when we were in the middle of somehting time sensitive.
  
  If we wanted to minimize the time interrupts were off, we could instead 
  could get away with only turning off interrupts just for the very brief moment 
  when we are actually sending a 0 bit (~1us), as long as we were sure that the total time 
  taken by any interrupts + the time in our pixel generation code never exceeded the reset time (5us).
  
*/


void setup() {
    
  ledsetup();  
  DDRB=0x01;
  delay(100);
  
}


// https://learn.adafruit.com/led-tricks-gamma-correction/the-quick-fix

const uint8_t PROGMEM gamma[] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,
    1,  1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,  2,  2,  2,  2,
    2,  3,  3,  3,  3,  3,  3,  3,  4,  4,  4,  4,  4,  5,  5,  5,
    5,  6,  6,  6,  6,  7,  7,  7,  7,  8,  8,  8,  9,  9,  9, 10,
   10, 10, 11, 11, 11, 12, 12, 13, 13, 13, 14, 14, 15, 15, 16, 16,
   17, 17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22, 23, 24, 24, 25,
   25, 26, 27, 27, 28, 29, 29, 30, 31, 32, 32, 33, 34, 35, 35, 36,
   37, 38, 39, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 50,
   51, 52, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 66, 67, 68,
   69, 70, 72, 73, 74, 75, 77, 78, 79, 81, 82, 83, 85, 86, 87, 89,
   90, 92, 93, 95, 96, 98, 99,101,102,104,105,107,109,110,112,114,
  115,117,119,120,122,124,126,127,129,131,133,135,137,138,140,142,
  144,146,148,150,152,154,156,158,160,162,164,167,169,171,173,175,
  177,180,182,184,186,189,191,193,196,198,200,203,205,208,210,213,
  215,218,220,223,225,228,231,233,236,239,241,244,247,249,252,255 };

// Map 0-255 visual brightness to 0-255 LED brightness 
#define GAMMA(x) (pgm_read_byte(&gamma[x]))


void showcountdown() {

  // Start sequence.....

  const char *countdownstr = "      TOTAL WORLD DOMINATION BEGINS IN ";

  unsigned int count = 600; 

  clear();
  while (count>0) {

    count--;

    uint8_t digit1 = count/100;
    uint8_t digit2 = (count - (digit1*100)) / 10;
    uint8_t digit3 = (count - (digit1*100) - (digit2*10));

    uint8_t char1 = digit1 + '0';
    uint8_t char2 = digit2 + '0';
    uint8_t char3 = digit3 + '0';
    
    uint8_t brightness = GAMMA( ((count % 100) * 256)  / 100 );

    cli();
    sendString( countdownstr , 1 , brightness , brightness , brightness );      
 
    sendRowRGB( 0x00 , 0 , 0 , 0xff );
 
  //  sendChar( '0' , 0 , 0x80, 0 , 0 );
    
    sendChar( char1 , 0 , 0x80, 0 , 0 );
    sendChar( '.'   , 0 , 0x80, 0 , 0  );
    sendChar( char2 , 0 , 0x80, 0 , 0  );
    sendChar( char3 , 0 , 0x80, 0 , 0 );
    
    sei();
    show();
  }

  count = 100;

  while (count>0) {

    count--;

    
    uint8_t brightness = GAMMA( ((count % 100) * 256)  / 100 );

    cli();
    sendString( countdownstr , 1 , brightness , brightness , brightness );      

    sendRowRGB( 0x00 , 0 , 0 , 0xff );   // We need to quickly send a blank byte just to keep from missing our deadlne.
    sendChar( '0' , 0 , brightness, 0 , 0 );
    sendChar( '.' , 0 , brightness, 0 , 0 );
    sendChar( '0' , 0 , brightness, 0 , 0 );
    sendChar( '0' , 0 , brightness, 0 , 0 );
     
    
    sei();
    show();
  }
  
  
}


void showstarfield() {

  const uint8_t field = 40;       // Good size for a field, must be less than 256 so counters fit in a byte
 
  uint8_t sectors = (PIXELS / field);      // Repeating sectors makes for more stars and faster update

  for(unsigned int i=0; i<500;i++) {

    unsigned int r = random( PIXELS * 8 );   // Random slow, so grab one big number and we will break it down. 
    

    unsigned int x = r /8; 
    uint8_t y = r & 0x07;                // We use 7 rows
    uint8_t bitmask = (2<<y);           // Start at bit #1 since we enver use the bottom bit

    cli();    

      unsigned int l=x; 
    
      while (l--) {
           sendRowRGB( 0 , 0x00, 0x00, 0x00);          
      }
        
      sendRowRGB( bitmask , 0x40, 0x40, 0xff);  // Starlight blue

      l = PIXELS-x;
      
      while (l--) {
           sendRowRGB( 0 , 0x00, 0x00, 0x00);          
      }      
        
          

    sei();

   // show(); // Not needed - random is alwasy slow enough to trigger a reset
       
  }

}

static inline void sendIcon( const uint8_t *fontbase , uint8_t which, int8_t shift , uint8_t width , uint8_t r , uint8_t g , uint8_t b ) {

  const uint8_t *charbase = fontbase + (which*width);

  if (shift <0) {

        uint8_t shiftabs = -1 * shift;
  
        while (width--) {

          uint8_t row = pgm_read_byte_near( charbase++ );
    
          sendRowRGB(  row << shiftabs , r , g , b );
     
    }

  } else {


    
    while (width--) {
  
        sendRowRGB(  (pgm_read_byte_near( charbase++ ) >> shift) & onBits , r , g , b );
     
    }

  }

}


#define ENIMIES_WIDTH 12

const uint8_t enimies[] PROGMEM = {

  0x70,0xf4,0xfe,0xda,0xd8,0xf4,0xf4,0xd8,0xda,0xfe,0xf4,0x70, // Enimie 1 - open
  0x72,0xf2,0xf4,0xdc,0xd8,0xf4,0xf4,0xd8,0xdc,0xf4,0xf2,0x72, // Enimie 1 - close
  0x1c,0x30,0x7c,0xda,0x7a,0x78,0x7a,0xda,0x7c,0x30,0x1c,0x00, // Enimie 2 - open
  0xf0,0x3a,0x7c,0xd8,0x78,0x78,0x78,0xd8,0x7c,0x3a,0xf0,0x00, // Enimie 2 - closed
  0x92,0x54,0x10,0x82,0x44,0x00,0x00,0x44,0x82,0x10,0x54,0x92, // Explosion
};


void showinvaderwipe( uint8_t which , const char *pointsStr , uint8_t r , uint8_t g, uint8_t b) {

  clear();
  delay(500);

  for( uint8_t p = 0 ; p<strlen( pointsStr) ; p++ ) {

      cli();
      sendStringAlt( "                " );
      sendIcon( enimies , which , 0 , ENIMIES_WIDTH , r , g , b );
      for(uint8_t i=0; i<=p ;i++ ){        
        sendChar( *(pointsStr+i) , 0 ,r>>2 , g>>2 , b>>2 );     // Dim text slightly
      }
      sei();
      delay(100);
    
  }

  delay(1500);

  
}

void showinvaders() {
  
  showinvaderwipe(   3 ,  " = 20 POINTS" , 0x80 , 0x80 ,0x80 );
  showinvaderwipe(   1 ,  " = 10 POINTS" , 0x00 , 0xff ,0x00 );

  uint8_t acount = PIXELS/(ENIMIES_WIDTH+FONT_WIDTH);      // How many aliens do we have room for?  

  for( int8_t row = -4 ; row < 6 ; row++ ) {     // Walk down the rows

    //  Walk them 6 pixels per row 

    // ALternate direction on each row

    uint8_t s,e,d;

    if (row & 1) {

      s=1; e=8; d=1;
      
    } else {

      s=7; e=0; d=-1;
            
    }

    
    for( char p = s ; p!=e ; p +=d ) {
   
        // Now slowly move aliens
  
        // work our way though the alines moving each one to the left
        
      
          cli();
    
          // Start with margin

          uint8_t margin = p ;

          while (margin--) {
            sendRowRGB( 0 , 0x00 , 0x00 , 0x00 );
          }

          for( uint8_t l=0; l<acount ; l++ ) {

            sendIcon( enimies , p&1 , row, ENIMIES_WIDTH , 0xFF , 0xFF , 0xFF );
            sendChar( ' ' , 0 , 0x00 , 0x00 , 0x00 ) ; // No over crowding
            
          }
  
          sei();
          delay(70);
            
        }
   }
     // delay(200);            
  
}





void showallyourbase() {
  
  const char *allyourbase = "CAT: ALL YOUR BASE ARE BELONG TO US !!! " ;

   clear();
  for(unsigned int slide=10000; slide ; slide-=10 ) {
      altbright = (slide & 0xff);
      cli();      
      sendChar(' ' , 0 , 0 , 0 , 0 );
      sendStringAlt( allyourbase);
      sei();
      show();
  }
  
}

  
  const char *m = 
          
"Twas brillig, and the slithy toves "
      "Did gyre and gimble in the wabe: "
"All mimsy were the borogoves, "
      "And the mome raths outgrabe. "

"Beware the Jabberwock, my son! "
      "The jaws that bite, the claws that catch! "
"Beware the Jubjub bird, and shun "      
      "The frumious Bandersnatch! "

"He took his vorpal sword in hand; "
      "Long time the manxome foe he sought- "
"So rested he by the Tumtum tree "
      "And stood awhile in thought. "

"And, as in uffish thought he stood, "
      "The Jabberwock, with eyes of flame, "
"Came whiffling through the tulgey wood, "      
      "And burbled as it came! "

"One, two! One, two! And through and through "
      "The vorpal blade went snicker-snack! "
"He left it dead, and with its head "
      "He went galumphing back. "

"And hast thou slain the Jabberwock? "
      "Come to my arms, my beamish boy! "
"O frabjous day! Callooh! Callay! "
      "He chortled in his joy. "

"Twas brillig, and the slithy toves "
      "Did gyre and gimble in the wabe: "
"All mimsy were the borogoves, "
      "And the mome raths outgrabe."  

      ;

#define JAB_MAX_BRIGHTNESS 0xff
#define JAB_MIN_BRIGHTNESS 0x00
#define JAB_STEPS (JAB_MAX_BRIGHTNESS-JAB_MIN_BRIGHTNESS)

void showjabber() {

  uint8_t sector =0;
  uint8_t step=0;
    
  while (*m) {      

      if (step== JAB_STEPS) {
        step=0;
        sector++;
        if (sector==3) {
          sector=0;
        }
      } else {
        step++;
      }

      uint8_t rampup = JAB_MIN_BRIGHTNESS + step;
      uint8_t rampdown = JAB_MIN_BRIGHTNESS + (JAB_STEPS - step); 
      
      uint8_t r,g,b;
      
      switch( sector ) {
        case 0: 
            r=rampup;
            g=rampdown;
            b=JAB_MIN_BRIGHTNESS;
            break;
         case 1:
            r=rampdown;
            g=JAB_MIN_BRIGHTNESS;
            b=rampup;
            break;
         case 2:
            r=JAB_MIN_BRIGHTNESS;
            g=rampup;
            b=rampdown;
            break;
      
      };

      for( uint8_t step=0; step<FONT_WIDTH+INTERCHAR_SPACE  ; step++ ) {   // step though each column of the 1st char for smooth scrolling


       cli();

       sendString( m , step , r, g, b );
      
       sei();

       PORTB|=0x01;      
       delay(1);
       PORTB&=~0x01;

      }

    m++;

  }



}


void loop() {

  showcountdown();
  showallyourbase();
  showstarfield();
  showinvaders();
  showjabber();

  // TODO: Actually sample the state of the pullup on unused pins and OR it into the mask so we maintain the state.
  // Must do AFTER the cli(). 
  // TODO: Add offBits also to maintain the pullup state of unused pins. 

  
  return;  
}



