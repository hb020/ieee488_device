#include <Arduino.h>

#include "ieee488.h"
#include "ieee488_hal.h"

ieee488_device_t d;

/********************************************************************
 * Callbacks. They must be non-blocking; any blocking operations should
 * be handled in a separate thread or interrupt context.
 ********************************************************************/

static unsigned int tx_counter = 0;
static bool device_tx(void* c, uint8_t* b, bool* end) {
    (void)c;
    static const char s[] = "READY\n";
    if (tx_counter >= sizeof(s) - 1) return false;
    *b = (uint8_t)s[tx_counter++];
    *end = tx_counter == sizeof(s) - 1;
    if (*end) tx_counter = 0;
    return true;
}

static void device_rx(void* c, uint8_t b, bool end) {
    (void)c;
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
    Serial.print(remote ? "remote" : "local");
    Serial.println(lockout ? " lockout" : "");
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
        case 0x55: Serial.print("TE "); break;
        default:
            if ((b & 0x60u) == 0x20u) {
                Serial.print("L");
                Serial.print((int)b & 0x1fu, DEC); break;
            }
            if ((b & 0x60u) == 0x40u) {
                Serial.print("T");
                Serial.print((int)b & 0x1fu, DEC); break;
            }            
            if ((b & 0x60u) == 0x60u) {
                Serial.print("S");
                Serial.print((int)b & 0x1fu, DEC); break;
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
    Serial.print("CMD: ");
    print_command(command);
    // Serial.print(" SH: ");
    // Serial.print(d.sh, HEX);
    // Serial.print(" AH: ");
    // Serial.print(d.ah, HEX);
    // Serial.print(" T: ");
    // Serial.print(d.talker, HEX);
    // Serial.print(" L: ");
    // Serial.print(d.listener, HEX);
    // Serial.print(" SR: ");
    // Serial.print(d.sr, HEX);
    // Serial.print(" RL: ");
    // Serial.print(d.rl, HEX);
    // Serial.print(" PP: ");
    // Serial.print(d.pp, HEX);
    // Serial.print(" SP: ");
    // Serial.print(d.serial_poll_mode ? "true" : "false");
    // Serial.print(" TPA: ");
    // Serial.print(d.talk_primary_addressed ? "true" : "false");
    // Serial.print(" LPA: ");
    // Serial.print(d.listen_primary_addressed ? "true" : "false");
    // Serial.print(" PPC: ");
    // Serial.print(d.pp_config_addressed ? "true" : "false");
    // Serial.print(" PPCfg: ");
    // Serial.print(d.pp_configured ? "true" : "false");
    // Serial.print(" PPLine: ");
    // Serial.print(d.pp_line, HEX);
    // Serial.print(" PPSense: ");
    // Serial.print(d.pp_sense ? "true" : "false");
    // Serial.print(" IndStat: ");
    // Serial.print(d.individual_status ? "true" : "false");
    // Serial.print(" ServPend: ");
    // Serial.print(d.service_pending ? "true" : "false");
    // Serial.print(" TXLoaded: ");
    // Serial.print(d.tx_loaded ? "true" : "false");
    // Serial.print(" TXEnd: ");
    // Serial.print(d.tx_end ? "true" : "false");
    // Serial.print(" TXByte: ");
    // Serial.print(d.tx_byte, HEX);
    // Serial.print(" Deadline: ");
    // Serial.print(d.deadline);
    // Serial.print(" StateSince: ");
    // Serial.print(d.state_since);
    // Serial.print(" LastIFC: ");
    // Serial.print(d.last_ifc ? "true" : "false");
    // Serial.print(" LastATN: ");
    // Serial.print(d.last_atn ? "true" : "false");
    // Serial.print(" LastDAV: ");
    // Serial.print(d.last_dav ? "true" : "false");
    // Serial.print(" LastEOI: ");
    // Serial.print(d.last_eoi ? "true" : "false");
    Serial.println();
}

ieee488_callbacks_t cb = {
    device_tx,       // tx_next
    device_rx,       // rx_byte
    status_byte,     // status_byte
    device_clear,    // device_clear
    device_trigger,  // device_trigger
    0, // remote_changed,  // remote_changed
    0, // command_seen,    // command_seen
    0                // ctx
};

/********************************************************************
 * The main program
 ********************************************************************/

ieee488_config_t cfg = {
    5,                    // primary address
    0,                    // secondary address
    IEEE488_ADDR_NORMAL,  // address mode
    false,                // talk only
    false,                // listen only
    true,                 // use EOI
    '\n',                 // EOS byte
    false,                // EOS enabled
    1000,                 // T3 handshake timeout us, 0 is indefinite
    10                    // T1 delay us
};

void setup() {
    Serial.begin(115200);
    Serial.println("Starting IEEE 488.1 device...");
    ieee488_init(&d, &cfg, &cb);
    Serial.print("The device is present on address ");
    Serial.println(cfg.primary_address);
}

void loop() {
    ieee488_poll(&d);
}
