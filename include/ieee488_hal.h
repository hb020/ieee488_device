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
#define ATN_IN_INTR_HANDLER

// When the pins are physically LOW, they are true (asserted) in the IEEE 488.1-1987 standard. 
// When the pins are physically HIGH, they are false (released) in the standard. 
// The HAL must implement wired-OR/open-collector semantics where required.

// The control bits are 0-7, corresponding to respectively DAV, NRFD, NDAC, ATN, IFC, SRQ, REN, and EOI.

/** @brief Read the state of a command line (bit).
 * @param line The line (bit) to read. See ieee488_ctrl_line_t, 8..15.
 * @return true if the line is asserted (pulled LOW), false otherwise.
 */
bool hal_read_line(ieee488_ctrl_line_t line);

/** @brief Read the state of the DAV line.
 * @return true if DAV is asserted (pulled LOW), false otherwise.
 */
bool hal_read_dav(void);

/** @brief Read the state of the NRFD line.
 * @return true if NRFD is asserted (pulled LOW), false otherwise.
 */
bool hal_read_nrfd(void);

/** @brief Read the state of the NDAC line.
 * @return true if NDAC is asserted (pulled LOW), false otherwise.
 */
bool hal_read_ndac(void);

/** @brief Read the state of the ATN line.
 * @return true if ATN is asserted (pulled LOW), false otherwise.
 */
bool hal_read_atn(void);

/** @brief Read the state of the IFC line.
 * @return true if IFC is asserted (pulled LOW), false otherwise.
 */
bool hal_read_ifc(void);

// No need for bool hal_read_srq(void);

/** @brief Read the state of the REN line.
 * @return true if REN is asserted (pulled LOW), false otherwise.
 */
bool hal_read_ren(void);

/** @brief Read the state of the EOI line.
 * @return true if EOI is asserted (pulled LOW), false otherwise.
 */
bool hal_read_eoi(void);

/** @brief Read the state of the ATN and EOI lines together.
 * @return true if both ATN and EOI are asserted (pulled LOW), false otherwise.
 */
bool hal_read_atn_and_eoi(void); // for ppol


/** @brief Drive a command line (bit) to asserted or released.
 * @param line The line (bit) to drive. See ieee488_ctrl_line_t, 8..15.
 * @param asserted true to assert the line (pull LOW), false to release it.
 */
void hal_drive_line(ieee488_ctrl_line_t line, bool asserted);

/** @brief Drive the DAV line.
 * @param asserted true to assert the line (pull LOW), false to release it.
 */
void hal_drive_dav(bool asserted);

/** @brief Drive the NRFD line.
 * @param asserted true to assert the line (pull LOW), false to release it.
 */
void hal_drive_nrfd(bool asserted);

/** @brief Drive the NDAC line.
 * @param asserted true to assert the line (pull LOW), false to release it.
 */
void hal_drive_ndac(bool asserted);

// No need for void hal_drive_atn(bool asserted);
// No need for void hal_drive_ifc(bool asserted);

/** @brief Drive the SRQ line.
 * @param asserted true to assert the line (pull LOW), false to release it.
 */
void hal_drive_srq(bool asserted);

// No need for void hal_drive_ren(bool asserted);

/** @brief Drive the EOI line.
 * @param asserted true to assert the line (pull LOW), false to release it.
 */
void hal_drive_eoi(bool asserted);

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

#endif // IEEE488_HAL_H