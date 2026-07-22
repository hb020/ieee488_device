#include <Arduino.h>
#include "ieee488_hal.h"

#ifdef ATmega4809

// See "ieee488_hal_ATmega4809.h" for the HAL interface definition.

#ifdef ATN_INTR_HANDLER  

// Interrupt handler for ATN line (PC6)
#if 1 == 1
ISR(PORTC_PORT_vect) {
  if (PORTC.INTFLAGS & (1 << 6)) { // Check if ATN triggered the interrupt
    ieee488_handle_atn_interrupt(); // Call the handler for ATN interrupt
    // Handle ATN interrupt
    PORTC.INTFLAGS = (1 << 6); // Clear the interrupt flag for ATN
  }
}
#else
// Naked interrupt is possible, but requires VPORT instead of PORT, and makes things slightly more complex.
// Handling multiple pins at the same time, and pullups require special care.
// The calling of the interrupt handler is 25% faster (3us instead of 4us), but I need 400% faster ()
ISR(PORTC_PORT_vect, ISR_NAKED) {
   ieee488_handle_atn_interrupt(); // Call the handler for ATN interrupt
   VPORTC.INTFLAGS |= (1 << 6); // Clear the interrupt flag for ATN
   reti();
}
#endif

#endif  // ATN_INTR_HANDLER

/** @brief Initialize the HAL.
 * @return true if initialization was successful, false otherwise.
 */
bool hal_init(void) {
  // Set data pins to input_pullup

  PORTD.PIN0CTRL |= PORT_PULLUPEN_bm;
  PORTD.PIN1CTRL |= PORT_PULLUPEN_bm;
  PORTD.PIN2CTRL |= PORT_PULLUPEN_bm;
  PORTD.PIN3CTRL |= PORT_PULLUPEN_bm;
  PORTD.PIN4CTRL |= PORT_PULLUPEN_bm;
  PORTD.PIN5CTRL |= PORT_PULLUPEN_bm;
  PORTD.PIN6CTRL |= PORT_PULLUPEN_bm;
  PORTD.PIN7CTRL |= PORT_PULLUPEN_bm;
  PORTD.DIRCLR = 0b11111111;

  // Set control pins to input_pullup, so we don't have to worry about them being driven low by the device when we are not driving them.
  PORTC.PIN0CTRL |= PORT_PULLUPEN_bm;
  PORTC.PIN1CTRL |= PORT_PULLUPEN_bm;
  PORTC.PIN2CTRL |= PORT_PULLUPEN_bm;
  PORTC.PIN3CTRL |= PORT_PULLUPEN_bm;
  PORTC.PIN4CTRL |= PORT_PULLUPEN_bm;
  PORTC.PIN5CTRL |= PORT_PULLUPEN_bm;
  PORTC.PIN6CTRL |= PORT_PULLUPEN_bm;
  PORTC.PIN7CTRL |= PORT_PULLUPEN_bm;
  PORTC.DIRCLR = 0b11111111;

  // Disable interrupts for all pins, except for ATN if ATN_INTR_HANDLER is defined.
  PORTC.PIN0CTRL &= ~PORT_ISC_gm;
  PORTC.PIN1CTRL &= ~PORT_ISC_gm;
  PORTC.PIN2CTRL &= ~PORT_ISC_gm;
  PORTC.PIN3CTRL &= ~PORT_ISC_gm;
  PORTC.PIN4CTRL &= ~PORT_ISC_gm;
  PORTC.PIN5CTRL &= ~PORT_ISC_gm;
  PORTC.PIN6CTRL &= ~PORT_ISC_gm;
  PORTC.PIN7CTRL &= ~PORT_ISC_gm;

#ifdef ATN_INTR_HANDLER
  PORTC.PIN6CTRL |= PORT_ISC_BOTHEDGES_gc; // Enable interrupt on both edges for ATN (PC6)
#endif

  return true;

}

#endif  // ATmega4809