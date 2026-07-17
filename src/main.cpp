#include <Arduino.h>

#include "ieee488.h"
#include "ieee488_hal.h"

ieee488_device_t d;

/********************************************************************
 * Callbacks. They must be non-blocking; any blocking operations should 
 * be handled in a separate thread or interrupt context.
 ********************************************************************/

static bool tx(void* c, uint8_t* b, bool* end) {
    (void)c;
    static const char s[] = "READY\n";
    static unsigned i;
    if (i >= sizeof(s) - 1) return false;
    *b = (uint8_t)s[i++];
    *end = i == sizeof(s) - 1;
    return true;
}

static void rx(void* c, uint8_t b, bool end) {
    (void)c;
    Serial.print("RX ");
    Serial.print(b, HEX);
    if (end) Serial.print(" END");
    Serial.println();
}

static void clear(void* c, bool selected) {
    (void)c;
    Serial.print(selected ? "selected" : "universal");
    Serial.println(" clear");
}

static void trigger(void* c) {
    (void)c;
    Serial.println("trigger");
}

static uint8_t stb(void* c) {
    (void)c;
    return 0x10;
}

ieee488_callbacks_t cb = {
    tx,       // tx_next
    rx,       // rx_byte
    stb,      // status_byte
    clear,    // device_clear
    trigger,  // device_trigger
    0,        // remote_changed
    0,        // command_seen
    0         // ctx
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
    0,                    // T3 handshake timeout us, 0 is indefinite
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
