

#include <Arduino.h>
#include "ieee488_hal.h"

#ifdef ATmega4809
#ifdef __cplusplus
extern "C" {
#endif
/***** Control pin map *****/
/*
  Data pin map
  ------------
  DIO1_PIN  22 : GPIB 1  : PD0
  DIO2_PIN  23 : GPIB 2  : PD1
  DIO3_PIN  24 : GPIB 3  : PD2
  DIO4_PIN  25 : GPIB 4  : PD3
  DIO5_PIN  26 : GPIB 13 : PD4
  DIO6_PIN  27 : GPIB 14 : PD5
  DIO7_PIN  28 : GPIB 15 : PD6
  DIO8_PIN  29 : GPIB 16 : PD7

  Control pin map
  ---------------
  IFC_PIN   18 : GPIB  9 : PC4 : ieee488_ctrl_line_t 4
  NDAC_PIN  17 : GPIB  8 : PC3 : ieee488_ctrl_line_t 2 
  NRFD_PIN  16 : GPIB  7 : PC2 : ieee488_ctrl_line_t 1
  DAV_PIN   15 : GPIB  6 : PC1 : ieee488_ctrl_line_t 0
  EOI_PIN   14 : GPIB  5 : PC0 : ieee488_ctrl_line_t 7
  REN_PIN   21 : GPIB 17 : PC7 : ieee488_ctrl_line_t 6
  SRQ_PIN   19 : GPIB 10 : PC5 : ieee488_ctrl_line_t 5
  ATN_PIN   20 : GPIB 11 : PC6 : ieee488_ctrl_line_t 3
*/

// This is not a controller, so I can use all open collector lines

// Preprocessor definitions for handling the pins, because functions are too slow
#define USE_VPORTS
#ifdef USE_VPORTS
#define DAV_IS_ASSERTED() ((VPORTC.IN & 0x02) == 0)
#define NRFD_IS_ASSERTED() ((VPORTC.IN & 0x04) == 0)
#define NDAC_IS_ASSERTED() ((VPORTC.IN & 0x08) == 0)
#define ATN_IS_ASSERTED() ((VPORTC.IN & 0x40) == 0)
#define IFC_IS_ASSERTED() ((VPORTC.IN & 0x10) == 0)
#define SRQ_IS_ASSERTED() ((VPORTC.IN & 0x20) == 0)
#define REN_IS_ASSERTED() ((VPORTC.IN & 0x80) == 0)
#define EOI_IS_ASSERTED() ((VPORTC.IN & 0x01) == 0)
#define IDY_IS_ASSERTED() ((VPORTC.IN & 0x41) == 0)

// Assert: Set the pin to output. It will be driven low (see init)
#define DAV_ASSERT() { VPORTC.DIR |= 0x02; }
#define NRFD_ASSERT() { VPORTC.DIR |= 0x04; }
#define NDAC_ASSERT() { VPORTC.DIR |= 0x08; }
#define ATN_ASSERT() { VPORTC.DIR |= 0x40; }
#define IFC_ASSERT() { VPORTC.DIR |= 0x10; }
#define SRQ_ASSERT() { VPORTC.DIR |= 0x20; }
#define REN_ASSERT() { VPORTC.DIR |= 0x80; }
#define EOI_ASSERT() { VPORTC.DIR |= 0x01; }

// Release: Set the pin to input_pullup, so it is not driven low
#define DAV_RELEASE() { VPORTC.DIR &= ~0x02; }
#define NRFD_RELEASE() { VPORTC.DIR &= ~0x04; }
#define NDAC_RELEASE() { VPORTC.DIR &= ~0x08; }
#define ATN_RELEASE() { VPORTC.DIR &= ~0x40; }
#define IFC_RELEASE() { VPORTC.DIR &= ~0x10; }
#define SRQ_RELEASE() { VPORTC.DIR &= ~0x20; }
#define REN_RELEASE() { VPORTC.DIR &= ~0x80; }
#define EOI_RELEASE() { VPORTC.DIR &= ~0x01; }

#define READ_DIO() (~VPORTD.IN)

// Writing is hard, VPORT would not be fast, as I need to set a mask and then write the value, so I will use PORTD.OUT directly. 
// The speed is not critical, as the data lines are only used for talk/listen, and not for handshaking.
#define DRIVE_DIO(value, enable) { if (enable) { PORTD.OUT = ~value; PORTD.DIRSET = 0xFF; } else { PORTD.DIRCLR = 0xFF; } }

#else
#define DAV_IS_ASSERTED() ((PORTC.IN & 0x02) == 0)
#define NRFD_IS_ASSERTED() ((PORTC.IN & 0x04) == 0)
#define NDAC_IS_ASSERTED() ((PORTC.IN & 0x08) == 0)
#define ATN_IS_ASSERTED() ((PORTC.IN & 0x40) == 0)
#define IFC_IS_ASSERTED() ((PORTC.IN & 0x10) == 0)
#define SRQ_IS_ASSERTED() ((PORTC.IN & 0x20) == 0)
#define REN_IS_ASSERTED() ((PORTC.IN & 0x80) == 0)
#define EOI_IS_ASSERTED() ((PORTC.IN & 0x01) == 0)
#define IDY_IS_ASSERTED() ((PORTC.IN & 0x41) == 0)

#define DAV_ASSERT() { PORTC.DIRSET = 0x02; PORTC.OUTCLR = 0x02; }
#define NRFD_ASSERT() { PORTC.DIRSET = 0x04; PORTC.OUTCLR = 0x04; }
#define NDAC_ASSERT() { PORTC.DIRSET = 0x08; PORTC.OUTCLR = 0x08; }
#define ATN_ASSERT() { PORTC.DIRSET = 0x40; PORTC.OUTCLR = 0x40; }
#define IFC_ASSERT() { PORTC.DIRSET = 0x10; PORTC.OUTCLR = 0x10; }
#define SRQ_ASSERT() { PORTC.DIRSET = 0x20; PORTC.OUTCLR = 0x20; }
#define REN_ASSERT() { PORTC.DIRSET = 0x80; PORTC.OUTCLR = 0x80; }
#define EOI_ASSERT() { PORTC.DIRSET = 0x01; PORTC.OUTCLR = 0x01; }

#define DAV_RELEASE() { PORTC.DIRCLR = 0x02; }
#define NRFD_RELEASE() { PORTC.DIRCLR = 0x04; }
#define NDAC_RELEASE() { PORTC.DIRCLR = 0x08; }
#define ATN_RELEASE() { PORTC.DIRCLR = 0x40; }
#define IFC_RELEASE() { PORTC.DIRCLR = 0x10; }
#define SRQ_RELEASE() { PORTC.DIRCLR = 0x20; }
#define REN_RELEASE() { PORTC.DIRCLR = 0x80; }
#define EOI_RELEASE() { PORTC.DIRCLR = 0x01; }

#define READ_DIO() (~PORTD.IN)

#define DRIVE_DIO(value, enable) { if (enable) { PORTD.OUT = ~value; PORTD.DIRSET = 0xFF; } else { PORTD.DIRCLR = 0xFF; } }
#endif

#define TIME_US() (micros())

#ifdef __cplusplus
}
#endif
#endif  // ATmega4809