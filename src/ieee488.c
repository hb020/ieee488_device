#include "ieee488.h"
#include "ieee488_hal.h"
#include <string.h>
#include <util/atomic.h>

ieee488_device_t ieee488_device;  // The global IEEE 488.1-1987 device instance.

/** @brief Get the current time in microseconds. 
 *
 * @return The current time in microseconds since startup, or 0 if the HAL does not provide a time function.
*/
static __attribute__((always_inline)) uint32_t now(void) { return TIME_US(); }

/** @brief Check if the handshake timer has expired.
 *
 * @return true if the handshake timer has expired, false otherwise.
 */
static __attribute__((always_inline)) bool expired(void) { return ieee488_device.cfg.handshake_timeout_us && (int32_t)(now() - ieee488_device.deadline) >= 0; }

/** @brief Arm the handshake timer with the configured timeout.
 * 
 * To be used with the expired() function to check for handshake timeouts.
 *
 */
static __attribute__((always_inline)) void arm(void) { ieee488_device.deadline = now() + ieee488_device.cfg.handshake_timeout_us; }

/** @brief Check if the given byte matches the device's primary address for listen.
 *
 * @param b The byte to check.
 * @return true if the byte matches the device's primary address for listen, false otherwise.
 */
static __attribute__((always_inline)) bool mla(uint8_t b) { return b == IEEE488_LAD(ieee488_device.cfg.primary_address); }

/** @brief Check if the given byte matches the device's primary address for talk.`
 *
 * @param b The byte to check.
 * @return true if the byte matches the device's primary address for talk, false otherwise.
 */
static __attribute__((always_inline)) bool mta(uint8_t b) { return b == IEEE488_TAD(ieee488_device.cfg.primary_address); }

/** @brief Check if the given byte matches the device's secondary address.
 *
 * @param b The byte to check.
 * @return true if the byte matches the device's secondary address, false otherwise.
 */
static __attribute__((always_inline)) bool msa(uint8_t b) { return (b & 0x60u) == 0x60u && (b & 0x1fu) == ieee488_device.cfg.secondary_address; }

/** @brief Check if the given byte is a primary command group byte.
 * @param b The byte to check.
 * @return true if the byte is a primary command group byte, false otherwise.
 */
static __attribute__((always_inline)) bool pcg(uint8_t b) { return (b & 0x60u) != 0x60u; }

/** @brief Set the Remote/Local state of the device and invoke the callback if it changes.
 *
 * @param s The new Remote/Local state to set.
 */
static __attribute__((always_inline)) void set_rl(ieee488_rl_state_t s) {
    if (ieee488_device.rl == s) return;
    ieee488_device.rl = s;
    bool remote = (s == IEEE488_RL_REMS || s == IEEE488_RL_RWLS);
    bool lockout = (s == IEEE488_RL_LWLS || s == IEEE488_RL_RWLS);
    if (ieee488_device.cb.remote_changed) ieee488_device.cb.remote_changed(remote, lockout);
}

/** @brief Decode an IEEE 488 command byte and update the device state accordingly.
 *
 * @param b The command byte to decode.
 */
static __attribute__((always_inline)) void decode_command(uint8_t b) {
    if (ieee488_device.restart_loop) return;
    if (ieee488_device.cb.command_seen) ieee488_device.cb.command_seen(b, true);
    bool addressed = (ieee488_device.listener == IEEE488_L_LADS);
    bool was_listener_addressed = (ieee488_device.listener == IEEE488_L_LADS || ieee488_device.listener == IEEE488_L_LACS);

    /* If PPC was seen for this addressed device, next SCG byte is PPE/PPD context,
       not extended secondary addressing. */
    bool pp_followup = ieee488_device.pp_config_addressed;

    /* T/TE, clauses 2.5.3.1-.8. */
    if (b == IEEE488_CMD_UNT || ((b & 0x60u) == 0x40u && !mta(b)) || mla(b)) ieee488_device.talker = IEEE488_T_TIDS;
    if (!ieee488_device.cfg.extended_address && mta(b)) ieee488_device.talker = IEEE488_T_TADS;
    if (ieee488_device.cfg.extended_address) {
        if (mta(b))
            ieee488_device.talk_primary_addressed = true;
        else if (pcg(b))
            ieee488_device.talk_primary_addressed = false;
        if (!pp_followup && ieee488_device.talk_primary_addressed && msa(b))
            ieee488_device.talker = IEEE488_T_TADS;
    }
    if (b == IEEE488_CMD_SPE) ieee488_device.serial_poll_mode = true;
    if (b == IEEE488_CMD_SPD) {
        ieee488_device.serial_poll_mode = false;
        ieee488_device.talker = IEEE488_T_TIDS;
        ieee488_device.talk_primary_addressed = false;
    }
    if (ieee488_device.serial_poll_mode && ((b & 0x60u) == 0x40u) && !mta(b)) {
        ieee488_device.listener = IEEE488_L_LIDS;
        ieee488_device.listen_primary_addressed = false;
    }

    /* L/LE, clauses 2.6.3.1-.5. */
    if (b == IEEE488_CMD_UNL || mta(b)) {
        ieee488_device.listener = IEEE488_L_LIDS;
        ieee488_device.listen_primary_addressed = false;
    }
    if (!ieee488_device.cfg.extended_address && mla(b)) ieee488_device.listener = IEEE488_L_LADS;
    if (ieee488_device.cfg.extended_address) {
        if (mla(b))
            ieee488_device.listen_primary_addressed = true;
        else if (pcg(b))
            ieee488_device.listen_primary_addressed = false;
        if (!pp_followup && ieee488_device.listen_primary_addressed && msa(b))
            ieee488_device.listener = IEEE488_L_LADS;
    }

    /* RL1, 2.8: enter remote on listen-addressing event while REN is asserted. */
    bool listener_addressed = (ieee488_device.listener == IEEE488_L_LADS || ieee488_device.listener == IEEE488_L_LACS);
    if (REN_IS_ASSERTED() && listener_addressed && !was_listener_addressed) {
        if (ieee488_device.rl == IEEE488_RL_LOCS)
            set_rl(IEEE488_RL_REMS);
        else if (ieee488_device.rl == IEEE488_RL_LWLS)
            set_rl(IEEE488_RL_RWLS);
    }

    /* Universal and addressed commands, 2.7-2.11 and 2.13. */
    ieee488_rl_state_t rl = ieee488_device.rl;
    switch (b) {
        case IEEE488_CMD_DCL:
            if (ieee488_device.cb.device_clear) ieee488_device.cb.device_clear(false);
            break;
        case IEEE488_CMD_SDC:
            if (addressed && ieee488_device.cb.device_clear) ieee488_device.cb.device_clear(true);
            break;
        case IEEE488_CMD_GET:
            if (addressed && ieee488_device.cb.device_trigger) ieee488_device.cb.device_trigger();
            break;
        case IEEE488_CMD_LLO:
            if (rl == IEEE488_RL_LOCS)
                set_rl(IEEE488_RL_LWLS);
            else if (rl == IEEE488_RL_REMS)
                set_rl(IEEE488_RL_RWLS);
            break;
        case IEEE488_CMD_GTL:
            if (addressed) {
                if (rl == IEEE488_RL_REMS)
                    set_rl(IEEE488_RL_LOCS);
                else if (rl == IEEE488_RL_RWLS)
                    set_rl(IEEE488_RL_LWLS);
            }
            break;
        case IEEE488_CMD_PPU:
            ieee488_device.pp_configured = false;
            ieee488_device.pp_config_addressed = false;
            break;
        case IEEE488_CMD_PPC:
            if (addressed) ieee488_device.pp_config_addressed = true;
            break;
        default:
            if (ieee488_device.pp_config_addressed && (b & 0x70u) == 0x60u) {
                ieee488_device.pp_configured = true;
                ieee488_device.pp_line = (uint8_t)((b & 7u) + 1u);
                ieee488_device.pp_sense = ((b >> 3) & 1u) != 0;
                ieee488_device.pp_config_addressed = false;
            } else if (ieee488_device.pp_config_addressed && (b & 0x70u) == 0x70u) {
                ieee488_device.pp_configured = false;
                ieee488_device.pp_config_addressed = false;
            } else if (pcg(b) && b != IEEE488_CMD_PPC)
                ieee488_device.pp_config_addressed = false;
            break;
    }
    if (ieee488_device.cb.command_seen) ieee488_device.cb.command_seen(b, false);
}

/** @brief Reset the IEEE 488 device to its initial state.
 */
void ieee488_reset(bool from_power_on) {
    bool notify_remote_local = false;
    bool notify_addressed_changed = false;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        ieee488_device.sh = IEEE488_SH_SIDS;
        ieee488_device.ah = IEEE488_AH_AIDS;
        ieee488_device.talker = IEEE488_T_TIDS;
        notify_addressed_changed = (ieee488_device.listener != IEEE488_L_LIDS);
        ieee488_device.listener = IEEE488_L_LIDS;
        ieee488_device.sr = IEEE488_SR_NPRS;
        notify_remote_local = (ieee488_device.rl != IEEE488_RL_LOCS);
        ieee488_device.rl = IEEE488_RL_LOCS;
        ieee488_device.pp = IEEE488_PP_PPIS;
        ieee488_device.serial_poll_mode = false;
        ieee488_device.talk_primary_addressed = false;
        ieee488_device.listen_primary_addressed = false;
        ieee488_device.pp_config_addressed = false;
        ieee488_device.pp_configured = false;
        ieee488_device.service_pending = false;
        ieee488_device.tx_loaded = false;
        ieee488_device.restart_loop = false;
        DRIVE_DIO(0, false);
        DAV_RELEASE();
        NRFD_ASSERT();
        NDAC_ASSERT();
        SRQ_RELEASE();
        EOI_RELEASE();
        ieee488_device.last_atn = ATN_IS_ASSERTED();
        ieee488_device.last_eoi = EOI_IS_ASSERTED();
        ieee488_device.last_ren = REN_IS_ASSERTED();
    }

    if (from_power_on) {
        notify_remote_local = true;
        notify_addressed_changed = true;
    }
    if (notify_remote_local && ieee488_device.cb.remote_changed)
        ieee488_device.cb.remote_changed(false, false);
    if (notify_addressed_changed && ieee488_device.cb.addressed_changed)
        ieee488_device.cb.addressed_changed(false);
}

/** @brief Initialize the IEEE 488 device with the given HAL, configuration, and callbacks.
 * @param c The configuration for the device.
 * @param cb The callbacks for the device.
 */
void ieee488_init(const ieee488_config_t* c, const ieee488_callbacks_t* cb) {
    memset(&ieee488_device, 0, sizeof(ieee488_device));
    ieee488_device.cfg = *c;
    if (cb) ieee488_device.cb = *cb;
    hal_init();
    ieee488_reset(true);
}

/** @brief Request or clear a service request.
 *
 * @param request true to request service, false to clear it.
 */
void ieee488_request_service(bool request) {
    ieee488_device.service_pending = request;
    if (request && ieee488_device.sr == IEEE488_SR_NPRS) ieee488_device.sr = IEEE488_SR_SQRS;
    if (!request) ieee488_device.sr = IEEE488_SR_NPRS;
}

/** @brief Return the device to local control.
 *
 */
void ieee488_return_to_local(void) {
    if (ieee488_device.rl == IEEE488_RL_REMS)
        set_rl(IEEE488_RL_LOCS);
}

/** @brief Set the individual status flag.
 *
 * @param v The value to set.
 */
void ieee488_set_individual_status(bool v) { ieee488_device.individual_status = v; }

/** @brief Configure the parallel poll local settings.
 *
 * @param en Enable or disable parallel poll local.
 * @param line The DIO line to use (1-8).
 * @param sense The sense value for the parallel poll.
 */
void ieee488_set_parallel_poll_local(bool en, uint8_t line, bool sense) {
    if (line < 1 || line > 8) return;
    ieee488_device.pp_configured = en;
    ieee488_device.pp_line = line;
    ieee488_device.pp_sense = sense;
}
/** @brief Check if the device is a talker.
 *
 * @return true if the device is a talker, false otherwise.
 */
bool ieee488_is_talker(void) { return ieee488_device.talker == IEEE488_T_TACS || ieee488_device.talker == IEEE488_T_SPAS; }

/** @brief Check if the device is a listener.
 *
 * @return true if the device is a listener, false otherwise.
 */
bool ieee488_is_listener(void) { return ieee488_device.listener == IEEE488_L_LACS; }

/** @brief Check if the device is in remote mode.
 *
 * @return true if the device is in remote mode, false otherwise.
 */
bool ieee488_is_remote(void) { return ieee488_device.rl == IEEE488_RL_REMS || ieee488_device.rl == IEEE488_RL_RWLS; }

/** @brief Force the acceptor to the idle state and release NRFD and NDAC lines.
 * 
 * To be called only by `acceptor_force_idle()` or the interrupt handler
 * 
 * @param idy true if the device is in IDY state (ATN and EOI asserted), false otherwise.
 */
static __attribute__((always_inline)) void acceptor_force_idle_raw(bool idy) {
    /* Not participating in handshake: do not hold shared listener lines. */
    if (!idy) {
        NDAC_RELEASE();
        NRFD_RELEASE();
    }
    ieee488_device.ah = IEEE488_AH_AIDS;
}

/** @brief Force the acceptor to the idle state and release AH lines.
 * 
 * To be called only by the main loop
 */
static __attribute__((always_inline)) void acceptor_force_idle(void) {
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        if (ieee488_device.restart_loop) return;
        acceptor_force_idle_raw(IDY_IS_ASSERTED());
    }
}

/** @brief Handle the acceptor state machine.
 *
 * @param atn The state of the ATN line.
 */
static void acceptor(bool atn) {
    /* AH1, 2.4: handshake every command byte and data only while LACS. */
    bool accept;
    bool dav;
    bool eoi;
    ieee488_ah_state_t ah;
    bool restart_loop;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        restart_loop = ieee488_device.restart_loop;
        accept = atn || ieee488_device.listener == IEEE488_L_LACS;
        dav = DAV_IS_ASSERTED();
        eoi = EOI_IS_ASSERTED();
        ah = ieee488_device.ah;
    }
    if (restart_loop) return;
    if (!accept) {
        acceptor_force_idle();
        return;
    }

    switch (ah) {
        case IEEE488_AH_AIDS:
        case IEEE488_AH_ANRS:
            ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
                if (ieee488_device.restart_loop) return;
                NDAC_ASSERT();
                NRFD_RELEASE();
                ieee488_device.ah = IEEE488_AH_ACRS;
                arm();
            }
            break;

        case IEEE488_AH_ACRS:
            /* No timeout here: waiting for DAV start is an idle condition, not an in-progress byte. */
            if (dav) {
                // TODO add delay section here based on ieee488_device.cfg.rx_delay_us if > 0, and !atn                
                ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
                    if (ieee488_device.restart_loop) return;
                    NRFD_ASSERT();
                }
                uint8_t b = READ_DIO();
                if (ieee488_device.restart_loop) return;
                if (atn)
                    decode_command(b);
                else if (ieee488_device.cb.rx_byte) {
                    bool is_end = eoi || (ieee488_device.cfg.eos_enabled && b == ieee488_device.cfg.eos_byte);
                    ieee488_device.cb.rx_byte(b, is_end);
                }
                ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
                    if (ieee488_device.restart_loop) return;
                    NDAC_RELEASE();
                    ieee488_device.ah = IEEE488_AH_ACDS;
                    arm();
                }
            }
            break;

        case IEEE488_AH_ACDS:
            if (!dav) {
                ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
                    if (ieee488_device.restart_loop) return;
                    NDAC_ASSERT();
                    NRFD_RELEASE();
                    ieee488_device.ah = IEEE488_AH_ACRS;
                    arm();
                }
            } else if (expired()) {
                acceptor_force_idle();
            }
            break;

        default:
            acceptor_force_idle();
            break;
    }
}

/** @brief Force the source to the idle state: DAV and EOI unasserted, DIO lines low, and source state machine reset.
 *
 * To be called only by `source_force_idle()` or the interrupt handler
 * 
 * @param drop_tx true to drop the current transmit byte, false to keep it.
 */
static __attribute__((always_inline)) void source_force_idle_raw(bool drop_tx, bool idy) {
    DAV_RELEASE();
    EOI_RELEASE();
    if (!idy) {
        DRIVE_DIO(0, false);
    }
    ieee488_device.sh = IEEE488_SH_SIDS;
    if (drop_tx) ieee488_device.tx_loaded = false;
}

/** @brief Force the source to the idle state: DAV and EOI unasserted, DIO lines low, and source state machine reset.
 *
 * To be called only by the main loop
 * 
 * @param drop_tx true to drop the current transmit byte, false to keep it.
 */
static __attribute__((always_inline)) void source_force_idle(bool drop_tx) {
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        if (ieee488_device.restart_loop) return;
        source_force_idle_raw(drop_tx, IDY_IS_ASSERTED());
    }
}

/** @brief Handle the source state machine.
 *
 * @param atn The state of the ATN line.
 */
static void source(bool atn) {
    ieee488_t_state_t talker;
    ieee488_sh_state_t sh;
    bool ndac;
    bool nrfd;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        if (ieee488_device.restart_loop) return;
        talker = ieee488_device.talker;
        sh = ieee488_device.sh;
        ndac = NDAC_IS_ASSERTED();
        nrfd = NRFD_IS_ASSERTED();
    }
    if (ieee488_device.restart_loop) return;

    bool active = !atn && (talker == IEEE488_T_TACS || talker == IEEE488_T_SPAS);
    if (!active) {
        source_force_idle(true);
        return;
    }
    switch (sh) {
        case IEEE488_SH_SIDS:
            if (ieee488_device.restart_loop) return;
            if (!ieee488_device.tx_loaded) {
                if (ieee488_device.talker == IEEE488_T_SPAS) {
                    uint8_t s = ieee488_device.cb.status_byte ? ieee488_device.cb.status_byte() : 0;
                    ieee488_device.tx_byte = (uint8_t)((s & 0xBFu) | ((ieee488_device.sr == IEEE488_SR_APRS) ? 0x40u : 0));
                    ieee488_device.tx_end = true;
                    ieee488_device.tx_loaded = true;
                } else if (ieee488_device.cb.tx_next)
                    // TODO add delay section here based on ieee488_device.cfg.tx_delay_us if > 0
                    ieee488_device.tx_loaded = ieee488_device.cb.tx_next(&ieee488_device.tx_byte, &ieee488_device.tx_end);
            }
            if (ieee488_device.tx_loaded) {
                if (ieee488_device.restart_loop) return;
                DRIVE_DIO(ieee488_device.tx_byte, true);
                ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
                    if (ieee488_device.restart_loop) return;
                
                    if (ieee488_device.cfg.use_eoi && ieee488_device.tx_end) {
                        EOI_ASSERT();
                    } else {
                        EOI_RELEASE();
                    }
                    ieee488_device.state_since = now();
                    ieee488_device.sh = IEEE488_SH_SDYS;
                    arm();
                }
            }
            break;

        case IEEE488_SH_SDYS:
            if (ieee488_device.restart_loop) return;
            if (!nrfd && (uint32_t)(now() - ieee488_device.state_since) >= ieee488_device.cfg.t1_delay_us) {
                ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
                    if (ieee488_device.restart_loop) return;
                    DAV_ASSERT();
                    ieee488_device.sh = IEEE488_SH_STRS;
                    arm();
                }
            } else if (expired()) {
                source_force_idle(true);
            }
            break;

        case IEEE488_SH_STRS:
            if (ieee488_device.restart_loop) return;
            if (!ndac) {
                ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
                    if (ieee488_device.restart_loop) return;
                    DAV_RELEASE();
                    ieee488_device.sh = IEEE488_SH_SWNS;
                    arm();
                }
            } else if (expired()) {
                source_force_idle(true);
            }
            break;

        case IEEE488_SH_SWNS:
            if (ieee488_device.restart_loop) return;
            if (ndac) {
                ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
                    if (ieee488_device.restart_loop) return;
                    ieee488_device.tx_loaded = false;
                    ieee488_device.sh = IEEE488_SH_SIDS;
                    if (ieee488_device.talker == IEEE488_T_SPAS && ieee488_device.sr == IEEE488_SR_APRS) {
                        ieee488_device.sr = IEEE488_SR_NPRS;
                    }
                    ieee488_device.service_pending = false;
                }
            } else if (expired()) {
                source_force_idle(true);
            }
            break;

        default:
            source_force_idle(true);
            break;
    }
}


// Time critical requirements, see table 39 in IEEE 488.1-1987.pdf
// most strict is reaction to ATN (200ns max)
// 
//    | Function             | Description                  | Time value
// ---+----------------------+------------------------------+------------
// t2 | SH, AH, T, L, LE, TE | response to ATN              | <= 200ns
// t4 | T,TE,L,LE,C,RL       | response to IFC or REN false | <100 µs
// t5 | PP                   | response to ATN ^ EOI        | <= 200ns
//
// ATN and EOI reactions are handled in an interrupt handler for t2/t5 compliance.

/** @brief Handle the ATN state change and update the device state accordingly.
 * 
 * Only to be called when the ATN line changes. 
 * If ATN interrupt handling is enabled, this function is called only by the interrupt handler.
 * 
 * It can modify:
 * 
 *  - restart_loop. If true, the main loop should be restarted to handle the new state.
 * 
 *  - last_atn. The last state of the ATN line.
 * 
 *  - sh, to SIDS. If so, then it will have modified DAV, EOI, and the data lines.
 * 
 *  - ah, to AIDS. If so, then it will have modified NRFD and NDAC.
 * 
 *  - pp, to PPAS. If so, then it will have modified NRFD and NDAC, and the data lines.
 * 
 *  - pp, to PPSS. If so, then it will have modified the data lines.
 * 
 *  - talker, to TADS, SPAS or TACS
 * 
 *  - listener, to LADS or LACS
 */
static __attribute__((always_inline)) void atn_handler(void) {
    // Get ATN state
    bool atn = ATN_IS_ASSERTED();
    bool restart_loop = false;

    // START ppoll section
    // a slight delay later, get EOI state, to see if ATN and EOI are asserted at the same time
    bool idy = false;
    if (atn && EOI_IS_ASSERTED()) {
        idy = true;
        /* t5: PP state transition on ATN ^ EOI */
        if (ieee488_device.pp_configured) {
            ieee488_device.pp = IEEE488_PP_PPAS;
            uint8_t v = (ieee488_device.individual_status == ieee488_device.pp_sense) ? (uint8_t)(1u << (ieee488_device.pp_line - 1u)) : 0;
            DRIVE_DIO(v, v != 0);
            NRFD_ASSERT();
            NDAC_ASSERT();            
        }
    }
    if (!atn && ieee488_device.pp == IEEE488_PP_PPAS) {
        ieee488_device.pp = IEEE488_PP_PPSS;
        DRIVE_DIO(0, false);
    }
    // END ppoll section

    // TODO: you may want to mix in the IFC reaction

    // t2: SH, AH, T, L, LE, TE state transition on ATN
    if (atn) {
        // ATN asserted: force source/acceptor to idle
        if (ieee488_device.sh != IEEE488_SH_SIDS) {
            source_force_idle_raw(true, idy);
            restart_loop = true;  // restart the main loop to handle the new state
        }
        if (ieee488_device.ah != IEEE488_AH_AIDS) {
            // Move to the AH idle state. Assert NDAC, because that is what the acceptor section does. 
            acceptor_force_idle_raw(true);
            NDAC_ASSERT();
            NRFD_RELEASE();
            restart_loop = true;  // restart the main loop to handle the new state
        }
        /* ATN asserted: transition active talker/listener to addressed */
        if (ieee488_device.talker == IEEE488_T_TACS || ieee488_device.talker == IEEE488_T_SPAS) {
            ieee488_device.talker = IEEE488_T_TADS;
            restart_loop = true;  // restart the main loop to handle the new state
        }
        if (ieee488_device.listener == IEEE488_L_LACS) {
            ieee488_device.listener = IEEE488_L_LADS;
            restart_loop = true;  // restart the main loop to handle the new state
        }
    } else {
        /* ATN released: transition addressed talker/listener to active */
        if (ieee488_device.talker == IEEE488_T_TADS) {
            ieee488_device.talker = ieee488_device.serial_poll_mode ? IEEE488_T_SPAS : IEEE488_T_TACS;
            restart_loop = true;  // restart the main loop to handle the new state
        }
        if (ieee488_device.listener == IEEE488_L_LADS) {
            ieee488_device.listener = IEEE488_L_LACS;
            restart_loop = true;  // restart the main loop to handle the new state
        }
    }
    ieee488_device.last_atn = atn;
    if (restart_loop) {
        ieee488_device.restart_loop = true;
    }
}

/** @brief Handle an ATN interrupt.
 *
 * This interrupt handler should be called when ATN changes state.
 * It is expected to be called from an interrupt context and must complete quickly to meet the timing requirements of the IEEE 488.1 standard.
 */
void __attribute__((always_inline)) ieee488_handle_atn_interrupt(void) {
    atn_handler();
}

/** @brief Poll the IEEE 488 device states, and act upon them
 * 
 * The most time sensitive tasks are done in the interrupt handler.
 * This function is called in the main loop, and handles the rest of the state machine. 
 * Do NOT use blocking calls in this function, as it is expected to be called frequently and must complete quickly.
 * 
 */
void ieee488_poll(void) {
    
    // get the values from the interrupt handler, which should have been called on ATN or DAV change
    bool atn = false;
    bool restart_loop = false;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        atn = ieee488_device.last_atn;
        restart_loop = ieee488_device.restart_loop;
    }
    if (restart_loop) {
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
            ieee488_device.restart_loop = false;
        }
    }

    // reset IDY if needed
    if (ieee488_device.pp_configured) {
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
            if (ieee488_device.pp == IEEE488_PP_PPAS && !IDY_IS_ASSERTED()) {
                ieee488_device.pp = IEEE488_PP_PPSS;
                DRIVE_DIO(0, false);
            }
        }
    }

    // This section is fast, and makes no change to the pins, or calls anything, but it makes changes to talker and listener, so run in a block
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        if (ieee488_device.restart_loop) return;

        bool ifc = IFC_IS_ASSERTED();
        if (ifc) { /* IFC: T and L return idle within t4, 2.5/2.6; serial poll reset. */
            ieee488_device.talker = IEEE488_T_TIDS;
            ieee488_device.listener = IEEE488_L_LIDS;
            ieee488_device.serial_poll_mode = false;
            ieee488_device.talk_primary_addressed = false;
            ieee488_device.listen_primary_addressed = false;
        }
        if (ieee488_device.cfg.talk_only && !ifc) ieee488_device.talker = IEEE488_T_TADS;
        if (ieee488_device.cfg.listen_only && !ifc) ieee488_device.listener = IEEE488_L_LADS;
    }

    if (ieee488_device.restart_loop) return;

    /* RL1, 2.8: MLA while REN enters remote; REN false returns local unless locked out. */
    bool ren = REN_IS_ASSERTED();
    bool ren_rise = false;
    ren_rise = ren && !ieee488_device.last_ren;
    ieee488_device.last_ren = ren;
    // Listener state can have been modified by the INTR handler
    ieee488_l_state_t listener_state;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        listener_state = ieee488_device.listener;
    }
    if (!ren) {
        if (ieee488_device.rl == IEEE488_RL_REMS)
            set_rl(IEEE488_RL_LOCS);
        else if (ieee488_device.rl == IEEE488_RL_RWLS)
            set_rl(IEEE488_RL_LWLS);
    } else if (ren_rise && (listener_state == IEEE488_L_LADS || listener_state == IEEE488_L_LACS)) {
        if (ieee488_device.rl == IEEE488_RL_LOCS)
            set_rl(IEEE488_RL_REMS);
        else if (ieee488_device.rl == IEEE488_RL_LWLS)
            set_rl(IEEE488_RL_RWLS);
    }

    if (ieee488_device.restart_loop) return;

    // Talker state can have been modified by the INTR handler
    ieee488_t_state_t talker_state;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        talker_state = ieee488_device.talker;
    }
    if (ieee488_device.restart_loop) return;

    /* SR1, 2.7: SRQ asserted in SQRS; serial poll response latches APRS. */
    if (ieee488_device.service_pending && ieee488_device.sr == IEEE488_SR_NPRS) ieee488_device.sr = IEEE488_SR_SQRS;
    if (talker_state == IEEE488_T_SPAS && ieee488_device.sr == IEEE488_SR_SQRS) ieee488_device.sr = IEEE488_SR_APRS;
    if (ieee488_device.sr == IEEE488_SR_SQRS) {
        SRQ_ASSERT();
    } else {
        SRQ_RELEASE();
    }

    if (ieee488_device.restart_loop) return;

    /* During a parallel poll AH must not interpret DIO as a command byte. */
    if (!IDY_IS_ASSERTED())
        acceptor(atn);

    source(atn);

    // TODO: if you support multiple addresses, then this will need to move elsewhere
    if (ieee488_device.cb.addressed_changed) {
        bool addressed = false;
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
            addressed = (ieee488_device.listener == IEEE488_L_LADS || ieee488_device.listener == IEEE488_L_LACS);
        }
        if (addressed != ieee488_device.last_addressed) {
            ieee488_device.last_addressed = addressed;
            ieee488_device.cb.addressed_changed(addressed);
        }
    }
}
