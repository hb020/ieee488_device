#include <Arduino.h>
#include "scpi_handler.h"
#include "ieee488.h"

extern ieee488_config_t cfg;

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
}

/** @brief Get the end character for SCPI messages. 
 * @return The end character, either the EOS byte if enabled, or '\n' if not.
*/
char endchar(void) {
    return cfg.eos_enabled ? (char)cfg.eos_byte : '\n';
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
    const char* ptr = in_buffer;
    int values_found = 0;
    if (!values) return 0;
    // Skip the first word
    char* endptr;
    ptr = strchr(ptr, ' ');
    if (!ptr) return 0;
    while (*ptr && isspace(*ptr)) ptr++;  // Skip whitespace

    while (*ptr && values_found < num_values) {
        values[values_found++] = strtoul(ptr, &endptr, 10);
        if (ptr == endptr) {
            // No conversion performed
            return values_found - 1;  // Return the number of values found so far
        }
        ptr = endptr;
        while (*ptr && isspace(*ptr)) ptr++;  // Skip whitespace
    }

    return values_found;
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
 * They all have 1 parameter: end, which is true if this is the last byte of the message, false otherwise.
 * They all return the next scpi_in_state_t value
 */

/* `*IDN?` */
static scpi_in_state_t idn_handler(uint8_t byte, bool end) {
    // This is a placeholder for the *IDN? command handler.
    // It should return the identification string of the device.
    (void)byte;  // Unused parameter
    (void)end;   // Unused parameter
    restart_out();
    char address[16];
    if (cfg.extended_address) {
        sprintf(address, "%d.%d", cfg.primary_address, cfg.secondary_address);
    } else {
        sprintf(address, "%d", cfg.primary_address);
    }
    sprintf(out_buffer, "Bateau,ieee488_device,%s,1.0%c", address, endchar());
    return SCPI_FLUSH;
}

/* `SYSTem:ERRor?` */
static scpi_in_state_t err_handler(uint8_t byte, bool end) {
    // This is a placeholder for the :SYST:ERR? command handler.
    // It should return the last error message of the device.
    (void)byte;  // Unused parameter
    (void)end;   // Unused parameter
    restart_out();
    switch (scpi_error_state) {
        case SCPI_NONE:
            sprintf(out_buffer, "0,\"No error\"%c", endchar());
            break;
        case SCPI_SYNTAX:
            sprintf(out_buffer, "-100,\"Syntax error\"%c", endchar());
            break;
        case SCPI_PARAMETER:
            sprintf(out_buffer, "-200,\"Parameter error\"%c", endchar());
            break;
        case SCPI_OVERFLOW:
            sprintf(out_buffer, "-300,\"Overflow error\"%c", endchar());
            break;
        default:
            sprintf(out_buffer, "-999,\"Unknown error\"%c", endchar());
            break;
    }
    scpi_error_state = SCPI_NONE;  // Clear the error state after reporting
    return SCPI_FLUSH;
}

/* `*CLS` */
static scpi_in_state_t cls_handler(uint8_t byte, bool end) {
    // This is a placeholder for the *CLS command handler.
    // It should clear the error queue of the device.
    (void)byte;  // Unused parameter
    (void)end;   // Unused parameter
    scpi_error_state = SCPI_NONE;  // Clear the error state
    restart_in();  // Clear the input buffer
    restart_out(); // Clear the output buffer
    return SCPI_FLUSH;
}

/* `LONGWR? {ASCII data}` */
static scpi_in_state_t longwr_handler(uint8_t byte, bool end) {
    // This is a placeholder for the LONGWR command handler.
    // It should set the device to long write mode.
    (void)byte;  // Unused parameter
    (void)end;   // Unused parameter

    if (!end) return SCPI_STREAM;  // Enter stream mode to receive the ASCII data
    return SCPI_FLUSH;
}

/* `LONGRD? {LEN} {START}` */
static scpi_in_state_t longrd_handler(uint8_t byte, bool end) {    
    // This is a placeholder for the LONGRD command handler.
    // It should set the device to long read mode.
    (void)byte;  // Unused parameter

    if (!end) return SCPI_FINISHING_COMMAND;  // Wait for the end of the command to get the parameter
    // Parse the parameter from the input buffer
    uint32_t value[2];
    if (get_uint32_varvalues(value, 2) != 2) {
        scpi_error_state = SCPI_SYNTAX;
        return SCPI_FLUSH;
    }
    // start char is OK?
    if (value[1] < 48 ||  value[1] > 126) {
        scpi_error_state = SCPI_PARAMETER;
        return SCPI_FLUSH;
    }

    // TODO handle the long read with value[0] as LEN and value[1] as START

    return SCPI_FLUSH;
}

/* `SLOWWR {USECS}` */
static scpi_in_state_t slowwr_handler(uint8_t byte, bool end) {
    // This is a placeholder for the SLOWWR command handler.
    // It should set the device to slow write mode.
    (void)byte;  // Unused parameter

    if (!end) return SCPI_FINISHING_COMMAND;  // Wait for the end of the command to get the parameter
    // Parse the parameter from the input buffer
    uint32_t value;
    if (get_uint32_varvalues(&value, 1) != 1) {
        scpi_error_state = SCPI_SYNTAX;
        return SCPI_FLUSH;
    }
    cfg.rx_delay_us = value;
    return SCPI_FLUSH;
}

/* `SLOWRD {USECS}` */
static scpi_in_state_t slowrd_handler(uint8_t byte, bool end) {
    // This is a placeholder for the SLOWRD command handler.
    // It should set the device to slow read mode.
    (void)byte;  // Unused parameter

    if (!end) return SCPI_FINISHING_COMMAND;  // Wait for the end of the command to get the parameter
    uint32_t value;
    if (get_uint32_varvalues(&value, 1) != 1) {
        scpi_error_state = SCPI_SYNTAX;
        return SCPI_FLUSH;
    }
    cfg.tx_delay_us = value;
    return SCPI_FLUSH;
}

/* `DELAYRD {SECS}` */
static scpi_in_state_t delayrd_handler(uint8_t byte, bool end) {
    // This is a placeholder for the DELAYRD command handler.
    // It should set the device to delay read mode.
    (void)byte;  // Unused parameter

    if (!end) return SCPI_FINISHING_COMMAND;  // Wait for the end of the command to get the parameter
    // Parse the parameter from the input buffer
    uint32_t value;
    if (get_uint32_varvalues(&value, 1) != 1) {
        scpi_error_state = SCPI_SYNTAX;
        return SCPI_FLUSH;
    }
    cfg.reply_delay_s = value;
    return SCPI_FLUSH;
}

/* `SLOWWR?` */
static scpi_in_state_t slowwrq_handler(uint8_t byte, bool end) {
    // This is a placeholder for the SLOWWR? command handler.
    (void)byte;  // Unused parameter
    if (!end) return SCPI_FINISHING_COMMAND;  // Wait for the end of the command to get the parameter

    restart_out();
    sprintf(out_buffer, "%lu%c", (unsigned long)cfg.rx_delay_us, endchar());
    return SCPI_FLUSH;
}

/* `SLOWRD?` */
static scpi_in_state_t slowrdq_handler(uint8_t byte, bool end) {
    // This is a placeholder for the SLOWRD? command handler.
    (void)byte;  // Unused parameter
    if (!end) return SCPI_FINISHING_COMMAND;  // Wait for the end of the command to get the parameter

    restart_out();
    sprintf(out_buffer, "%lu%c", (unsigned long)cfg.tx_delay_us, endchar());
    return SCPI_FLUSH;
}

/* `DELAYRD?` */
static scpi_in_state_t delayrdq_handler(uint8_t byte, bool end) {
    // This is a placeholder for the DELAYRD? command handler.
    (void)byte;  // Unused parameter
    if (!end) return SCPI_FINISHING_COMMAND;  // Wait for the end of the command to get the parameter

    restart_out();
    sprintf(out_buffer, "%lu%c", (unsigned long)cfg.reply_delay_s, endchar());
    return SCPI_FLUSH;
}

/* `SRQ [{DELAY}]` */
static scpi_in_state_t srq_handler(uint8_t byte, bool end) {
    (void)byte;  // Unused parameter
    if (!end) return SCPI_FINISHING_COMMAND;  // Wait for the end of the command to get the parameter
    
    uint32_t value;
    if (get_uint32_varvalues(&value, 1) != 1) {
        value = 0;  // Default to 0 if no parameter is provided
    }
    // TODO handle the SRQ with value as DELAY
    return SCPI_FLUSH;
}

/* `ADDR {PRIMARY} [{SECONDARY}]` */
static scpi_in_state_t addr_handler(uint8_t byte, bool end) {
    (void)byte;  // Unused parameter
    if (!end) return SCPI_FINISHING_COMMAND;  // Wait for the end of the command to get the parameter
    
    uint32_t value[2];
    int num_values = get_uint32_varvalues(value, 2);
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
    return SCPI_FLUSH;
}

/* `EOS [{TERMCHAR}]` */
static scpi_in_state_t eos_handler(uint8_t byte, bool end) {
    (void)byte;  // Unused parameter
    if (!end) return SCPI_FINISHING_COMMAND;  // Wait for the end of the command to get the parameter
    
    uint32_t value;
    if (get_uint32_varvalues(&value, 1) != 1) {
        value = 0;  // Default to 0 if no parameter is provided
    }
    if (value == 0) {
        cfg.eos_enabled = false;
    } else if (value >= 1 && value <= 255) {
        cfg.eos_enabled = true;
        cfg.eos_byte = (uint8_t)value;
    } else {
        scpi_error_state = SCPI_PARAMETER;
    }
    return SCPI_FLUSH;
}

/* `EOS?` */
static scpi_in_state_t eosq_handler(uint8_t byte, bool end) {
    (void)byte;  // Unused parameter
    (void)end;   // Unused parameter
    // This is a placeholder for the EOS? command handler.
    // It should return the current end-of-string setting.
    restart_out();
    if (cfg.eos_enabled) {
        sprintf(out_buffer, "%u%c", (unsigned int)cfg.eos_byte, endchar());
    } else {
        sprintf(out_buffer, "0%c", endchar());
    }
    return SCPI_FLUSH;
}

static scpi_command scpi_commands[] = {
    {"*IDN?", idn_handler},
    {":SYST:ERR?", err_handler},
    {":SYSTEM:ERR?", err_handler},
    {":SYST:ERROR?", err_handler},
    {":SYSTEM:ERROR?", err_handler},
    {"*RST", nullptr},
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
};

/** @brief Handle a received byte to be used in a command.
 * @param byte The received byte.
 * @param end True if this is the last byte of the message, false otherwise.
 */
void read_command(uint8_t byte, bool end) {
    // store the byte in the input buffer if we are in receiving or finishing command state
    if (scpi_in_state == SCPI_RECEIVING_COMMAND || scpi_in_state == SCPI_FINISHING_COMMAND) {
        if (in_counter >= sizeof(in_buffer) - 1) {
            // Buffer overflow, silently flush
            scpi_error_state = SCPI_OVERFLOW;
            scpi_in_state = SCPI_FLUSH;
            return;
        }
        // Not in idle state, store the byte in the input buffer
        in_buffer[in_counter++] = (char)byte;
        // and continue to process the byte in the state machine below
    }

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
                        if (end) {
                            restart_in();  // Clear the input buffer after processing
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

            // We are waiting for the end of the command
            if (end) {
                // Call the handler again to finish processing
                if (scpi_command_idx < sizeof(scpi_commands) / sizeof(scpi_commands[0])) {
                    struct scpi_command cmd = scpi_commands[scpi_command_idx];
                    if (cmd.handler) {
                        cmd.handler(byte, end);
                    }
                }
                scpi_in_state = SCPI_IDLE;
            }
            break;
        case SCPI_STREAM:
            // In stream state, we are ignoring all input until the end of the message

            break;
        case SCPI_FLUSH:
            // In flush state, we are ignoring all input until the next command
            break;
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
    if (!scpi_out_from_buffer) {
        // If we are not sending from the buffer, we can indicate that there is no data to send
        // TODO fill in
        *end = true;
        return false;
    }
    uint8_t ch = 0;
    if (out_counter >= sizeof(out_buffer) - 1) {
        // all sent
        *end = true;
        return false;
    }
    *byte = (uint8_t)out_buffer[out_counter++];
    *end = out_counter == strlen(out_buffer);
    return true;
}

/** @brief Handle a received byte.
 * @param byte The received byte.
 * @param end True if this is the last byte of the message, false otherwise.
 */
void device_rx(uint8_t byte, bool end) {
    // debug
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
    read_command(byte, end);
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
    // TODO add volatile SRQ bit 6 to the status byte if a service request is pending
    uint8_t status = 0x10;
    if (scpi_error_state != SCPI_NONE) {
        status |= 0x04;  // Syntax error bit set
    }
    return status;
}

void handle_idle(void) {
    // This function can be used to handle idle state, such as processing commands or other tasks.
    // For now, it does nothing.
}