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
// It will also handle the time sensitive handling of IDY (ATN ^ EOI). This will fail if EOI is not asserted at the same time as ATN.
#define ATN_INTR_HANDLER

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

// READ_DIO() // read the DIO lines, return a byte with bit 0 = DIO1, bit 1 = DIO2, ..., bit 7 = DIO8
// DRIVE_DIO(value, enable) // drive the DIO lines with the given value (bit 0 = DIO1, bit 1 = DIO2, ..., bit 7 = DIO8) if enable is true, otherwise release the lines

// TIME_US()  // Return the time in microseconds since startup. This is used for timing constraints in the IEEE 488.1-1987 standard.

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