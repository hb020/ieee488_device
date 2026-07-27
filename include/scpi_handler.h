#ifndef SCPI_HANDLER_H
#define SCPI_HANDLER_H

#include <Arduino.h>

/** @brief Get the next byte for transmission.
 * @param byte Pointer to a byte to be filled with the next byte to transmit.
 * @param end Pointer to a boolean to be filled with true if this is the last byte of the message, false otherwise.
 * @return true if a byte was provided, false if there are no more bytes to send.
 */
bool device_tx(uint8_t* byte, bool* end);

/** @brief Handle a received byte.
 * @param byte The received byte.
 * @param end True if this is the last byte of the message, false otherwise.
 */
void device_rx(uint8_t byte, bool end);

/** @brief Handle a device clear (DC1, 2.10) command.
 * @param selected True if the device is addressed, false if it is a universal clear.
 */
void device_clear(bool selected);

/** @brief Handle a device trigger (DT1, 2.11) command.
 */
void device_trigger(void);

/** @brief Get the status byte.
 * @return The status byte with STB bits excluding RQS bit 6.
 */
uint8_t status_byte(void);

/** @brief Handle the idle state.
 */
void handle_idle(void);

#endif // SCPI_HANDLER_H