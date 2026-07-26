#ifndef IEEE488_H
#define IEEE488_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// This implementation knows how to talk to 1 GPIB bus connector.
// More connectors could in theory be possible by for example
// making `struct ieee488_device_t` a parameter to all functions,
// but that would make the HAL more complex and slower, and it is
// not needed for the current use case.

// The mechanism needed for syncing between the ISR and the main code is affected
// by 8 bit Arduino limitations: `#include <stdatomic.h>` is not supported (yet).
// Therefore, I use `volatile`, `#include <util/atomic.h>` and `ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { .... }`.
// If you want to move to a better chain, you WILL want to use atomic variables.

/* IEEE 488.1-1987, 1.4.3: sixteen signal lines. Logical true means asserted.
 * The HAL must implement wired-OR/open-collector semantics where required.
 */
typedef enum {
    IEEE488_DAV = 0,  // R W
    IEEE488_NRFD,     // R W
    IEEE488_NDAC,     // R W
    IEEE488_ATN,      // R
    IEEE488_IFC,      // R
    IEEE488_SRQ,      //   W
    IEEE488_REN,      // R
    IEEE488_EOI       // R W
} ieee488_ctrl_line_t;

/** @brief Hardware Abstraction Layer (HAL) for IEEE 488.1-1987.
 * The HAL provides the interface between the IEEE 488.1-1987 protocol implementation and the underlying hardware.
 */

// See "ieee488_hal.h" for the HAL interface definition.
// The HAL must implement wired-OR/open-collector semantics where required.
// It does not go through ctx for speed reasons, but the HAL can use its own context if needed.

/* IEEE 488.1 multiline command codes, 2.13 and Annex E. */
enum {
    IEEE488_CMD_GTL = 0x01,
    IEEE488_CMD_SDC = 0x04,
    IEEE488_CMD_PPC = 0x05,
    IEEE488_CMD_GET = 0x08,
    IEEE488_CMD_TCT = 0x09,
    IEEE488_CMD_LLO = 0x11,
    IEEE488_CMD_DCL = 0x14,
    IEEE488_CMD_PPU = 0x15,
    IEEE488_CMD_SPE = 0x18,
    IEEE488_CMD_SPD = 0x19,
    IEEE488_CMD_UNL = 0x3F,
    IEEE488_CMD_UNT = 0x5F
};

// Convert primary address to LAD command byte
#define IEEE488_LAD(a) ((uint8_t)(0x20u | ((a) & 0x1Fu)))
// Convert primary address to TAD command byte
#define IEEE488_TAD(a) ((uint8_t)(0x40u | ((a) & 0x1Fu)))
// Convert primary address to SAD command byte
#define IEEE488_SAD(a) ((uint8_t)(0x60u | ((a) & 0x1Fu)))

// Parallel poll sense and enable command bytes
#define IEEE488_PPE(line_1_to_8, sense) ((uint8_t)(0x60u | (((sense) ? 1u : 0u) << 3) | (((line_1_to_8) - 1u) & 7u)))
#define IEEE488_PPD 0x70u

typedef enum { IEEE488_OK = 0,
               IEEE488_EINVAL = -1,
               IEEE488_EBUSY = -2,
               IEEE488_ETIMEOUT = -3,
               IEEE488_EOVERFLOW = -4 } ieee488_result_t;


/** @brief Configuration for an IEEE 488.1-1987 device. */
typedef struct {
    uint8_t primary_address;              // 0..30; 31 is UNL/UNT
    uint8_t secondary_address;            // 0..31, used in extended mode
    bool extended_address;                // true if using extended addressing, false for normal addressing
    bool talk_only;                       // local ton message, 2.5.5
    bool listen_only;                     // local lon message, 2.6.5
    bool use_eoi;                         // Use the EOI line to indicate the end of a message going to the bus. If false, the controller has to deduct the end of message from the returned data instead of from EOI.
    uint8_t eos_byte;                     // The byte value that indicates the end of a message coming from the bus, aside from any potential EOI.
    bool eos_enabled;                     // True if EOS byte is enabled, false otherwise.
    uint32_t handshake_timeout_us;        // T3 handshake timeout us, 0 = no software timeout
    uint32_t t1_delay_us;                 // T1 source settling delay; see 2.3 and 3.8
    uint8_t pp_line;                      // The DIO line to use for parallel poll local (1-8). 0 for 'not configured'.
} ieee488_config_t;

/** @brief Callbacks for an IEEE 488.1-1987 device.
 *
 * The callback functions must be non-blocking; any blocking operations should be handled in a separate thread or interrupt context.
 */
typedef struct {
    /* Device-dependent data path, outside the standard (1.4.1, 2.1.1). */

    /** @brief Get the next byte for transmission.
     * @param ctx The context pointer provided to ieee488_init().
     * @param byte Pointer to a byte to be filled with the next byte to transmit.
     * @param end Pointer to a boolean to be filled with true if this is the last byte of the message, false otherwise.
     * @return true if a byte was provided, false if there are no more bytes to send.
     */
    bool (*tx_next)(void* ctx, uint8_t* byte, bool* end);

    /** @brief Handle a received byte.
     * @param ctx The context pointer provided to ieee488_init().
     * @param byte The received byte.
     * @param end True if this is the last byte of the message, false otherwise.
     */
    void (*rx_byte)(void* ctx, uint8_t byte, bool end);

    /** @brief Get the status byte.
     * @param ctx The context pointer provided to ieee488_init().
     * @return The status byte with STB bits excluding RQS bit 6.
     */
    uint8_t (*status_byte)(void* ctx);

    /* Interface function actions. */

    /** @brief Handle a device clear (DC1, 2.10) command.
     * @param ctx The context pointer provided to ieee488_init().
     * @param selected True if the device is addressed, false if it is a universal clear.
     */
    void (*device_clear)(void* ctx, bool selected);

    /** @brief Handle a device trigger (DT1, 2.11) command.
     * @param ctx The context pointer provided to ieee488_init().
     */
    void (*device_trigger)(void* ctx);

    /** @brief Handle a change in the remote/local status. (RL1)
     * @param ctx The context pointer provided to ieee488_init().
     * @param remote True if the device is now in remote mode, false if in local mode.
     * @param lockout True if the device is now in lockout state, false otherwise.
     */
    void (*remote_changed)(void* ctx, bool remote, bool lockout);

    /** @brief Handle a change in the addressed status.
     * @param ctx The context pointer provided to ieee488_init().
     * @param addressed True if the device is now addressed, false otherwise.
     */
    void (*addressed_changed)(void* ctx, bool addressed);

    /** @brief Handle a command seen on the bus.
     *
     * Is called before and after the command processing.
     *
     * @param ctx The context pointer provided to ieee488_init().
     * @param command The command byte that was seen.
     * @param before True if called before processing the command, false if called after.
     */
    void (*command_seen)(void* ctx, uint8_t command, bool before);

    void* ctx;
} ieee488_callbacks_t;

// Force the enums in 1 byte each, so that the need for atomic access is reduced.
// You may want to change that and use atomic access if you have a different architecture or compiler that does not guarantee atomic access to 1-byte variables.

typedef enum __attribute__((packed)) { IEEE488_SH_SIDS,
               IEEE488_SH_SGNS,
               IEEE488_SH_SDYS,
               IEEE488_SH_STRS,
               IEEE488_SH_SWNS,
               IEEE488_SH_SIWS } ieee488_sh_state_t; /** SH (Source Handshake) states */

typedef enum __attribute__((packed)) { IEEE488_AH_AIDS,
               IEEE488_AH_ANRS,
               IEEE488_AH_ACRS,
               IEEE488_AH_ACDS,
               IEEE488_AH_AWNS } ieee488_ah_state_t; /** AH (Acceptor Handshake) states */

typedef enum __attribute__((packed)) { IEEE488_T_TIDS,
               IEEE488_T_TADS,
               IEEE488_T_TACS,
               IEEE488_T_SPAS } ieee488_t_state_t; /** T (Talker) states */

typedef enum __attribute__((packed)) { IEEE488_L_LIDS,
               IEEE488_L_LADS,
               IEEE488_L_LACS } ieee488_l_state_t; /** L (Listener) states */

typedef enum __attribute__((packed)) { IEEE488_SR_NPRS,
               IEEE488_SR_SQRS,
               IEEE488_SR_APRS } ieee488_sr_state_t; /** SR (Service Request) states */

typedef enum __attribute__((packed)) { IEEE488_RL_LOCS,
               IEEE488_RL_LWLS,
               IEEE488_RL_REMS,
               IEEE488_RL_RWLS } ieee488_rl_state_t; /** RL (Remote Local) states */

typedef enum __attribute__((packed)) { IEEE488_PP_PPIS,
               IEEE488_PP_PPSS,
               IEEE488_PP_PPAS } ieee488_pp_state_t; /** PP (Parallel Poll) states */

/** @brief Internal state of an IEEE 488.1-1987 device. */
typedef struct ieee488_device {
    ieee488_config_t cfg;    // Configuration for the device.
    ieee488_callbacks_t cb;  // Callbacks for the device.

    volatile ieee488_sh_state_t sh;       // Source Handshake (SH) state.
    volatile ieee488_ah_state_t ah;       // Acceptor Handshake (AH) state.
    volatile ieee488_t_state_t talker;    // Talker (T) state.
    volatile ieee488_l_state_t listener;  // Listener (L) state.
    volatile ieee488_sr_state_t sr;       // Service Request (SR) state.
    volatile ieee488_rl_state_t rl;       // Remote Local (RL) state.
    volatile ieee488_pp_state_t pp;       // Parallel Poll (PP) state.

    bool serial_poll_mode;             // true if the device is in serial poll mode, false otherwise.
    bool talk_primary_addressed;       // true if the device is addressed as a talker, false otherwise.
    bool listen_primary_addressed;     // true if the device is addressed as a listener, false otherwise.
    bool pp_config_addressed;          // true if the device is addressed for parallel poll, false otherwise.
    bool pp_configured;                // true if the device is addressed for parallel poll configuration, false otherwise.
    uint8_t pp_line;                   // the current parallel poll line.
    bool pp_sense, individual_status;  // the sense value for the parallel poll lines and the individual status.
    bool service_pending;              // true if a service request is pending, false otherwise.

    bool tx_loaded;                                           // true if a byte is loaded for transmission, false otherwise.
    bool tx_end;                                              // true if the loaded byte is the last byte of the message, false otherwise.
    uint8_t tx_byte;                                          // the byte loaded for transmission.
    uint32_t deadline;                                        // the time by which the current operation must complete, in microseconds.
    uint32_t state_since;                                     // the time since the last state change, in microseconds.
    volatile uint8_t last_atn, last_eoi, last_ren;  // the last states of the interface signals.
    bool last_addressed;                                      // true if the device was last addressed, false otherwise.
    volatile uint8_t restart_loop;                            // true if the main loop should be restarted, false otherwise.
} ieee488_device_t;

extern ieee488_device_t ieee488_device;  // The global IEEE 488.1-1987 device instance.

/** @brief Initialize the device.
 * @param hal The hardware abstraction layer.
 * @param cfg The device configuration.
 * @param cb The device callbacks.
 */
void ieee488_init(const ieee488_config_t* cfg, const ieee488_callbacks_t* cb);

/** @brief Reset the device.
 *
 * Local power-on message (pon).
 */
void ieee488_reset(bool from_power_on);

/** @brief Poll an IEEE 488.1-1987 device for any activity.
 *
 * Call this frequently to allow the device to process bus events and perform any necessary actions.
 * @param d The device to poll.
 */
void ieee488_poll(void);

/* Local messages defined by Annex D. */

/** @brief Request service the device. (rsv)
 * @param request true to request service, false to clear the request.
 */
void ieee488_request_service(bool request);

/** @brief Return the device to local control. (rtl)
 */
void ieee488_return_to_local(void);

/** @brief Set the individual status of the device.
 * @param ist The individual status value.
 */
void ieee488_set_individual_status(bool ist);

/** @brief Set the parallel poll local configuration of the device.
 *
 * PP2-style local config
 * @param enabled true to enable parallel poll local configuration, false to disable.
 * @param line_1_to_8 The parallel poll lines to configure (1 to 8).
 * @param sense The sense value for the parallel poll lines.
 */
void ieee488_set_parallel_poll_local(bool enabled,
                                     uint8_t line_1_to_8, bool sense);

/** @brief Check if the device is a talker.
 * @return true if the device is a talker, false otherwise.
 */
bool ieee488_is_talker(void);

/** @brief Check if the device is a listener.
 * @return true if the device is a listener, false otherwise.
 */
bool ieee488_is_listener(void);

/** @brief Check if the device is in remote mode.
 * @return true if the device is in remote mode, false otherwise.
 */
bool ieee488_is_remote(void);

/** @brief Handle an ATN interrupt.
 *
 * This interrupt handler should be called when ATN changes state.
 * It is expected to be called from an interrupt context and must complete quickly to meet the timing requirements of the IEEE 488.1 standard.
 */
extern inline void ieee488_handle_atn_interrupt(void);

/** @brief Handle an IDY (ATN && EOI) interrupt.
 *
 * This interrupt handler should be called when (ATN && EOI) change state.
 * It is expected to be called from an interrupt context and must complete quickly to meet the timing requirements of the IEEE 488.1 standard.
 */
extern inline void ieee488_handle_idy_interrupt(void);

#ifdef __cplusplus
}
#endif
#endif
