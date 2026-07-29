#ifndef TESTDEVICE_H
#define TESTDEVICE_H

#include <Arduino.h>

/** @brief Get the next byte for transmission.
 * @param byte Pointer to a byte to be filled with the next byte to transmit.
 * @param end Pointer to a boolean to be filled with true if this is the last byte of the message, false otherwise.
 * @return true if a byte was provided, false if there are no more bytes to send.
 */
bool device_tx(uint8_t* byte, bool* end);

/** @brief Check if the device is ready to receive a byte.
 * @return true if the device is ready to receive a byte, false otherwise.
 */
bool device_rx_ready(void);

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

/** @brief Handle a change in the remote/local status. (RL1)
 * @param remote True if the device is now in remote mode, false if in local mode.
 * @param lockout True if the device is now in lockout state, false otherwise.
 */
void remote_changed(bool remote, bool lockout);

/** @brief Handle a change in the addressed status.
 * @param addressed True if the device is now addressed, false otherwise.
 */
void addressed_changed(bool addressed);

/** @brief Handle the idle state.
 */
void handle_idle(void);

#endif // TESTDEVICE_H