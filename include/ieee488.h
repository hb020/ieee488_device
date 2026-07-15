#ifndef IEEE488_H
#define IEEE488_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* IEEE 488.1-1987, 1.4.3: sixteen signal lines. Logical true means asserted.
 * The HAL must implement wired-OR/open-collector semantics where required.
 */
typedef enum {
    IEEE488_DIO1 = 0,
    IEEE488_DIO2,
    IEEE488_DIO3,
    IEEE488_DIO4,
    IEEE488_DIO5,
    IEEE488_DIO6,
    IEEE488_DIO7,
    IEEE488_DIO8,
    IEEE488_DAV,
    IEEE488_NRFD,
    IEEE488_NDAC,
    IEEE488_ATN,
    IEEE488_IFC,
    IEEE488_SRQ,
    IEEE488_REN,
    IEEE488_EOI,
    IEEE488_LINE_COUNT
} ieee488_line_t;

typedef struct {
    void* ctx;
    bool (*read_line)(void* ctx, ieee488_line_t line);
    void (*drive_line)(void* ctx, ieee488_line_t line, bool asserted);
    uint8_t (*read_dio)(void* ctx); /* logical byte: bit 0 = DIO1 */
    void (*drive_dio)(void* ctx, uint8_t value, bool enable);
    uint32_t (*time_us)(void* ctx);
} ieee488_hal_t;

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
#define IEEE488_LAD(a) ((uint8_t)(0x20u | ((a) & 0x1Fu)))
#define IEEE488_TAD(a) ((uint8_t)(0x40u | ((a) & 0x1Fu)))
#define IEEE488_SAD(a) ((uint8_t)(0x60u | ((a) & 0x1Fu)))
#define IEEE488_PPE(line_1_to_8, sense) ((uint8_t)(0x60u | (((sense) ? 1u : 0u) << 3) | (((line_1_to_8) - 1u) & 7u)))
#define IEEE488_PPD 0x70u

typedef enum { IEEE488_OK = 0,
               IEEE488_EINVAL = -1,
               IEEE488_EBUSY = -2,
               IEEE488_ETIMEOUT = -3,
               IEEE488_EOVERFLOW = -4 } ieee488_result_t;

typedef enum { IEEE488_ADDR_NORMAL,
               IEEE488_ADDR_EXTENDED } ieee488_address_mode_t;

typedef struct {
    uint8_t primary_address;   /* 0..30; 31 is UNL/UNT */
    uint8_t secondary_address; /* 0..31, used in extended mode */
    ieee488_address_mode_t address_mode;
    bool talk_only;   /* local ton message, 2.5.5 */
    bool listen_only; /* local lon message, 2.6.5 */
    bool use_eoi;
    uint8_t eos_byte;
    bool eos_enabled;
    uint32_t handshake_timeout_us; /* 0 = no software timeout */
    uint32_t t1_delay_us;          /* source settling delay; see 2.3 and 3.8 */
} ieee488_config_t;

typedef struct {
    /* Device-dependent data path, outside the standard (1.4.1, 2.1.1). */
    bool (*tx_next)(void* ctx, uint8_t* byte, bool* end);
    void (*rx_byte)(void* ctx, uint8_t byte, bool end);
    uint8_t (*status_byte)(void* ctx); /* STB bits excluding RQS bit 6 */

    /* Interface function actions. */
    void (*device_clear)(void* ctx, bool selected);               /* DC1, 2.10 */
    void (*device_trigger)(void* ctx);                            /* DT1, 2.11 */
    void (*remote_changed)(void* ctx, bool remote, bool lockout); /* RL1 */
    void (*command_seen)(void* ctx, uint8_t command);
    void* ctx;
} ieee488_callbacks_t;

typedef enum { IEEE488_SH_SIDS,
               IEEE488_SH_SGNS,
               IEEE488_SH_SDYS,
               IEEE488_SH_STRS,
               IEEE488_SH_SWNS,
               IEEE488_SH_SIWS } ieee488_sh_state_t;
typedef enum { IEEE488_AH_AIDS,
               IEEE488_AH_ANRS,
               IEEE488_AH_ACRS,
               IEEE488_AH_ACDS,
               IEEE488_AH_AWNS } ieee488_ah_state_t;
typedef enum { IEEE488_T_TIDS,
               IEEE488_T_TADS,
               IEEE488_T_TACS,
               IEEE488_T_SPAS } ieee488_t_state_t;
typedef enum { IEEE488_L_LIDS,
               IEEE488_L_LADS,
               IEEE488_L_LACS } ieee488_l_state_t;
typedef enum { IEEE488_SR_NPRS,
               IEEE488_SR_SQRS,
               IEEE488_SR_APRS } ieee488_sr_state_t;
typedef enum { IEEE488_RL_LOCS,
               IEEE488_RL_LWLS,
               IEEE488_RL_REMS,
               IEEE488_RL_RWLS } ieee488_rl_state_t;
typedef enum { IEEE488_PP_PPIS,
               IEEE488_PP_PPSS,
               IEEE488_PP_PPAS } ieee488_pp_state_t;

typedef struct ieee488_device {
    ieee488_hal_t hal;
    ieee488_config_t cfg;
    ieee488_callbacks_t cb;

    ieee488_sh_state_t sh;
    ieee488_ah_state_t ah;
    ieee488_t_state_t talker;
    ieee488_l_state_t listener;
    ieee488_sr_state_t sr;
    ieee488_rl_state_t rl;
    ieee488_pp_state_t pp;

    bool serial_poll_mode;
    bool talk_primary_addressed, listen_primary_addressed;
    bool pp_config_addressed, pp_configured;
    uint8_t pp_line;
    bool pp_sense, individual_status;
    bool service_pending;

    bool tx_loaded, tx_end;
    uint8_t tx_byte;
    uint32_t deadline, state_since;
    bool last_ifc, last_atn, last_dav;
} ieee488_device_t;

void ieee488_init(ieee488_device_t* d, const ieee488_hal_t* hal,
                  const ieee488_config_t* cfg, const ieee488_callbacks_t* cb);
void ieee488_reset(ieee488_device_t* d); /* local power-on message (pon) */
void ieee488_poll(ieee488_device_t* d);  /* call frequently */

/* Local messages defined by Annex D. */
void ieee488_request_service(ieee488_device_t* d, bool request); /* rsv */
void ieee488_return_to_local(ieee488_device_t* d);               /* rtl */
void ieee488_set_individual_status(ieee488_device_t* d, bool ist);
void ieee488_set_parallel_poll_local(ieee488_device_t* d, bool enabled,
                                     uint8_t line_1_to_8, bool sense); /* PP2-style local config */

bool ieee488_is_talker(const ieee488_device_t* d);
bool ieee488_is_listener(const ieee488_device_t* d);
bool ieee488_is_remote(const ieee488_device_t* d);

#ifdef __cplusplus
}
#endif
#endif
