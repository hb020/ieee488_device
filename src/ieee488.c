#include "ieee488.h"
#include <string.h>

#define R(d, l) ((d)->hal.read_line((d)->hal.ctx, (l)))
#define W(d, l, v) ((d)->hal.drive_line((d)->hal.ctx, (l), (v)))
static uint32_t now(ieee488_device_t* d) { return d->hal.time_us ? d->hal.time_us(d->hal.ctx) : 0; }
static bool expired(ieee488_device_t* d) { return d->cfg.handshake_timeout_us && (int32_t)(now(d) - d->deadline) >= 0; }
static void arm(ieee488_device_t* d) { d->deadline = now(d) + d->cfg.handshake_timeout_us; }
static bool mla(ieee488_device_t* d, uint8_t b) { return b == IEEE488_LAD(d->cfg.primary_address); }
static bool mta(ieee488_device_t* d, uint8_t b) { return b == IEEE488_TAD(d->cfg.primary_address); }
static bool msa(ieee488_device_t* d, uint8_t b) { return (b & 0x60u) == 0x60u && (b & 0x1fu) == d->cfg.secondary_address; }
static bool pcg(uint8_t b) { return (b & 0x60u) != 0x60u; }

static void set_rl(ieee488_device_t* d, ieee488_rl_state_t s) {
    if (d->rl == s) return;
    d->rl = s;
    if (d->cb.remote_changed) d->cb.remote_changed(d->cb.ctx, s == IEEE488_RL_REMS || s == IEEE488_RL_RWLS, s == IEEE488_RL_LWLS || s == IEEE488_RL_RWLS);
}

static void decode_command(ieee488_device_t* d, uint8_t b) {
    bool addressed = (d->listener == IEEE488_L_LADS);
    if (d->cb.command_seen) d->cb.command_seen(d->cb.ctx, b);

    /* If PPC was seen for this addressed device, next SCG byte is PPE/PPD context,
       not extended secondary addressing. */
    bool pp_followup = d->pp_config_addressed;

    /* T/TE, clauses 2.5.3.1-.8. */
    if (b == IEEE488_CMD_UNT || ((b & 0x60u) == 0x40u && !mta(d, b)) || mla(d, b)) d->talker = IEEE488_T_TIDS;
    if (d->cfg.address_mode == IEEE488_ADDR_NORMAL && mta(d, b)) d->talker = IEEE488_T_TADS;
    if (d->cfg.address_mode == IEEE488_ADDR_EXTENDED) {
        if (mta(d, b))
            d->talk_primary_addressed = true;
        else if (pcg(b))
            d->talk_primary_addressed = false;
        if (!pp_followup && d->talk_primary_addressed && msa(d, b))
            d->talker = IEEE488_T_TADS;
    }
    if (b == IEEE488_CMD_SPE) d->serial_poll_mode = true;
    if (b == IEEE488_CMD_SPD) d->serial_poll_mode = false;

    /* L/LE, clauses 2.6.3.1-.5. */
    if (b == IEEE488_CMD_UNL || mta(d, b)) d->listener = IEEE488_L_LIDS;
    if (d->cfg.address_mode == IEEE488_ADDR_NORMAL && mla(d, b)) d->listener = IEEE488_L_LADS;
    if (d->cfg.address_mode == IEEE488_ADDR_EXTENDED) {
        if (mla(d, b))
            d->listen_primary_addressed = true;
        else if (pcg(b))
            d->listen_primary_addressed = false;
        if (!pp_followup && d->listen_primary_addressed && msa(d, b))
            d->listener = IEEE488_L_LADS;
    }

    /* Universal and addressed commands, 2.7-2.11 and 2.13. */
    switch (b) {
        case IEEE488_CMD_DCL:
            if (d->cb.device_clear) d->cb.device_clear(d->cb.ctx, false);
            break;
        case IEEE488_CMD_SDC:
            if (addressed && d->cb.device_clear) d->cb.device_clear(d->cb.ctx, true);
            break;
        case IEEE488_CMD_GET:
            if (addressed && d->cb.device_trigger) d->cb.device_trigger(d->cb.ctx);
            break;
        case IEEE488_CMD_LLO:
            if (d->rl == IEEE488_RL_LOCS)
                set_rl(d, IEEE488_RL_LWLS);
            else if (d->rl == IEEE488_RL_REMS)
                set_rl(d, IEEE488_RL_RWLS);
            break;
        case IEEE488_CMD_GTL:
            if (addressed) {
                if (d->rl == IEEE488_RL_REMS)
                    set_rl(d, IEEE488_RL_LOCS);
                else if (d->rl == IEEE488_RL_RWLS)
                    set_rl(d, IEEE488_RL_LWLS);
            }
            break;
        case IEEE488_CMD_PPU:
            d->pp_configured = false;
            d->pp_config_addressed = false;
            break;
        case IEEE488_CMD_PPC:
            if (addressed) d->pp_config_addressed = true;
            break;
        default:
            if (d->pp_config_addressed && (b & 0x70u) == 0x60u) {
                d->pp_configured = true;
                d->pp_line = (uint8_t)((b & 7u) + 1u);
                d->pp_sense = ((b >> 3) & 1u) != 0;
                d->pp_config_addressed = false;
            } else if (d->pp_config_addressed && (b & 0x70u) == 0x70u) {
                d->pp_configured = false;
                d->pp_config_addressed = false;
            } else if (pcg(b) && b != IEEE488_CMD_PPC)
                d->pp_config_addressed = false;
            break;
    }
}

void ieee488_reset(ieee488_device_t* d) {
    d->sh = IEEE488_SH_SIDS;
    d->ah = IEEE488_AH_AIDS;
    d->talker = IEEE488_T_TIDS;
    d->listener = IEEE488_L_LIDS;
    d->sr = IEEE488_SR_NPRS;
    set_rl(d, IEEE488_RL_LOCS);
    d->pp = IEEE488_PP_PPIS;
    d->serial_poll_mode = false;
    d->talk_primary_addressed = false;
    d->listen_primary_addressed = false;
    d->pp_config_addressed = false;
    d->pp_configured = false;
    d->service_pending = false;
    d->tx_loaded = false;
    d->hal.drive_dio(d->hal.ctx, 0, false);
    W(d, IEEE488_DAV, false);
    W(d, IEEE488_NRFD, true);
    W(d, IEEE488_NDAC, true);
    W(d, IEEE488_SRQ, false);
    W(d, IEEE488_EOI, false);
}

void ieee488_init(ieee488_device_t* d, const ieee488_hal_t* h, const ieee488_config_t* c, const ieee488_callbacks_t* cb) {
    memset(d, 0, sizeof(*d));
    d->hal = *h;
    d->cfg = *c;
    if (cb) d->cb = *cb;
    ieee488_reset(d);
    d->last_ifc = R(d, IEEE488_IFC);
    d->last_atn = R(d, IEEE488_ATN);
    d->last_dav = R(d, IEEE488_DAV);
}

void ieee488_request_service(ieee488_device_t* d, bool request) {
    d->service_pending = request;
    if (request && d->sr == IEEE488_SR_NPRS) d->sr = IEEE488_SR_SQRS;
    if (!request) d->sr = IEEE488_SR_NPRS;
}
void ieee488_return_to_local(ieee488_device_t* d) {
    if (d->rl == IEEE488_RL_REMS)
        set_rl(d, IEEE488_RL_LOCS);
    else if (d->rl == IEEE488_RL_RWLS)
        set_rl(d, IEEE488_RL_LWLS);
}
void ieee488_set_individual_status(ieee488_device_t* d, bool v) { d->individual_status = v; }
void ieee488_set_parallel_poll_local(ieee488_device_t* d, bool en, uint8_t line, bool sense) {
    if (line < 1 || line > 8) return;
    d->pp_configured = en;
    d->pp_line = line;
    d->pp_sense = sense;
}
bool ieee488_is_talker(const ieee488_device_t* d) { return d->talker == IEEE488_T_TACS || d->talker == IEEE488_T_SPAS; }
bool ieee488_is_listener(const ieee488_device_t* d) { return d->listener == IEEE488_L_LACS; }
bool ieee488_is_remote(const ieee488_device_t* d) { return d->rl == IEEE488_RL_REMS || d->rl == IEEE488_RL_RWLS; }

static void acceptor_force_idle(ieee488_device_t* d) {
    /* Deterministic AH idle: not ready to accept, not accepted. */
    W(d, IEEE488_NRFD, true);
    W(d, IEEE488_NDAC, true);
    d->ah = IEEE488_AH_AIDS;
}

static void acceptor(ieee488_device_t* d, bool atn) {
    bool dav = R(d, IEEE488_DAV);
    /* AH1, 2.4: handshake every command byte and data only while LACS. */
    bool accept = atn || d->listener == IEEE488_L_LACS;
    if (!accept) {
        acceptor_force_idle(d);
        return;
    }

    switch (d->ah) {
        case IEEE488_AH_AIDS:
        case IEEE488_AH_ANRS:
            W(d, IEEE488_NDAC, true);
            W(d, IEEE488_NRFD, false);
            d->ah = IEEE488_AH_ACRS;
            arm(d);
            break;

        case IEEE488_AH_ACRS:
            if (dav) {
                W(d, IEEE488_NRFD, true);
                uint8_t b = d->hal.read_dio(d->hal.ctx);
                if (atn)
                    decode_command(d, b);
                else if (d->cb.rx_byte)
                    d->cb.rx_byte(d->cb.ctx, b, R(d, IEEE488_EOI) || (d->cfg.eos_enabled && b == d->cfg.eos_byte));
                W(d, IEEE488_NDAC, false);
                d->ah = IEEE488_AH_ACDS;
                arm(d);
            } else if (expired(d)) {
                acceptor_force_idle(d);
            }
            break;

        case IEEE488_AH_ACDS:
            if (!dav) {
                W(d, IEEE488_NDAC, true);
                W(d, IEEE488_NRFD, false);
                d->ah = IEEE488_AH_ACRS;
                arm(d);
            } else if (expired(d)) {
                acceptor_force_idle(d);
            }
            break;

        default:
            acceptor_force_idle(d);
            break;
    }
}

static void source_force_idle(ieee488_device_t* d, bool drop_tx) {
    W(d, IEEE488_DAV, false);
    W(d, IEEE488_EOI, false);
    d->hal.drive_dio(d->hal.ctx, 0, false);
    d->sh = IEEE488_SH_SIDS;
    if (drop_tx) d->tx_loaded = false;
}

static void source(ieee488_device_t* d, bool atn) {
    bool active = !atn && (d->talker == IEEE488_T_TACS || d->talker == IEEE488_T_SPAS);
    if (!active) {
        source_force_idle(d, true);
        return;
    }
    switch (d->sh) {
        case IEEE488_SH_SIDS:
            if (!d->tx_loaded) {
                if (d->talker == IEEE488_T_SPAS) {
                    uint8_t s = d->cb.status_byte ? d->cb.status_byte(d->cb.ctx) : 0;
                    d->tx_byte = (uint8_t)((s & 0xBFu) | ((d->sr == IEEE488_SR_APRS) ? 0x40u : 0));
                    d->tx_end = true;
                    d->tx_loaded = true;
                } else if (d->cb.tx_next)
                    d->tx_loaded = d->cb.tx_next(d->cb.ctx, &d->tx_byte, &d->tx_end);
            }
            if (d->tx_loaded) {
                d->hal.drive_dio(d->hal.ctx, d->tx_byte, true);
                W(d, IEEE488_EOI, d->cfg.use_eoi && d->tx_end);
                d->state_since = now(d);
                d->sh = IEEE488_SH_SDYS;
                arm(d);
            }
            break;

        case IEEE488_SH_SDYS:
            if (!R(d, IEEE488_NRFD) && (uint32_t)(now(d) - d->state_since) >= d->cfg.t1_delay_us) {
                W(d, IEEE488_DAV, true);
                d->sh = IEEE488_SH_STRS;
                arm(d);
            } else if (expired(d)) {
                source_force_idle(d, true);
            }
            break;

        case IEEE488_SH_STRS:
            if (!R(d, IEEE488_NDAC)) {
                W(d, IEEE488_DAV, false);
                d->sh = IEEE488_SH_SWNS;
                arm(d);
            } else if (expired(d)) {
                source_force_idle(d, true);
            }
            break;

        case IEEE488_SH_SWNS:
            if (R(d, IEEE488_NDAC)) {
                d->tx_loaded = false;
                d->sh = IEEE488_SH_SIDS;
                if (d->talker == IEEE488_T_SPAS && d->sr == IEEE488_SR_APRS) {
                    d->sr = IEEE488_SR_NPRS;
                    d->service_pending = false;
                }
            } else if (expired(d)) {
                source_force_idle(d, true);
            }
            break;

        default:
            source_force_idle(d, true);
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

static void atn_interrupt_handler(ieee488_device_t* d, bool atn, bool eoi) {
    /* t2, t5: immediate state changes on ATN + EOI transition (must be called from ISR) */
    // t2: SH, AH, T, L, LE, TE state transition on ATN
    if (!atn) {
        /* ATN released: transition addressed talker/listener to active */
        if (d->talker == IEEE488_T_TADS) d->talker = d->serial_poll_mode ? IEEE488_T_SPAS : IEEE488_T_TACS;
        if (d->listener == IEEE488_L_LADS) d->listener = IEEE488_L_LACS;
    } else {
        /* ATN asserted: transition active talker/listener to addressed */
        if (d->talker == IEEE488_T_TACS || d->talker == IEEE488_T_SPAS) d->talker = IEEE488_T_TADS;
        if (d->listener == IEEE488_L_LACS) d->listener = IEEE488_L_LADS;
    }
    /* Force source and acceptor to idle on ATN assertion */
    if (atn) {
        source_force_idle(d, true);
        acceptor_force_idle(d);
    }

    /* t5: PP state transition on ATN ^ EOI */

    bool idy = atn && eoi;
    if (idy && d->pp_configured) {
        d->pp = IEEE488_PP_PPAS;
        uint8_t v = (d->individual_status == d->pp_sense) ? (uint8_t)(1u << (d->pp_line - 1u)) : 0;
        d->hal.drive_dio(d->hal.ctx, v, v != 0);
    } else if (d->pp == IEEE488_PP_PPAS && !idy) {
        d->pp = IEEE488_PP_PPSS;
        d->hal.drive_dio(d->hal.ctx, 0, false);
    }
    if (idy) {
        W(d, IEEE488_NRFD, true);
        W(d, IEEE488_NDAC, true);
    }
}


// This interrupt handler is called when either ATN or EOI changes state.
// It is expected to be called from an interrupt context, and must complete quickly to meet the timing requirements of the IEEE 488.1 standard.
void ieee488_handle_interrupt(ieee488_device_t* d) {
    bool atn = R(d, IEEE488_ATN);
    bool eoi = R(d, IEEE488_EOI);
    if ((atn != d->last_atn) || (eoi != d->last_eoi)) {
        d->last_atn = atn;
        d->last_eoi = eoi;
        atn_interrupt_handler(d, atn, eoi);
    }
}

// TODO make sure that whatever the interrupt handler controls, is compatible with the rest of the code

void ieee488_poll(ieee488_device_t* d) {
    bool ifc = R(d, IEEE488_IFC), atn = R(d, IEEE488_ATN), ren = R(d, IEEE488_REN), eoi = R(d, IEEE488_EOI);
    if (ifc) { /* IFC: T and L return idle within t4, 2.5/2.6; serial poll reset. */
        d->talker = IEEE488_T_TIDS;
        d->listener = IEEE488_L_LIDS;
        d->serial_poll_mode = false;
        d->talk_primary_addressed = false;
        d->listen_primary_addressed = false;
    }
    if (d->cfg.talk_only && !ifc) d->talker = IEEE488_T_TADS;
    if (d->cfg.listen_only && !ifc) d->listener = IEEE488_L_LADS;

    /* RL1, 2.8: MLA while REN enters remote; REN false returns local unless locked out. */
    if (!ren) {
        if (d->rl == IEEE488_RL_REMS)
            set_rl(d, IEEE488_RL_LOCS);
        else if (d->rl == IEEE488_RL_RWLS)
            set_rl(d, IEEE488_RL_LWLS);
    } else if (d->listener == IEEE488_L_LADS || d->listener == IEEE488_L_LACS) {
        if (d->rl == IEEE488_RL_LOCS)
            set_rl(d, IEEE488_RL_REMS);
        else if (d->rl == IEEE488_RL_LWLS)
            set_rl(d, IEEE488_RL_RWLS);
    }

    /* SR1, 2.7: SRQ asserted in SQRS; serial poll response latches APRS. */
    if (d->service_pending && d->sr == IEEE488_SR_NPRS) d->sr = IEEE488_SR_SQRS;
    if (d->talker == IEEE488_T_SPAS && d->sr == IEEE488_SR_SQRS) d->sr = IEEE488_SR_APRS;
    W(d, IEEE488_SRQ, d->sr == IEEE488_SR_SQRS);

    /* During a parallel poll AH must not interpret DIO as a command byte. */
    bool idy = atn && eoi;
    if (!idy)
        acceptor(d, atn);

    source(d, atn);

    d->last_ifc = ifc;
    d->last_dav = R(d, IEEE488_DAV);
}
