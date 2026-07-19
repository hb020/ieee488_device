#ifndef IEEE488_HAL_H
#define IEEE488_HAL_H
#include <stdbool.h>
#include <stdint.h>
#include "ieee488.h"

#ifdef __cplusplus
extern "C" {
#endif

// Determine if the ATN line is being handled in an interrupt context.
// This can be used to achieve the timing constraints around ATN changes if the entire loop takes too much time.
#define ATN_INTR_HANDLER
// Determine if the IDY state (ATN && EOI) line is being handled in an interrupt context.
// This can be used to achieve the timing constraints around IDY changes if the entire loop takes too much time.
// #define IDY_INTR_HANDLER

// When the pins are physically LOW, they are true (asserted) in the IEEE 488.1-1987 standard. 
// When the pins are physically HIGH, they are false (released) in the standard. 
// The HAL must implement wired-OR/open-collector semantics where required.

// The control bits are 0-7, corresponding to respectively DAV, NRFD, NDAC, ATN, IFC, SRQ, REN, and EOI.

// You must define the following functions or preprocessor definitions in your HAL implementation:

// ** The pin reading functions that must return true if the line is asserted (pulled LOW), false otherwise. **
//
// DAV_IS_ASSERTED()
// NRFD_IS_ASSERTED()
// NDAC_IS_ASSERTED()
// ATN_IS_ASSERTED()
// IFC_IS_ASSERTED()
// SRQ_IS_ASSERTED()
// REN_IS_ASSERTED()
// EOI_IS_ASSERTED()
// IDY_IS_ASSERTED() 

// ** The pin driving functions that must drive the line LOW (asserted): **

// DAV_ASSERT()
// NRFD_ASSERT()
// NDAC_ASSERT()
// ATN_ASSERT()
// IFC_ASSERT()
// SRQ_ASSERT()
// REN_ASSERT()
// EOI_ASSERT()

// DAV_RELEASE()
// NRFD_RELEASE()
// NDAC_RELEASE()
// ATN_RELEASE()
// IFC_RELEASE()
// SRQ_RELEASE()
// REN_RELEASE()
// EOI_RELEASE()


/** @brief Read the state of the digital I/O lines (DIO1-DIO8).
 * @return The logical state of the DIO lines, bit 0 = DIO1.
 */
uint8_t hal_read_dio(void);

/** @brief Drive the digital I/O lines (DIO1-DIO8).
 * @param value The logical state to drive onto the DIO lines, bit 0 = DIO1.
 * @param enable true to drive the lines, false to release them.
 */
void hal_drive_dio(uint8_t value, bool enable);

/** @brief Get the time in microseconds since startup.
 * @return Time in microseconds since startup.
 */
uint32_t hal_time_us(void);

/** @brief Initialize the HAL.
 * @return true if initialization was successful, false otherwise.
 */
bool hal_init(void);

#ifdef __cplusplus
}
#endif

#ifdef ATmega4809

#include "ieee488_hal_ATmega4809.h"

#endif // ATmega4809

#endif // IEEE488_HAL_H