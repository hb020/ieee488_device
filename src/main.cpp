#include <Arduino.h>

#include "ieee488.h"
#include "ieee488_hal.h"

#define LED_R 13
#define LED_G 39
#define LED_B 38
int debug_output = 0;

/********************************************************************
 * Callbacks. They must be non-blocking; any blocking operations should
 * be handled in a separate thread or interrupt context.
 ********************************************************************/

static unsigned int tx_counter = 0;
/** @brief Get the next byte for transmission.
 * @param ctx The context pointer provided to ieee488_init().
 * @param byte Pointer to a byte to be filled with the next byte to transmit.
 * @param end Pointer to a boolean to be filled with true if this is the last byte of the message, false otherwise.
 * @return true if a byte was provided, false if there are no more bytes to send.
 */
static bool device_tx(void* c, uint8_t* b, bool* end) {
    (void)c;
    static const char s[] = "READY\n";
    if (tx_counter >= sizeof(s) - 1) {
        // Let device_rx determine new data to send.
        return false;
    }
    *b = (uint8_t)s[tx_counter++];
    *end = tx_counter == sizeof(s) - 1;
    return true;
}

/** @brief Handle a received byte.
 * @param ctx The context pointer provided to ieee488_init().
 * @param byte The received byte.
 * @param end True if this is the last byte of the message, false otherwise.
 */
static void device_rx(void* c, uint8_t b, bool end) {
    (void)c;
    tx_counter = 0; // allow new transmission to start after receiving a byte
    Serial.print("RX ");
    Serial.print(b, HEX);
    if (end) Serial.print(" END");
    Serial.println();
}

static void device_clear(void* c, bool selected) {
    (void)c;
    Serial.print(selected ? "selected" : "universal");
    Serial.println(" clear");
    tx_counter = 0;
}

static void device_trigger(void* c) {
    (void)c;
    Serial.println("trigger");
}

static uint8_t status_byte(void* c) {
    (void)c;
    return 0x10;
}

/** @brief Handle a change in the remote/local status. (RL1)
 * @param ctx The context pointer provided to ieee488_init().
 * @param remote True if the device is now in remote mode, false if in local mode.
 * @param lockout True if the device is now in lockout state, false otherwise.
 */
static void remote_changed(void* ctx, bool remote, bool lockout) {
    (void)ctx;
    (void)lockout;
    if (remote) {
        digitalWrite(LED_R, LOW);
        digitalWrite(LED_G, HIGH);
    } else {
        digitalWrite(LED_G, LOW);
        digitalWrite(LED_R, HIGH);
    }
    if (debug_output > 1) {
        Serial.print(remote ? "Remote" : "Local");
        Serial.print(lockout ? " lockout" : "");
        Serial.println();
    }
}

/** @brief Handle a change in the addressed status.
 * @param ctx The context pointer provided to ieee488_init().
 * @param addressed True if the device is now addressed, false otherwise.
 */
static void addressed_changed(void* ctx, bool addressed) {
    (void)ctx;
    digitalWrite(LED_B, addressed ? LOW : HIGH);
    if (debug_output > 1) {
        Serial.print(addressed ? "Addressed" : "Unaddressed");
        Serial.println();
    }
}

static void print_nr(uint8_t b) {
    b = b & 0x1fu;
    if (b == 0) Serial.print("00");
    else if (b == 1) Serial.print("01");
    else if (b == 2) Serial.print("02");
    else if (b == 3) Serial.print("03");
    else if (b == 4) Serial.print("04");
    else if (b == 5) Serial.print("05");
    else if (b == 6) Serial.print("06");
    else if (b == 7) Serial.print("07");
    else if (b == 8) Serial.print("08");
    else if (b == 9) Serial.print("09");
    else if (b == 10) Serial.print("10");
    else if (b == 11) Serial.print("11");
    else if (b == 12) Serial.print("12");
    else if (b == 13) Serial.print("13");
    else if (b == 14) Serial.print("14");
    else if (b == 15) Serial.print("15");
    else if (b == 16) Serial.print("16");
    else if (b == 17) Serial.print("17");
    else if (b == 18) Serial.print("18");
    else if (b == 19) Serial.print("19");
    else if (b == 20) Serial.print("20");
    else if (b == 21) Serial.print("21");
    else if (b == 22) Serial.print("22");
    else if (b == 23) Serial.print("23");
    else if (b == 24) Serial.print("24");
    else if (b == 25) Serial.print("25");
    else if (b == 26) Serial.print("26");
    else if (b == 27) Serial.print("27");
    else if (b == 28) Serial.print("28");
    else if (b == 29) Serial.print("29");
    else if (b == 30) Serial.print("30");
    else if (b == 31) Serial.print("31");
}

static void print_command(uint8_t b) {
    switch (b) {
        case IEEE488_CMD_GTL: Serial.print("GTL"); break;
        case IEEE488_CMD_SDC: Serial.print("SDC"); break;
        case IEEE488_CMD_PPC: Serial.print("PPC"); break;
        case IEEE488_CMD_GET: Serial.print("GET"); break;
        case IEEE488_CMD_TCT: Serial.print("TCT"); break;
        case IEEE488_CMD_LLO: Serial.print("LLO"); break;
        case IEEE488_CMD_DCL: Serial.print("DCL"); break;
        case IEEE488_CMD_PPU: Serial.print("PPU"); break;
        case IEEE488_CMD_SPE: Serial.print("SPE"); break;
        case IEEE488_CMD_SPD: Serial.print("SPD"); break;
        case IEEE488_CMD_UNL: Serial.print("UNL"); break;
        case IEEE488_CMD_UNT: Serial.print("UNT"); break;
        default:
            if ((b & 0x60u) == 0x20u) {
                Serial.print("L");
                print_nr(b);
                break;
            }
            if ((b & 0x60u) == 0x40u) {
                Serial.print("T");
                print_nr(b);
                break;
            }            
            if ((b & 0x60u) == 0x60u) {
                Serial.print("S");
                print_nr(b);
                break;
            } else {
                Serial.print("0x");
                Serial.print(b, HEX); break;
            }
    }
}

/** @brief Handle a command seen on the bus.
 *
 * Is called before the command is processed.
 *
 * @param ctx The context pointer provided to ieee488_init().
 * @param command The command byte that was seen.
 * @param before True if called before processing the command, false if called after.
 */
static void command_seen(void* ctx, uint8_t command, bool before) {
    (void)ctx;
    if (before) return;
    print_command(command);
    // Serial.print(" SH: ");
    // print_nr(ieee488_device.sh);
    // Serial.print(" AH: ");
    // print_nr(ieee488_device.ah);
    // Serial.print(" T: ");
    // print_nr(ieee488_device.talker);
    // Serial.print(" L: ");
    // print_nr(ieee488_device.listener);
    // Serial.print(" SR: ");
    // print_nr(ieee488_device.sr);
    // Serial.print(" RL: ");
    // print_nr(ieee488_device.rl);
    // Serial.print(" PP: ");
    // print_nr(ieee488_device.pp);
    // Serial.print(" SP: ");
    // Serial.print(ieee488_device.serial_poll_mode ? "1" : "0");
    // Serial.print(" TPA: ");
    // Serial.print(ieee488_device.talk_primary_addressed ? "1" : "0");
    // Serial.print(" LPA: ");
    // Serial.print(ieee488_device.listen_primary_addressed ? "1" : "0");
    // Serial.print(" PPC: ");
    // Serial.print(ieee488_device.pp_config_addressed ? "1" : "0");
    // Serial.print(" PPCfg: ");
    // Serial.print(ieee488_device.pp_configured ? "1" : "0");
    // Serial.print(" PPLine: ");
    // Serial.print(ieee488_device.pp_line);
    // Serial.print(" PPSense: ");
    // Serial.print(ieee488_device.pp_sense ? "1" : "0");
    // Serial.print(" IndStat: ");
    // Serial.print(ieee488_device.individual_status ? "1" : "0");
    // Serial.print(" ServPend: ");
    // Serial.print(ieee488_device.service_pending ? "1" : "0");
    // Serial.print(" TXLoaded: ");
    // Serial.print(ieee488_device.tx_loaded ? "1" : "0");
    // Serial.print(" TXEnd: ");
    // Serial.print(ieee488_device.tx_end ? "1" : "0");
    // Serial.print(" TXByte: ");
    // Serial.print(ieee488_device.tx_byte, HEX);
    // Serial.print(" Deadline: ");
    // Serial.print(ieee488_device.deadline);
    // Serial.print(" StateSince: ");
    // Serial.print(ieee488_device.state_since);
    // Serial.print(" LastIFC: ");
    // Serial.print(ieee488_device.last_ifc ? "1" : "0");
    // Serial.print(" LastATN: ");
    // Serial.print(ieee488_device.last_atn ? "1" : "0");
    // Serial.print(" LastDAV: ");
    // Serial.print(ieee488_device.last_dav ? "1" : "0");
    // Serial.print(" LastEOI: ");
    // Serial.print(ieee488_device.last_eoi ? "1" : "0");
    Serial.println();
}

ieee488_callbacks_t cb = {
    device_tx,       // tx_next
    device_rx,       // rx_byte
    status_byte,     // status_byte
    device_clear,    // device_clear
    device_trigger,  // device_trigger
    remote_changed,  // remote_changed,
    addressed_changed, // addressed_changed
    command_seen,    // command_seen
    0                // ctx
};

/********************************************************************
 * Config handling
 ********************************************************************/

ieee488_config_t cfg = {
    5,                    // primary address
    0,                    // secondary address
    false,                // extended address
    false,                // talk only
    false,                // listen only
    true,                 // use EOI
    '\n',                 // EOS byte
    false,                // EOS enabled
    1000,                 // T3 handshake timeout us, 0 is indefinite
    10,                   // T1 delay us
    0                     // pp_line
};

/********************************************************************
 * Menu system
 ********************************************************************/

void showConfig(void) {
    Serial.println("\n=== Configuration ===");
    Serial.print("- Address: ");
    Serial.print(cfg.primary_address);
    if (cfg.extended_address) {
        Serial.print(",");
        Serial.print(cfg.secondary_address);
    }
    Serial.println();
    Serial.print("- EOI (outgoing): ");
    Serial.println(cfg.use_eoi ? "enabled" : "disabled");
    Serial.print("- EOS (incoming): ");
    if (cfg.eos_enabled) {
        Serial.print("0x");
        Serial.println(cfg.eos_byte, HEX);
    } else {
        Serial.println("disabled");
    }
    Serial.print("- T3 Timeout (us): ");
    Serial.println(cfg.handshake_timeout_us);
    Serial.print("- T1 Delay (us): ");
    Serial.println(cfg.t1_delay_us);
    Serial.print("- PP Line: ");
    if (cfg.pp_line < 1 || cfg.pp_line > 8) {
        Serial.println("not configured");
    } else {
        Serial.println(cfg.pp_line);
    }
    Serial.print("- Debug output: ");
    if (debug_output == 0) Serial.println("disabled");
    else if (debug_output == 1) Serial.println("enabled");
    else Serial.println("verbose");
}

/********************************************************************
 * Configuration menu
 ********************************************************************/

 void strip(char* s) {
    size_t len = strlen(s);
    while (len > 0 && (isspace((unsigned char)s[len - 1]))) {
        s[len - 1] = '\0';
        len--;
    }
}

// read a line from Serial into buf, up to max_len-1 characters, null-terminated. 
// Returns the length of the stripped string read (excluding null terminator).
size_t readLine(char* buf, size_t max_len) {
    size_t len = 0;
    while (len < max_len - 1) {
        if (!Serial.available()) continue;
        char c = Serial.read();
        buf[len++] = c;
        Serial.print(c);  // Echo back the typed character
        if (c == '\n') break;
    }
    buf[len] = '\0';
    strip(buf);  // Remove trailing whitespace
    return strlen(buf);
}

void showPrompt(void) {
    Serial.print("\nEnter option (? for menu): ");
}

void printMenu(void) {
    Serial.println("\n=== IEEE 488 Device Configuration Menu ===");
    Serial.println("c. Show configuration");
    Serial.println("a. Set address (0-30)[,(0-30)]");
    Serial.println("e. Toggle outgoing EOI use");
    Serial.println("s. Set incoming EOS byte");
    Serial.println("3. Set T3 timeout (us)");
    Serial.println("1. Set T1 delay (us)");
    Serial.println("p. Set PP line (1-8, 0 to disable)");
    Serial.println("d. Toggle debug output");
    Serial.println("q. Activate and run the device");
}

// Run a simple menu, returns true if the device should be activated and run, false otherwise.
bool handleSerialConfig(void) {
    if (!Serial.available()) return false;
    
    char cmd = Serial.read();
    if (cmd == '\r') return false; // Ignore newlines
    if (cmd == '\n') {
        showPrompt();
        return false; // print newline, but ignore the rest
    }
    Serial.println(cmd);
    
    uint32_t value = 0;
    char buf[32];
    size_t len = 0;
    memset(buf, 0, sizeof(buf));
    
    switch (cmd) {
        case 'a':
        Serial.print("Current address: ");
            Serial.print(cfg.primary_address);
            if (cfg.extended_address) {
                Serial.print(",");
                Serial.print(cfg.secondary_address);
            }
            Serial.println();
            Serial.print("Enter address (0-30)[,(0-30)]: ");
            len = readLine(buf, sizeof(buf));
            if (len > 0) {
                unsigned int primary = 0;
                unsigned int secondary = 0;
                char *cptr = strchr(buf, ',');
                if (cptr) {
                    *cptr = '\0';
                    cptr++;
                    secondary = atoi(cptr);
                }
                primary = atoi(buf);
                if (primary > 30 || (cptr && secondary > 30)) {
                    Serial.print("Ignoring invalid address ");
                    Serial.print(primary, DEC);
                    if (cptr) {
                        Serial.print(",");
                        Serial.print(secondary, DEC);
                    }
                    Serial.println();
                    break;
                }
                cfg.primary_address = primary;
                if (cptr){
                    cfg.secondary_address = secondary;
                    cfg.extended_address = true;
                }
                else {
                    cfg.secondary_address = 0;
                    cfg.extended_address = false;
                }
                Serial.print("Address set to ");
                Serial.print(cfg.primary_address);
                if (cfg.extended_address) {
                    Serial.print(",");
                    Serial.print(cfg.secondary_address);
                }
                Serial.println();
            } else {
                Serial.println("Keeping address unchanged");
            }
            break;
                        
        case 'e':
            cfg.use_eoi = !cfg.use_eoi;
            Serial.print("EOI on outgoing messages is now ");
            Serial.println(cfg.use_eoi ? "enabled" : "disabled");
            break;
            
        case 's':
            Serial.print("EOS on incoming messages is now ");
            if (cfg.eos_enabled) {
                Serial.print("0x");
                if (cfg.eos_byte < 0x10) Serial.print("0");
                Serial.print(cfg.eos_byte, HEX);
            } else {
                Serial.print("disabled");
            }        
            Serial.println();
            Serial.print("Enter EOS byte (hex 00-FF or empty for none): ");
            len = readLine(buf, sizeof(buf));
            if (len == 0) {
                cfg.eos_enabled = false;
                Serial.println("EOS byte cleared");
                break;
            }
            value = (uint32_t)strtol(buf, NULL, 16);
            if (value <= 0xFF) {
                cfg.eos_byte = (uint8_t)value;
                cfg.eos_enabled = true;
                Serial.print("EOS byte set to 0x");
                if (cfg.eos_byte < 0x10) Serial.print("0");
                Serial.println(cfg.eos_byte, HEX);
            } else {
                Serial.println("Invalid byte value");
            }
            break;
                        
        case '3':
            Serial.print("T3 Timeout (us): ");
            Serial.println(cfg.handshake_timeout_us);        
            Serial.print("Enter T3 timeout (us): ");
            len = readLine(buf, sizeof(buf));
            if (len >  0) {            
                value = atoi(buf);
                cfg.handshake_timeout_us = value;
                Serial.print("T3 timeout set to ");
                Serial.print(value);
                Serial.println(" us");
            } else {
                Serial.println("Keeping T3 timeout unchanged");
            }
            break;
            
        case '1':
            Serial.print("T1 Delay (us): ");
            Serial.println(cfg.t1_delay_us);
            Serial.print("Enter T1 delay (us): ");
            len = readLine(buf, sizeof(buf));
            if (len >  0) {            
                value = atoi(buf);
                cfg.t1_delay_us = value;
                Serial.print("T1 delay set to ");
                Serial.print(value);
                Serial.println(" us");
            } else {
                Serial.println("Keeping T1 delay unchanged");
            }
            break;
            
        case 'p':
            Serial.print("Current PP line: ");
            if (cfg.pp_line < 1 || cfg.pp_line > 8) {
                Serial.print("not configured");
            } else {
                Serial.print(cfg.pp_line);
            }
            Serial.println();
            Serial.print("Enter PP line (1-8, 0 to disable): ");
            len = readLine(buf, sizeof(buf));
            if (len >  0) {            
                value = atoi(buf);
                if (value == 0 || (value >= 1 && value <= 8)) {
                    cfg.pp_line = value;
                    if (cfg.pp_line >= 1 && cfg.pp_line <= 8) {
                        ieee488_set_parallel_poll_local(true, cfg.pp_line, true);
                        ieee488_set_individual_status(true);
                        Serial.print("PP line set to ");
                        Serial.println(value);
                    } else {
                        ieee488_set_parallel_poll_local(false, 0, false);
                        ieee488_set_individual_status(false);
                        Serial.println("Parallel poll disabled");
                    }
                } else {
                    Serial.println("Invalid PP line (0, 1-8)");
                }
            } else {
                Serial.println("Keeping PP line unchanged");
            }
            break;

        case 'd':
            debug_output++;
            if (debug_output > 2) debug_output = 0;
            Serial.print("Debug output ");
            if (debug_output == 0) Serial.println("disabled");
            else if (debug_output == 1) Serial.println("enabled");
            else Serial.println("verbose");
            break;
            
        case 'c':
            showConfig();
            break;
            
        case 'h':
        case '?':
            printMenu();
            break;

        case 'q':
            return true; // Activate and run the device
            
        case '\t':
        case '\n':
        case '\r':
            return false; // Ignore whitespace

        default:
            Serial.print("Unknown option '");
            Serial.print(cmd);
            Serial.println("'");
            break;
    }
    showPrompt();
    return false;
}

/********************************************************************
 * The main program
 ********************************************************************/
bool device_active = false;

void setup() {
    pinMode(LED_R, OUTPUT);
    pinMode(LED_G, OUTPUT);
    pinMode(LED_B, OUTPUT);
    digitalWrite(LED_R, HIGH);
    digitalWrite(LED_G, LOW);
    digitalWrite(LED_B, HIGH);
    Serial.begin(115200);
    Serial.println("IEEE-488 Device");
    showConfig();
    Serial.setTimeout(10000);
    printMenu();
    showPrompt();
}

void loop() {
    if (!device_active) {
        if (handleSerialConfig()) {
            device_active = true;
            showConfig();
            if (!debug_output) {
                cb.addressed_changed = 0;
                cb.command_seen = 0;
            }
            ieee488_init(&cfg, &cb);
            if (cfg.pp_line >= 1 && cfg.pp_line <= 8) {
                ieee488_set_parallel_poll_local(true, cfg.pp_line, true);
                ieee488_set_individual_status(true);
            } else {
                ieee488_set_parallel_poll_local(false, 0, false);
                ieee488_set_individual_status(false);
            }        
            Serial.println("Starting the IEEE-488 Device...");
            Serial.println("Device is now active. Press reset to reconfigure.");
        }
    } else {
        ieee488_poll();
    }
}
