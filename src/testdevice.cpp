#include <Arduino.h>
#include "config.h"
#include "testdevice.h"
#include "ieee488.h"

#define LONG_MAX_MS 10800000  // 3 hours in milliseconds
#define SHORT_MAX_MS 10000    // 10 seconds in milliseconds

// stuff from main.cpp that we need to access here
extern int debug_level;
extern void load_config(bool full_reset);

/********************************************************************
 * This code must all be non-blocking; any blocking operations should
 * be handled in a separate thread or interrupt context.
 ********************************************************************/

/* The SCPI interpreter here is really basic.
 * It does not support command chaining,
 * and does not support the full mandatory IEEE-488.2 command set.
 *
 * The reason for this is that I want to keep the code simple and small,
 * and I want to influence the reading/writing handling in detail.
 */

static unsigned int in_counter = 0;
static char in_buffer[256];

static unsigned int out_counter = 0;
static char out_buffer[256];

typedef enum {
    SCPI_NONE,
    SCPI_SYNTAX,
    SCPI_PARAMETER,
    SCPI_OVERFLOW
} scpi_error_state_t;

static scpi_error_state_t scpi_error_state = SCPI_NONE;

typedef enum {
    SCPI_IDLE,               // Before reading a command. Potential whitespace before the command will be ignored.
    SCPI_RECEIVING_COMMAND,  // Up to the first whitespace or end of message. The command is in the input buffer.
    SCPI_FINISHING_COMMAND,  // Up to the end of the message. The command is in the input buffer.
    SCPI_STREAM,             // Up to the end of message, but do not write to the input buffer.
    SCPI_FLUSH               // Flush the input buffer. This is used when a command has been processed or is in error and we are ready for the next command.
} scpi_in_state_t;

static scpi_in_state_t scpi_in_state = SCPI_IDLE;

static bool scpi_out_from_buffer = true;

struct scpi_command {
    const char* command;
    scpi_in_state_t (*handler)(uint8_t byte, bool end);
};

// The index of the command being handled.
// Only means something when scpi_in_state is SCPI_FINISHING_COMMAND or SCPI_STREAM.
// 255 means no command recognized yet.
static uint8_t scpi_command_idx = 255;

static uint32_t rx_deadline = 0;  // the time by which the next rx action can continue, in milliseconds. 0 means no delay is active. This is used to implement the rx_delay_ms feature.

/** @brief Arm the RX delay timer with the configured delay.
 *
 * To be used with the rx_delay_expired() function to check for RX delays.
 */
void rx_arm_delay(void) {
    if (cfg.rx_delay_ms) {
        rx_deadline = millis() + cfg.rx_delay_ms;
        if (rx_deadline == 0) rx_deadline = 1;  // avoid 0, which means no delay is active
    } else {
        rx_deadline = 0;
    }
}

/** @brief Check if the RX delay has expired.
 *
 * @return true if the RX delay has expired, false otherwise.
 */
bool rx_delay_expired(void) {
    return ((rx_deadline == 0) || (int32_t)(millis() - rx_deadline) >= 0);
}

static uint32_t tx_deadline = 0;  // the time by which the next tx action can continue, in milliseconds. 0 means no delay is active. This is used to implement the tx_delay_ms feature.

/** @brief Arm the TX delay timer with the configured delay.
 *
 * To be used with the tx_delay_expired() function to check for TX delays.
 * @param after_command true if this is after a command has been processed, false if this is after a byte has been transmitted.
 */
void tx_arm_delay(bool after_command) {
    if (after_command && cfg.reply_delay_ms) {
        tx_deadline = millis() + cfg.reply_delay_ms;
        if (tx_deadline == 0) tx_deadline = 1;  // avoid 0, which means no delay is active
    } else if (cfg.tx_delay_ms) {
        tx_deadline = millis() + cfg.tx_delay_ms;
        if (tx_deadline == 0) tx_deadline = 1;  // avoid 0, which means no delay is active
    } else {
        tx_deadline = 0;
    }
}

/** @brief Check if the TX delay has expired.
 *
 * @return true if the TX delay has expired, false otherwise.
 */
bool tx_delay_expired(void) {
    return ((tx_deadline == 0) || (int32_t)(millis() - tx_deadline) >= 0);
}

static uint32_t srq_deadline = 0;  // the time by which the next SRQ will arrive, in milliseconds. 0 means no delay is active. This is used to implement the SRQ delay feature.

/** @brief Arm the SRQ delay timer with the specified delay.
 *
 * @param delay_ms The delay in milliseconds. 0 means immediate.
 */
void srq_arm_delay(uint32_t delay_ms) {
    srq_deadline = millis() + delay_ms;
    if (srq_deadline == 0) srq_deadline = 1;  // avoid 0, which means no delay is active
}

/** @brief Disarm the SRQ delay timer.
 */
void srq_disarm_delay(void) {
    srq_deadline = 0;  // disarm the SRQ delay timer
}

/** @brief Check if the SRQ delay has expired.
 *
 * @return true if the SRQ delay has expired, false otherwise.
 */
bool srq_delay_expired(void) {
    return ((srq_deadline != 0) && (int32_t)(millis() - srq_deadline) >= 0);
}

/** @brief Restart the input buffer and state machine. */
void restart_in(void) {
    memset(in_buffer, 0, sizeof(in_buffer));
    in_counter = 0;
    scpi_command_idx = 255;
    scpi_in_state = SCPI_IDLE;
}

/** @brief Restart the output buffer and state machine. */
void restart_out(void) {
    memset(out_buffer, 0, sizeof(out_buffer));
    out_counter = 0;
    scpi_out_from_buffer = true;
    tx_arm_delay(true);  // arm the tx delay timer for the reply delay, if configured
}

/** @brief Get the end characters for SCPI messages.
 * @return The end characters, either the EOS byte if enabled, or "\r\n" if not.
 */
const char* endchars(void) {
    static char eos_char[2] = {0, 0};  // buffer to hold the EOS character and null terminator
    if (cfg.eos_enabled) {
        eos_char[0] = cfg.eos_byte;
        return eos_char;
    } else {
        return "\r\n";
    }
}

/** @brief Parse a string of space separated uint32_t integers from in_buffer and fill the values array.
 *
 * The values are expected to be in decimal format, separated by whitespace.
 * The first word in the string is to be ignored.
 * in_buffer has no leading whitespace, but can have trailing whitespace
 * and multiple spaces between the values.
 *
 * @param values Pointer to an array of uint32_t to be filled with the parsed values
 * @param num_values The number of values to parse and fill in the values array
 * @param allowed_separators A string of characters that are allowed as single char separators between values (space is always allowed).
 *        If NULL, only whitespace is allowed.
 * @return the number of values read
 */
int get_uint32_varvalues(uint32_t* values, size_t num_values, const char* allowed_separators) {
    const char* ptr = in_buffer;
    int values_found = 0;
    if (!values) return 0;
    // Skip the first word
    char* endptr;
    while (*ptr && isspace(*ptr)) ptr++;  // Skip leading whitespace
    ptr = strchr(ptr, ' ');
    if (!ptr) return 0;

    while (*ptr && values_found < num_values) {
        values[values_found++] = strtoul(ptr, &endptr, 10);
        if (ptr == endptr) {
            // No conversion performed
            return values_found - 1;  // Return the number of values found so far
        }
        ptr = endptr;
        if (!*ptr) break;  // End of string
        if (!isspace(*ptr)) {
            // Invalid character found
            if (allowed_separators && (!strchr(allowed_separators, *ptr))) {
                // Invalid character found, stop parsing
                return values_found - 1;  // Return the number of values found so far
            }
            ptr++;  // Skip the allowed separator
        }
        while (*ptr && isspace(*ptr)) ptr++;  // Skip whitespace
    }

    return values_found;
}

/** @brief Parse a string of space separated uint32_t integers from in_buffer and fill the values array.
 *
 * The values are expected to be in decimal format, separated by whitespace.
 * The first word in the string is to be ignored.
 * in_buffer has no leading whitespace, but can have trailing whitespace
 * and multiple spaces between the values.
 *
 * @param values Pointer to an array of uint32_t to be filled with the parsed values
 * @param num_values The number of values to parse and fill in the values array
 * @return the number of values read
 */
int get_uint32_varvalues(uint32_t* values, size_t num_values) {
    return get_uint32_varvalues(values, num_values, NULL);
}

/** @brief Parse a string of space separated int16_t integers from in_buffer and fill the values array.
 *
 * The values are expected to be in decimal format, separated by whitespace.
 * The first word in the string is to be ignored.
 * in_buffer has no leading whitespace, but can have trailing whitespace
 * and multiple spaces between the values.
 *
 * @param values Pointer to an array of int16_t to be filled with the parsed values
 * @param num_values The number of values to parse and fill in the values array
 * @param allowed_separators A string of characters that are allowed as single char separators between values (space is always allowed).
 *        If NULL, only whitespace is allowed.
 * @return the number of values read
 */
int get_int16_varvalues(int16_t* values, size_t num_values, const char* allowed_separators) {
    const char* ptr = in_buffer;
    int values_found = 0;
    if (!values) return 0;
    // Skip the first word
    char* endptr;
    while (*ptr && isspace(*ptr)) ptr++;  // Skip leading whitespace
    ptr = strchr(ptr, ' ');
    if (!ptr) return 0;

    while (*ptr && values_found < num_values) {
        values[values_found++] = (int16_t)strtol(ptr, &endptr, 10);
        if (ptr == endptr) {
            // No conversion performed
            return values_found - 1;  // Return the number of values found so far
        }
        ptr = endptr;
        if (!*ptr) break;  // End of string
        if (!isspace(*ptr)) {
            // Invalid character found
            if (allowed_separators && (!strchr(allowed_separators, *ptr))) {
                // Invalid character found, stop parsing
                return values_found - 1;  // Return the number of values found so far
            }
            ptr++;  // Skip the allowed separator
        }
        while (*ptr && isspace(*ptr)) ptr++;  // Skip whitespace
    }

    return values_found;
}

/** @brief Parse a string of space separated int16_t integers from in_buffer and fill the values array.
 *
 * The values are expected to be in decimal format, separated by whitespace.
 * The first word in the string is to be ignored.
 * in_buffer has no leading whitespace, but can have trailing whitespace
 * and multiple spaces between the values.
 *
 * @param values Pointer to an array of int16_t to be filled with the parsed values
 * @param num_values The number of values to parse and fill in the values array
 * @return the number of values read
 */
int get_int16_varvalues(int16_t* values, size_t num_values) {
    return get_int16_varvalues(values, num_values, NULL);
}

/*
 * The SCPI command set is defined here.
 * Each command has a string and a handler function.
 * The handler function is called when the command is recognized.
 * The handler function should be non-blocking and return quickly.
 *
 * The functions are called after the first command word is received, in SCPI_RECEIVING_COMMAND state.
 * They return the next wanted scpi_in_state.
 *
 * They all have 2 parameters:
 *    - byte, which is the received byte
 *    - end, which is true if this is the last byte of the message, false otherwise.
 * They all return the next scpi_in_state_t value
 */

/* `*IDN?` */
static scpi_in_state_t idn_handler(uint8_t byte, bool end) {
    // This is the *IDN? command handler.
    // It returns the identification string of the device.
    (void)byte;  // Unused parameter
    (void)end;   // Unused parameter
    restart_out();
    char address[16];
    if (cfg.extended_address) {
        sprintf(address, "%d.%d", cfg.primary_address, cfg.secondary_address);
    } else {
        sprintf(address, "%d", cfg.primary_address);
    }
    sprintf(out_buffer, "Bateau,ieee488_device,%s,1.0%s", address, endchars());
    return SCPI_FLUSH;
}

/* `SYSTem:ERRor?` */
static scpi_in_state_t err_handler(uint8_t byte, bool end) {
    // This is the :SYST:ERR? command handler.
    // It returns the last error message of the device.
    (void)byte;  // Unused parameter
    (void)end;   // Unused parameter
    restart_out();
    switch (scpi_error_state) {
        case SCPI_NONE:
            sprintf(out_buffer, "+0,\"No error\"%s", endchars());
            break;
        case SCPI_SYNTAX:
            sprintf(out_buffer, "-100,\"Syntax error\"%s", endchars());
            break;
        case SCPI_PARAMETER:
            sprintf(out_buffer, "-200,\"Parameter error\"%s", endchars());
            break;
        case SCPI_OVERFLOW:
            sprintf(out_buffer, "-300,\"Overflow error\"%s", endchars());
            break;
        default:
            sprintf(out_buffer, "-999,\"Unknown error\"%s", endchars());
            break;
    }
    scpi_error_state = SCPI_NONE;  // Clear the error state after reporting
    return SCPI_FLUSH;
}

/* `*CLS` */
static scpi_in_state_t cls_handler(uint8_t byte, bool end) {
    // This is the *CLS command handler.
    // It clears the error queue of the device and all in- and output buffers.
    (void)byte;                      // Unused parameter
    (void)end;                       // Unused parameter
    scpi_error_state = SCPI_NONE;    // Clear the error state
    restart_in();                    // Clear the input buffer
    restart_out();                   // Clear the output buffer
    cfg.rx_delay_ms = 0;             // Clear the RX delay
    cfg.tx_delay_ms = 0;             // Clear the TX delay
    cfg.reply_delay_ms = 0;          // Clear the reply delay
    rx_deadline = 0;                 // Clear the RX delay
    tx_deadline = 0;                 // Clear the TX delay
    srq_deadline = 0;                // Clear the SRQ delay
    ieee488_request_service(false);  // Clear the SRQ line to the controller
    return SCPI_FLUSH;
}

/* `*RST` */
static scpi_in_state_t rst_handler(uint8_t byte, bool end) {
    // This is the *RST command handler.
    // It resets the device to its default state.
    (void)byte;              // Unused parameter
    (void)end;               // Unused parameter
    cls_handler(byte, end);  // Clear the error state and buffers
    load_config(false);

    return SCPI_FLUSH;
}

/* `LONGWR? {ASCII data}` */
typedef enum { LONGWR_STARTING,
               LONGWR_RECEIVING,
               LONGWR_TRAILING,
               LONGWR_FLUSH } longwr_state_t;
static longwr_state_t longwr_state = LONGWR_STARTING;
static uint32_t longwr_counter = 0;  // The number of bytes received in the long write operation
static uint8_t longwr_start;         // The start character of the long write data
static uint8_t longwr_lastchar;      // The last character received in the long write operation
static char longwr_errmsg[128];      // Buffer for error messages

static void longwr_create_reply(void) {
    restart_out();
    if (longwr_errmsg[0] != '\0') {
        sprintf(out_buffer, "-1,%d,\"%s\"%s", (int)longwr_start, longwr_errmsg, endchars());
    } else {
        sprintf(out_buffer, "%lu,%d,\"\"%s", (unsigned long)longwr_counter, (int)longwr_start, endchars());
    }
}

static scpi_in_state_t longwr_handler(uint8_t byte, bool end) {
    // This is the LONGWR command handler.
    // It sets the device to long write mode.
    (void)byte;  // Unused parameter

    if (scpi_in_state == SCPI_RECEIVING_COMMAND) {
        // The first word of the command has been received
        longwr_counter = 0;
        longwr_start = 0;
        longwr_lastchar = 0;
        longwr_errmsg[0] = '\0';  // Clear any error message
        longwr_state = LONGWR_STARTING;
        longwr_create_reply();
        return SCPI_STREAM;
    }
    // I am in stream mode
    if (longwr_state == LONGWR_STARTING) {
        // I am in the starting state, waiting for the first byte of the ASCII data
        if (isspace(byte) || byte == '\'' || byte == '"') {
            // Ignore leading whitespace
            // return immediately. No need to change the state, as we are still in the starting state
            // and if this is the end, I have already positioned the output buffer to a reply
            return SCPI_STREAM;
        }
        longwr_start = byte;
        longwr_lastchar = 0;
        longwr_counter = 0;
        longwr_errmsg[0] = '\0';  // Clear the error message (again, just in case)
        longwr_state = LONGWR_RECEIVING;
        // and fall through to the receiving state to process the first byte of data
    }
    if (longwr_state == LONGWR_RECEIVING) {
        // I am in the stream handling state, receiving the ASCII data
        // have done the first byte, now we are receiving the rest of the ASCII data
        if (isspace(byte) || byte == '\'' || byte == '"') {
            // flush whitespace or quote at the end of the data, and return the reply
            longwr_state = LONGWR_TRAILING;
        } else {
            longwr_counter++;
            if (byte < 0x30 || byte > 0x7E) {
                // Invalid character received, flush the rest of the data and return an error
                sprintf(longwr_errmsg, "LONGWR? command received invalid character 0x%02X at position %lu", byte, (unsigned long)longwr_counter);
                longwr_create_reply();
                longwr_state = LONGWR_FLUSH;
            }
            if (longwr_lastchar != 0) {
                // the first character has been received, compare sequence
                if (longwr_lastchar == 0x7E && byte == 0x30) {
                    // wrap around from 0x7E to 0x30 is allowed
                } else if (byte != longwr_lastchar + 1) {
                    // Invalid character sequence received, flush the rest of the data and return an error
                    sprintf(longwr_errmsg, "LONGWR? command received invalid character sequence 0x%02X after 0x%02X at position %lu", byte, longwr_lastchar, (unsigned long)longwr_counter);
                    longwr_create_reply();
                    longwr_state = LONGWR_FLUSH;
                }
            }
            longwr_lastchar = byte;
        }
    }
    if ((longwr_state == LONGWR_TRAILING) && (!end)) {
        // I am in the trailing state, waiting for the end of the message
        if (!(isspace(byte) || byte == '\'' || byte == '"')) {
            // Invalid character received after the data, flush the rest of the data and return an error
            sprintf(longwr_errmsg, "LONGWR? command received invalid character 0x%02X after data", byte);
            longwr_create_reply();
            longwr_state = LONGWR_FLUSH;
        }
    }
    if (end) {
        // The end of the message has been received, return the reply
        longwr_create_reply();
        longwr_state = LONGWR_FLUSH;
    }
    return SCPI_STREAM;
}

static uint32_t longrd_len = 0;   // The length of the long read data still to be sent
static uint8_t longrd_ch = 0x30;  // The next character of the long read data to be sent

/* `LONGRD? [{LEN} [{START}]]` */
static scpi_in_state_t longrd_handler(uint8_t byte, bool end) {
    // This is the LONGRD command handler.
    // It sets the device to long read mode.
    (void)byte;  // Unused parameter

    if (!end) return SCPI_FINISHING_COMMAND;  // Wait for the end of the command to get the parameter
    // Parse the parameter from the input buffer
    uint32_t value[2];
    int nr = get_uint32_varvalues(value, 2);
    if (nr == 0) {
        value[0] = 1024;  // Default length is 1024 bytes
        value[1] = 0x30;  // Default start character is '0'
    } else if (nr == 1) {
        value[1] = 0x30;  // Default start character is '0'
    } else if (nr > 2) {
        scpi_error_state = SCPI_SYNTAX;
        return SCPI_FLUSH;
    }
    // limit start char
    if (value[1] < 0x30) {
        value[1] = 0x30;
    }
    if (value[1] > 0x7E) {
        value[1] = 0x7E;
    }
    longrd_len = value[0];
    longrd_ch = value[1];
    restart_out();
    scpi_out_from_buffer = false;

    return SCPI_FLUSH;
}

/** @brief Transmit the next byte for a long read operation.
 * @return The next byte of the long read data, or 0 if the long read is finished, and the terminator is to be sent.
 *         In that case, the transmit can continue from the regular out buffer.
 */
static uint8_t longrd_tx(void) {
    if (longrd_len == 0) {
        // finish it with the buffer
        restart_out();
        strcpy(out_buffer, endchars());
        return 0;
    } else {
        // loop through the ASCII characters from longrd_ch to 0x7E, wrapping around to 0x30, and decrement longrd_len
        longrd_len--;
        uint8_t next_ch = longrd_ch;
        if (longrd_ch >= 0x7E) {
            longrd_ch = 0x30;  // wrap around to '0'
        } else {
            longrd_ch++;
        }
        return next_ch;
    }
}

/* `SLOWWR {MSECS}` */
static scpi_in_state_t slowwr_handler(uint8_t byte, bool end) {
    // This is the SLOWWR command handler.
    // It sets the device to slow write mode.
    (void)byte;  // Unused parameter

    if (!end) return SCPI_FINISHING_COMMAND;  // Wait for the end of the command to get the parameter
    // Parse the parameter from the input buffer
    uint32_t value;
    if (get_uint32_varvalues(&value, 1) != 1) {
        scpi_error_state = SCPI_SYNTAX;
        return SCPI_FLUSH;
    }
    if (value > SHORT_MAX_MS) {
        value = SHORT_MAX_MS;  // Limit to 10 seconds
    }
    cfg.rx_delay_ms = value;
    return SCPI_FLUSH;
}

/* `SLOWRD {MSECS}` */
static scpi_in_state_t slowrd_handler(uint8_t byte, bool end) {
    // This is the SLOWRD command handler.
    // It sets the device to slow read mode.
    (void)byte;  // Unused parameter

    if (!end) return SCPI_FINISHING_COMMAND;  // Wait for the end of the command to get the parameter
    uint32_t value;
    if (get_uint32_varvalues(&value, 1) != 1) {
        scpi_error_state = SCPI_SYNTAX;
        return SCPI_FLUSH;
    }
    if (value > SHORT_MAX_MS) {
        value = SHORT_MAX_MS;  // Limit to 10 seconds
    }
    cfg.tx_delay_ms = value;
    return SCPI_FLUSH;
}

/* `DELAYRD {MSECS}` */
static scpi_in_state_t delayrd_handler(uint8_t byte, bool end) {
    // This is the DELAYRD command handler.
    // It sets the device to delay read mode.
    (void)byte;  // Unused parameter

    if (!end) return SCPI_FINISHING_COMMAND;  // Wait for the end of the command to get the parameter
    // Parse the parameter from the input buffer
    uint32_t value;
    if (get_uint32_varvalues(&value, 1) != 1) {
        scpi_error_state = SCPI_SYNTAX;
        return SCPI_FLUSH;
    }
    if (value > LONG_MAX_MS) {
        value = LONG_MAX_MS;  // Limit to 3 hours
    }
    cfg.reply_delay_ms = value;
    return SCPI_FLUSH;
}

/* `SLOWWR?` */
static scpi_in_state_t slowwrq_handler(uint8_t byte, bool end) {
    // This is the SLOWWR? command handler.
    (void)byte;                               // Unused parameter
    if (!end) return SCPI_FINISHING_COMMAND;  // Wait for the end of the command to get the parameter

    restart_out();
    sprintf(out_buffer, "%lu%s", (unsigned long)cfg.rx_delay_ms, endchars());
    return SCPI_FLUSH;
}

/* `SLOWRD?` */
static scpi_in_state_t slowrdq_handler(uint8_t byte, bool end) {
    // This is the SLOWRD? command handler.
    (void)byte;                               // Unused parameter
    if (!end) return SCPI_FINISHING_COMMAND;  // Wait for the end of the command to get the parameter

    restart_out();
    sprintf(out_buffer, "%lu%s", (unsigned long)cfg.tx_delay_ms, endchars());
    return SCPI_FLUSH;
}

/* `DELAYRD?` */
static scpi_in_state_t delayrdq_handler(uint8_t byte, bool end) {
    // This is the DELAYRD? command handler.
    (void)byte;                               // Unused parameter
    if (!end) return SCPI_FINISHING_COMMAND;  // Wait for the end of the command to get the parameter

    restart_out();
    sprintf(out_buffer, "%lu%s", (unsigned long)cfg.reply_delay_ms, endchars());
    return SCPI_FLUSH;
}

/* `SRQ [{MSECS}]` */
static scpi_in_state_t srq_handler(uint8_t byte, bool end) {
    (void)byte;                               // Unused parameter
    if (!end) return SCPI_FINISHING_COMMAND;  // Wait for the end of the command to get the parameter

    uint32_t value;
    if (get_uint32_varvalues(&value, 1) != 1) {
        value = 0;  // Default to 0 if no parameter is provided
    }
    if (value > LONG_MAX_MS) {
        value = LONG_MAX_MS;  // Limit to 3 hours
    }
    srq_arm_delay(value);
    return SCPI_FLUSH;
}

/* `ADDR {PRIMARY}[,{SECONDARY}]` (with allowed separators ",:. ") */
static scpi_in_state_t addr_handler(uint8_t byte, bool end) {
    (void)byte;                               // Unused parameter
    if (!end) return SCPI_FINISHING_COMMAND;  // Wait for the end of the command to get the parameter

    uint32_t value[2];

    int num_values = get_uint32_varvalues(value, 2, ",:.");
    if (num_values < 1) {
        scpi_error_state = SCPI_PARAMETER;
        return SCPI_FLUSH;
    }
    if (value[0] > 30 || (num_values == 2 && value[1] > 30)) {
        scpi_error_state = SCPI_PARAMETER;
        return SCPI_FLUSH;
    }
    cfg.primary_address = (uint8_t)value[0];
    if (num_values == 2) {
        cfg.extended_address = true;
        cfg.secondary_address = (uint8_t)value[1];
    } else {
        cfg.extended_address = false;
    }
    ieee488_reset(false);  // Reset the device with the new address, but do not reset the configuration
    return SCPI_FLUSH;
}

/* `EOS [{TERMCHAR}]` */
static scpi_in_state_t eos_handler(uint8_t byte, bool end) {
    (void)byte;                               // Unused parameter
    if (!end) return SCPI_FINISHING_COMMAND;  // Wait for the end of the command to get the parameter

    int16_t value;
    if (get_int16_varvalues(&value, 1) != 1) {
        value = -1;  // Default to 0 if no parameter is provided
    }
    if (value < 0 || value > 255) {
        cfg.eos_enabled = false;  // do not use EOS for incoming messages, use EOI only
        cfg.use_eoi = true;       // Use EOI for outgoing end of message
        // As a side effect, the `endchars()` function will use CR/LF at end of all outgoing messages
    } else if (value >= 0 && value <= 255) {
        cfg.eos_enabled = true;
        cfg.eos_byte = (uint8_t)value;  // use the provided byte as the EOS character for incoming messages (but EOI still is accepted)
        cfg.use_eoi = false;            // Do not use EOI for outgoing end of message. 
        // As a side effect, the `endchars()` function will use the EOS character at end of all outgoing messages
    } else {
        scpi_error_state = SCPI_PARAMETER;
    }
    return SCPI_FLUSH;
}

/* `EOS?` */
static scpi_in_state_t eosq_handler(uint8_t byte, bool end) {
    (void)byte;  // Unused parameter
    (void)end;   // Unused parameter
    // This is the EOS? command handler.
    // It returns the current end-of-string setting.
    restart_out();
    if (cfg.eos_enabled) {
        sprintf(out_buffer, "%u%s", (unsigned int)cfg.eos_byte, endchars());
    } else {
        sprintf(out_buffer, "-1%s", endchars());
    }
    return SCPI_FLUSH;
}

/* `T1 {USECS}` */
static scpi_in_state_t t1_handler(uint8_t byte, bool end) {
    // This is the T1 command handler.
    // It sets the T1 settling time for multiline messages.
    (void)byte;  // Unused parameter

    if (!end) return SCPI_FINISHING_COMMAND;  // Wait for the end of the command to get the parameter
    // Parse the parameter from the input buffer
    uint32_t value;
    if (get_uint32_varvalues(&value, 1) != 1) {
        scpi_error_state = SCPI_SYNTAX;
        return SCPI_FLUSH;
    }
    if (value > 1000000) {  // Limit to 1 second
        value = 1000000;
    }
    if (value < 2) {
        value = 2;  // Minimum value is 2 us, as per the IEEE 488.1-1987 standard
    }
    cfg.t1_delay_us = value;
    return SCPI_FLUSH;
}

/* `T1?` */
static scpi_in_state_t t1q_handler(uint8_t byte, bool end) {
    (void)byte;  // Unused parameter
    (void)end;   // Unused parameter
    // This is the T1? command handler.
    // It returns the current T1 delay setting.
    restart_out();
    sprintf(out_buffer, "%u%s", (unsigned int)cfg.t1_delay_us, endchars());
    return SCPI_FLUSH;
}

/* `T1 {USECS}` */
static scpi_in_state_t t3_handler(uint8_t byte, bool end) {
    // This is the T3 command handler.
    (void)byte;  // Unused parameter

    if (!end) return SCPI_FINISHING_COMMAND;  // Wait for the end of the command to get the parameter
    // Parse the parameter from the input buffer
    uint32_t value;
    if (get_uint32_varvalues(&value, 1) != 1) {
        scpi_error_state = SCPI_SYNTAX;
        return SCPI_FLUSH;
    }
    if (value > 10000000) {  // Limit to 10 seconds
        value = 10000000;
    }
    cfg.handshake_timeout_us = value;
    return SCPI_FLUSH;
}

/* `T3?` */
static scpi_in_state_t t3q_handler(uint8_t byte, bool end) {
    (void)byte;  // Unused parameter
    (void)end;   // Unused parameter
    // This is the T3? command handler.
    // It returns the current T3 timeout setting.
    restart_out();
    sprintf(out_buffer, "%u%s", (unsigned int)cfg.handshake_timeout_us, endchars());
    return SCPI_FLUSH;
}

static scpi_command scpi_commands[] = {
    {"*IDN?", idn_handler},
    {":SYST:ERR?", err_handler},  // yeah, this repitition is stupid. Here a better parser would be better. But it is the only case...
    {":SYSTEM:ERR?", err_handler},
    {":SYST:ERROR?", err_handler},
    {":SYSTEM:ERROR?", err_handler},
    {"SYST:ERR?", err_handler},
    {"SYSTEM:ERR?", err_handler},
    {"SYST:ERROR?", err_handler},
    {"SYSTEM:ERROR?", err_handler},
    {"*RST", rst_handler},
    {"*CLS", cls_handler},
    {"LONGWR?", longwr_handler},
    {"LONGRD?", longrd_handler},
    {"SLOWWR", slowwr_handler},
    {"SLOWRD", slowrd_handler},
    {"DELAYRD", delayrd_handler},
    {"SLOWWR?", slowwrq_handler},
    {"SLOWRD?", slowrdq_handler},
    {"DELAYRD?", delayrdq_handler},
    {"SRQ", srq_handler},
    {"ADDR", addr_handler},
    {"EOS", eos_handler},
    {"EOS?", eosq_handler},
    {"T1", t1_handler},
    {"T1?", t1q_handler},
    {"T3", t3_handler},
    {"T3?", t3q_handler},
};

bool add_to_in_buffer(uint8_t byte, bool end) {
    if (in_counter >= sizeof(in_buffer) - 1) {
        // Buffer overflow, silently flush
        scpi_error_state = SCPI_OVERFLOW;
        scpi_in_state = SCPI_FLUSH;
        if (end) {
            restart_in();  // Clear the input buffer after processing
        }
        return false;
    }
    in_buffer[in_counter++] = (char)byte;
    return true;
}

/** @brief Handle a received byte to be used in a command.
 * @param byte The received byte.
 * @param end True if this is the last byte of the message, false otherwise.
 */
void read_command(uint8_t byte, bool end) {
    // Serial.print("Read_Command, scpi_in_state=");
    // Serial.print(scpi_in_state);
    // Serial.print(", in_counter=");
    // Serial.println(in_counter);

    switch (scpi_in_state) {
        case SCPI_IDLE:
            // In idle state, we are waiting for the first byte of a command
            if (isspace(byte)) {
                // Ignore leading whitespace
                return;
            }
            // if not: fall through to receiving command state
            scpi_in_state = SCPI_RECEIVING_COMMAND;
        case SCPI_RECEIVING_COMMAND:
            if (!add_to_in_buffer(byte, end)) {
                return;  // Buffer overflow, stop processing
            }
            // In receiving command state, we are collecting bytes for the command
            // handle the byte if it is the end of the command or a whitespace
            if (end || (byte == ' ')) {
                // The command is complete, process it

                in_buffer[in_counter] = '\0';  // Null-terminate the string
                // Remove trailing whitespace for the comparison
                size_t len = strlen(in_buffer);
                while (len > 0 && isspace(in_buffer[len - 1])) {
                    len--;
                }
                // compare with all known commands
                for (size_t i = 0; i < sizeof(scpi_commands) / sizeof(scpi_commands[0]); i++) {
                    struct scpi_command cmd = scpi_commands[i];
                    if (strncasecmp(in_buffer, cmd.command, len) == 0) {
                        // Command recognized, call the handler if it exists
                        if (cmd.handler) {
                            scpi_in_state = cmd.handler(byte, end);
                            scpi_command_idx = i;  // Store the index of the command, for later use in SCPI_FINISHING_COMMAND or SCPI_STREAM state
                        } else {
                            scpi_in_state = SCPI_FLUSH;  // No handler, flush the input
                        }
                        return;
                    }
                }
                // nothing found: error
                scpi_error_state = SCPI_SYNTAX;
                scpi_in_state = SCPI_FLUSH;
                return;
            }
            break;
        case SCPI_FINISHING_COMMAND:
            // In finishing command state, we are waiting for the end of the command
            if (!add_to_in_buffer(byte, end)) {
                return;  // Buffer overflow, stop processing
            }
            // We are waiting for the end of the command
            if (end) {
                // Call the handler again to finish processing
                if (scpi_command_idx < sizeof(scpi_commands) / sizeof(scpi_commands[0])) {
                    struct scpi_command cmd = scpi_commands[scpi_command_idx];
                    if (cmd.handler) {
                        cmd.handler(byte, end);
                    }
                }
            }
            break;
        case SCPI_STREAM:
            // In stream state, we are reading all input until the end of the message
            if (scpi_command_idx < sizeof(scpi_commands) / sizeof(scpi_commands[0])) {
                struct scpi_command cmd = scpi_commands[scpi_command_idx];
                if (cmd.handler) {
                    cmd.handler(byte, end);
                }
            }
            break;
        case SCPI_FLUSH:
            // In flush state, we are ignoring all input until the next command
            break;
    }
    if (end) {
        restart_in();  // Clear the input buffer after processing
    }
    return;
}

/** @brief Handle a received byte from an input stream.
 * @param byte The received byte.
 * @param end True if this is the last byte of the message, false otherwise.
 */
void read_stream(uint8_t byte, bool end) {
    if (scpi_in_state != SCPI_STREAM) {
        // Not in stream mode, ignore the byte
        return;
    }
}

/** The callbacks from the IEEE-488 interface */

/** @brief Get the next byte for transmission.
 * @param byte Pointer to a byte to be filled with the next byte to transmit.
 * @param end Pointer to a boolean to be filled with true if this is the last byte of the message, false otherwise.
 * @return true if a byte was provided, false if there are no more bytes to send.
 */
bool device_tx(uint8_t* byte, bool* end) {
    if (!tx_delay_expired()) {
        // If the TX delay has not expired, we cannot send a byte yet
        *end = false;
        return false;
    }
    *byte = 0;
    if (!scpi_out_from_buffer) {
        // If we are not sending from the buffer, we stream There is only 1 streamer, so that is simple
        // This function may tell us to use the buffer instead.
        *byte = longrd_tx();
        *end = false;  // We are not at the end of the message yet, as long as we are streaming
    }
    if (scpi_out_from_buffer) {
        if (out_counter >= strlen(out_buffer)) {
            if (cfg.use_eoi) {
                *end = true;
            } else {
                *end = false;
            }
            return false;
        }
        *byte = (uint8_t)out_buffer[out_counter++];
        if (cfg.use_eoi) {
            *end = out_counter >= strlen(out_buffer);
        } else {
            *end = false;
        }
    }
    tx_arm_delay(false);  // arm the tx delay timer for the next byte, if configured

    if (debug_level > 0) {
        Serial.print("TX ");
        if (*byte < 32) {
            Serial.print("0x");
            if (*byte < 16) Serial.print("0");
            Serial.print(*byte, HEX);
        } else {
            Serial.print("'");
            Serial.print((char)*byte);
            Serial.print("'");
        }
        if (*end) Serial.print(" END");
        Serial.println();
    }
    return true;
}

/** @brief Check if the device is ready to receive a byte.
 * @return true if the device is ready to receive a byte, false otherwise.
 */
bool device_rx_ready(void) {
    // The device is ready to receive a byte if the RX delay has expired
    return rx_delay_expired();  // you might want to add:  && (scpi_in_state != SCPI_FLUSH);
}

/** @brief Handle a received byte.
 * @param byte The received byte.
 * @param end True if this is the last byte of the message, false otherwise.
 */
void device_rx(uint8_t byte, bool end) {
    if (debug_level > 0) {
        Serial.print("RX ");
        if (byte < 32) {
            Serial.print("0x");
            if (byte < 16) Serial.print("0");
            Serial.print(byte, HEX);
        } else {
            Serial.print("'");
            Serial.print((char)byte);
            Serial.print("'");
        }
        if (end) Serial.print(" END");
        Serial.println();
    }
    read_command(byte, end);
    rx_arm_delay();  // arm the rx delay timer for the next byte, if configured
    if (end) {
        restart_in();
    }
}

void device_clear(bool selected) {
    cls_handler(0, true);
    Serial.print(selected ? "selected" : "universal");
    Serial.println(" clear");
}

void device_trigger(void) {
    Serial.println("trigger");
}

uint8_t status_byte(void) {    
    uint8_t status = 0x00;
    if (out_counter < strlen(out_buffer)) {
        status |= 0x01;  // Message available bit set
    }
    if (scpi_error_state != SCPI_NONE) {
        status |= 0x04;  // Syntax error bit set
    }
    // I do not have to worry about the SRQ bit, as it is handled by the IEEE-488 state machine itself
    return status;
}

/** @brief Handle a change in the remote/local status. (RL1)
 * @param remote True if the device is now in remote mode, false if in local mode.
 * @param lockout True if the device is now in lockout state, false otherwise.
 */
void remote_changed(bool remote, bool lockout) {
    (void)lockout;
    if (remote) {
        digitalWrite(LED_R, LOW);
        digitalWrite(LED_G, HIGH);
    } else {
        digitalWrite(LED_G, LOW);
        digitalWrite(LED_R, HIGH);
    }
    if (debug_level > 1) {
        Serial.print(remote ? "Remote" : "Local");
        Serial.print(lockout ? " lockout" : "");
        Serial.println();
    }
}

/** @brief Handle a change in the addressed status.
 * @param addressed True if the device is now addressed, false otherwise.
 */
void addressed_changed(bool addressed) {
    digitalWrite(LED_B, addressed ? LOW : HIGH);
    if (debug_level > 1) {
        Serial.print(addressed ? "Addressed" : "Unaddressed");
        Serial.println();
    }
}

void handle_idle(void) {
    // This function can be used to handle idle state, such as processing commands or other tasks.
    if (srq_delay_expired()) {
        // If the SRQ delay has expired we can set the SRQ state
        srq_disarm_delay();  // Clear the SRQ delay
        // and signal the SRQ line to the controller
        ieee488_request_service(true);
    }
}
