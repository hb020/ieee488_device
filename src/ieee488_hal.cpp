#include <Arduino.h>
#include "ieee488_hal.h"

#ifdef ATmega4809

// See "ieee488_hal_ATmega4809.h" for the HAL interface definition.

// Interrupt handler for ATN (PC6) and maybe also DAV (PC1)
// Naked interrupt is NOT possible, because I need regular CPU registers inside the handler.

ISR(PORTC_PORT_vect) {
#ifndef DAV_INTR_HANDLER
  ieee488_handle_atn_interrupt(); // Call the handler for ATN interrupt
  VPORTC.INTFLAGS |= (1 << 6); // Clear the interrupt flag for ATN
#else
  if (PORTC.INTFLAGS & (1 << 6)) { // ATN interrupt
    ieee488_handle_atn_interrupt(); // Call the handler for ATN interrupt
    VPORTC.INTFLAGS |= (1 << 6); // Clear the interrupt flag for ATN
  }
  if (PORTC.INTFLAGS & (1 << 1)) { // DAV interrupt
    // Handle DAV interrupt
    VPORTC.INTFLAGS |= (1 << 1); // Clear the interrupt flag for DAV
  }
#endif
}

#ifdef DAV_INTR_HANDLER
void hal_control_DAV_interrupt(bool enable) {
  if (enable) {
    PORTC.PIN1CTRL |= PORT_ISC_FALLING_gc; // Enable interrupt on LOW for DAV (PC1)
  } else {
    PORTC.PIN1CTRL &= ~PORT_ISC_gm; // Disable interrupt on DAV (PC1)
  }
}
#endif

/** @brief Initialize the HAL.
 * @return true if initialization was successful, false otherwise.
 */
bool hal_init(void) {
  
  // Set data pins to input_pullup, no interrupts
  PORTD.PIN0CTRL = PORT_PULLUPEN_bm;
  PORTD.PIN1CTRL = PORT_PULLUPEN_bm;
  PORTD.PIN2CTRL = PORT_PULLUPEN_bm;
  PORTD.PIN3CTRL = PORT_PULLUPEN_bm;
  PORTD.PIN4CTRL = PORT_PULLUPEN_bm;
  PORTD.PIN5CTRL = PORT_PULLUPEN_bm;
  PORTD.PIN6CTRL = PORT_PULLUPEN_bm;
  PORTD.PIN7CTRL = PORT_PULLUPEN_bm;
  PORTD.DIRCLR = 0b11111111;

  // Set control pins to input_pullup, no interrupts, so we don't have to worry about them being driven low by the device when we are not driving them.
  PORTC.PIN0CTRL = PORT_PULLUPEN_bm;
  PORTC.PIN1CTRL = PORT_PULLUPEN_bm;
  PORTC.PIN2CTRL = PORT_PULLUPEN_bm;
  PORTC.PIN3CTRL = PORT_PULLUPEN_bm;
  PORTC.PIN4CTRL = PORT_PULLUPEN_bm;
  PORTC.PIN5CTRL = PORT_PULLUPEN_bm;
  PORTC.PIN6CTRL = PORT_PULLUPEN_bm;
  PORTC.PIN7CTRL = PORT_PULLUPEN_bm;
  PORTC.DIRCLR = 0b11111111; // input_pullup, no interrupts. Could also have used PORTC.DIR = 0b00000000; but this is more explicit.
  
#ifdef USE_VPORTS
  // Enable VPORTs for faster access to control lines
  // Assert = DIR out, low; Release = DIR in (because pullup)
  // So I set all outputs to 0
  VPORTC.OUT = 0b00000000;
  VPORTC.DIR = 0b00000000; // Set all control lines to input
#endif

  PORTC.PIN6CTRL |= PORT_ISC_BOTHEDGES_gc; // Enable interrupt on both edges for ATN (PC6)
#ifdef DAV_INTR_HANDLER  
  hal_control_DAV_interrupt(true); // Enable interrupt on DAV (PC1)
#endif
  CPUINT.LVL1VEC = PORTC_PORT_vect_num; // Set the interrupt vector for PORTC to the handler
  SREG |= (1 << SREG_I); // Enable global interrupts

  return true;

}

#endif  // ATmega4809