#include <Arduino.h>
#include "ieee488_hal.h"

#ifdef ATmega4809
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
  IFC_PIN   18 : GPIB  9 : PC4 : ieee488_line_t 12
  NDAC_PIN  17 : GPIB  8 : PC3 : ieee488_line_t 10
  NRFD_PIN  16 : GPIB  7 : PC2 : ieee488_line_t  9
  DAV_PIN   15 : GPIB  6 : PC1 : ieee488_line_t  8
  EOI_PIN   14 : GPIB  5 : PC0 : ieee488_line_t 15
  REN_PIN   21 : GPIB 17 : PC7 : ieee488_line_t 14
  SRQ_PIN   19 : GPIB 10 : PC5 : ieee488_line_t 13
  ATN_PIN   20 : GPIB 11 : PC6 : ieee488_line_t 11
*/

/** Set the state of the pull-up resistors for a port */
void set_port_pullup_bits(PORT_t& port, uint8_t reg){
  port.PIN0CTRL |= ((reg<<3) & PORT_PULLUPEN_bm);
  port.PIN1CTRL |= ((reg<<2) & PORT_PULLUPEN_bm);
  port.PIN2CTRL |= ((reg<<1) & PORT_PULLUPEN_bm);
  port.PIN3CTRL |= (reg & PORT_PULLUPEN_bm);
  port.PIN4CTRL |= ((reg>>1) & PORT_PULLUPEN_bm);
  port.PIN5CTRL |= ((reg>>2) & PORT_PULLUPEN_bm);
  port.PIN6CTRL |= ((reg>>3) & PORT_PULLUPEN_bm);
  port.PIN7CTRL |= ((reg>>4) & PORT_PULLUPEN_bm);
  
}

/** @brief Convert an IEEE488 line to the corresponding port bit.
 * @param line The IEEE488 line to convert.
 * @return The corresponding port mask for the line.
 */
inline uint8_t line_to_mask(ieee488_line_t line) {
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
 * @param line The line (bit) to read. See ieee488_line_t, 8..15.
 * @return true if the line is asserted, false otherwise.
 */
bool hal_read_line(ieee488_line_t line) {
  uint8_t mask = line_to_mask(line);
  PORTC.DIRCLR = mask; // Set the pin as input
  return (PORTC.IN & mask) == 0; // Asserted if the pin is LOW
}

/** @brief Drive a command line (bit) to asserted or released.
 * @param line The line (bit) to drive. See ieee488_line_t, 8..15.
 * @param asserted true to assert the line, false to release it.
 */
void hal_drive_line(ieee488_line_t line, bool asserted) {
  uint8_t mask = line_to_mask(line);
  PORTC.DIRSET = mask; // Set the pin as output
  if (asserted) {
    PORTC.OUTSET = mask;
  } else {
    PORTC.OUTCLR = mask;
  }
}

/** @brief Read the state of the digital I/O lines (DIO1-DIO8).
 * @return The logical state of the DIO lines, bit 0 = DIO1.
 */
uint8_t hal_read_dio(void) {
  return ~PORTD.IN;
}

/** @brief Drive the digital I/O lines (DIO1-DIO8).
 * @param value The logical state to drive onto the DIO lines, bit 0 = DIO1.
 * @param enable true to drive the lines, false to release them.
 */
void hal_drive_dio(uint8_t value, bool enable) {
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
uint32_t hal_time_us(void) {
    return micros();
}

/** @brief Initialize the HAL.
 * @return true if initialization was successful, false otherwise.
 */
bool hal_init(void) {
  // Set data pins to input_pullup
  set_port_pullup_bits(PORTD, 0xFF);
  PORTD.DIRCLR = 0b11111111;

  // Set control pins to input_pullup, so we don't have to worry about them being driven low by the device when we are not driving them.
  set_port_pullup_bits(PORTC, 0xFF);
  PORTC.DIRCLR = 0b11111111;

  return true;

}

#endif  // ATmega4809