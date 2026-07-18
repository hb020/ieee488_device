

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

// TODO in order to meet timing constraints:
// * Enable interrupts for ATN and EOI lines to meet timing requirements.
// * this means that I MIGHT need to set Global Interrupt Enable (bit 7) in the SREG register (SEI/CLI), but I need to check if Arduino does this automatically. If not, I will need to set it in hal_init().
// * The problem is that the AT4809 (and others) has problem handling interrupts that originate from the same port. Interrupts might get lost.
// * Only bits 6 and 2 have fully async interrupt
// * Create dedicated functions for each pin for `hal_drive_line` and `hal_read_line`
// * Move those functions and `hal_drive_dio` and `hal_read_dio` to inline functions in ieee488_hal.h
// * see https://github.com/microchip-pic-avr-examples/atmega4809-getting-started-with-gpio-mplab/blob/master/Wake_Up_On_Button_Press/Wake_Up_On_Button_Press.X/main.c
// * I might need to go through CCL for ATN ^ EOI (see https://github.com/microchip-pic-avr-examples/atmega4809-getting-started-with-ccl-studio)

// This is not a controller, so I can use all open collector lines

/** @brief Convert an IEEE488 line to the corresponding port bit.
 * @param line The IEEE488 line to convert.
 * @return The corresponding port mask for the line.
 */
inline uint8_t line_to_mask(ieee488_ctrl_line_t line) {
  switch (line)
  {
    case IEEE488_DAV:
      return 1 << 1; // PC1
    case IEEE488_NRFD:
      return 1 << 2; // PC2
    case IEEE488_NDAC:
      return 1 << 3; // PC3
    case IEEE488_ATN:
      return 1 << 6; // PC6
    case IEEE488_IFC:
      return 1 << 4; // PC4
    case IEEE488_SRQ:
      return 1 << 5; // PC5
    case IEEE488_REN:
      return 1 << 7; // PC7
    case IEEE488_EOI:
      return 1 << 0; // PC0
    default:
      return 0; // Invalid line
  } 
}

/** @brief Read the state of a command line (bit).
 * @param line The line (bit) to read. See ieee488_ctrl_line_t, 8..15.
 * @return true if the line is asserted, false otherwise.
 */
inline bool hal_read_line(ieee488_ctrl_line_t line) {
  uint8_t mask = line_to_mask(line);
  // do not change the PORTC.DIRCLR = mask as the pin may have been set as output
  return (PORTC.IN & mask) == 0; // Asserted if the pin is LOW
}

/** @brief Drive a command line (bit) to asserted or released.
 * @param line The line (bit) to drive. See ieee488_ctrl_line_t, 8..15.
 * @param asserted true to assert the line, false to release it.
 */
inline void hal_drive_line(ieee488_ctrl_line_t line, bool asserted) {
  uint8_t mask = line_to_mask(line);
  if (asserted) {
    PORTC.DIRSET = mask; // Drive LOW (asserted) via open-collector behavior.
    PORTC.OUTCLR = mask;
  } else {
    PORTC.DIRCLR = mask; // Release line (Hi-Z), external/pull-up drives HIGH.
  }
}

/** @brief Read the state of the digital I/O lines (DIO1-DIO8).
 * @return The logical state of the DIO lines, bit 0 = DIO1.
 */
inline uint8_t hal_read_dio(void) {
  return ~PORTD.IN;
}

/** @brief Drive the digital I/O lines (DIO1-DIO8).
 * @param value The logical state to drive onto the DIO lines, bit 0 = DIO1.
 * @param enable true to drive the lines, false to release them.
 */
inline void hal_drive_dio(uint8_t value, bool enable) {
  if (enable) {
    PORTD.OUT = ~value;    
    PORTD.DIRSET = 0b11111111;
  } else {
    PORTD.DIRCLR = 0b11111111;
  }
}

/** @brief  Time in microseconds since startup.
 * @return Time in microseconds since startup.
 */
inline uint32_t hal_time_us(void) {
    return micros();
}

#ifdef __cplusplus
}
#endif
#endif  // ATmega4809