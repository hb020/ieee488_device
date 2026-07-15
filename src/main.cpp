#include <Arduino.h>

#include "ieee488.h"

ieee488_device_t d;

/* Replace with GPIO/transceiver access. */
static bool rd(void* c, ieee488_line_t l) {
    (void)c;
    (void)l;
    return false;
}
static void wr(void* c, ieee488_line_t l, bool a) {
    (void)c;
    (void)l;
    (void)a;
}
static uint8_t rdio(void* c) {
    (void)c;
    return 0;
}
static void wdio(void* c, uint8_t b, bool e) {
    (void)c;
    (void)b;
    (void)e;
}

/** @brief  Time in microseconds since startup.
 * @param c Context pointer (unused).
 * @return Time in microseconds since startup.
 */
static uint32_t tus(void* c) {
    (void)c;
    return micros();
}

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
    printf("RX %02x%s\n", b, end ? " END" : "");
}
static void clear(void* c, bool selected) {
    (void)c;
    printf("%s clear\n", selected ? "selected" : "universal");
}
static void trigger(void* c) {
    (void)c;
    puts("trigger");
}
static uint8_t stb(void* c) {
    (void)c;
    return 0x10;
}

ieee488_hal_t h = {
    0,     // ctx
    rd,    // read line
    wr,    // drive line
    rdio,  // read dio
    wdio,  // drive dio
    tus    // time us
};

ieee488_config_t cfg = {
    5,                    // primary address
    0,                    // secondary address
    IEEE488_ADDR_NORMAL,  // address mode
    false,                // talk only
    false,                // listen only
    true,                 // use EOI
    '\n',                 // EOS byte
    false,                // EOS enabled
    0,                    // handshake timeout us, 0 is indefinite
    1                     // t1 delay us
};

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

void setup() {
    ieee488_init(&d, &h, &cfg, &cb);
}

void loop() {
    ieee488_poll(&d);
}
