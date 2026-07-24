# IEEE-488.1 (GPIB) device library

Portable gnu11 implementation of an IEEE-488.1 **device-side** interface, excluding the Controller (`C`) interface function.

This can be used to create:

- a GPIB interface to any device
- a standalone test device for GPIB controllers
- a GPIB-to-LAN gateway (like ICS's 4865)

## Status

This is an early version, and will likely not work when there are other devices on the bus.

Working:

- serial poll
- parallel poll
- read/write
- trigger
- remote/local
- clear
- basic config menu

Not compliant:

- timing on an ATmega4809.
  - The fastest I can get is about 3us for t2 or t5, while it should be 200ns.
  - None of my gateways have problems with it, PROVIDED there are no other devices on the bus.
  - When other devices are on the bus, things break: others will have finished the handshake before I can even start it. Hence: I'm missing commands.
  - A faster CPU, or elaborated CCL, is required.
  - Interrupt handling on DAV (fetching commands) might be a possible protection against missing commands, but it is unlikely to succeed, since I'd need below 1 us handling.

TODO:

- check how extended address should work:
  - 'find': x,0 is found, but not the others
  - read/write/clear/local/remote/trigger/readstb work, and they do not react to other secondary addresses

## Supported functions/capabilities

- `SH1` source handshake — IEEE 488.1-1987 §2.3
- `AH1` acceptor handshake — §2.4
- normal and extended `T/TE` talker, including serial poll — §2.5
- normal and extended `L/LE` listener — §2.6
- `SR1` service request — §2.7
- `RL1` remote/local with local lockout — §2.8
- `PP1`, with local configuration support for parallel poll participation (`PPE`/`PPD`) — §2.9
- `DC1` device clear — §2.10
- `DT1` device trigger — §2.11
- all non-controller command decoding from §2.13/ Annex D: addresses, `GTL`, `SDC`, `PPC`, `GET`, `LLO`, `DCL`, `PPU`, `SPE`, `SPD`, `UNL`, `UNT`, `PPE`, and `PPD`.

The library deliberately does not implement the `C` function (§2.12), does not drive `ATN`, `IFC`, or `REN`, and ignores `TCT` as required for a non-controller device.

## Supported command set

| Message |  Code/range | Implementation                                          |
| ------- | ----------: | ------------------------------------------------------- |
| GTL     |      `0x01` | Addressed return to local                               |
| SDC     |      `0x04` | Selected device clear                                   |
| PPC     |      `0x05` | Parallel-poll configuration                             |
| GET     |      `0x08` | Group execute trigger                                   |
| TCT     |      `0x09` | Recognized and ignored because Controller is excluded   |
| LLO     |      `0x11` | Universal local lockout                                 |
| DCL     |      `0x14` | Universal device clear                                  |
| PPU     |      `0x15` | Parallel-poll unconfigure                               |
| SPE     |      `0x18` | Serial-poll enable                                      |
| SPD     |      `0x19` | Serial-poll disable                                     |
| LAD     | `0x20–0x3E` | Listen addressing                                       |
| UNL     |      `0x3F` | Unlisten                                                |
| TAD     | `0x40–0x5E` | Talk addressing                                         |
| UNT     |      `0x5F` | Untalk                                                  |
| SAD/PPE | `0x60–0x6F` | Secondary addressing or poll enable according to state  |
| SAD/PPD | `0x70–0x7F` | Secondary addressing or poll disable according to state |

## Electrical/HAL assumptions

IEEE-488 uses negative-true signalling and wired-OR behavior. The HAL uses **logical assertion**, not voltage level: `true` means the message is asserted. The external line interface must meet §3 driver/receiver requirements. `NRFD`, `NDAC`, and `SRQ` must be open-collector/wired-OR. DIO and other output-capable lines require suitable tri-state/open-collector transceivers.

## Integration

1. Implement the several functions and macros mentioned in`ieee488_hal.h`.
2. Set primary/secondary addresses and timing in `ieee488_config_t`.
3. Implement callbacks for device-dependent data and actions.

The polling implementation is non-blocking, and the code in the loop MUST be non-blocking. For high transfer rates, or stricter timing requirements, use one or more of the following:

- fast CPU
- threading
- GPIO edge interrupts
- translate the FSM into an FPGA/peripheral implementation

A basic interrupt mechanism is in place on ATN.
It assumes that on parallel polling, the EOI line is asserted at the same time as the ATN line. Which may not be the case everywhere....

The mechanism needed for syncing between the ISR and the main code is affected by 8 bit Arduino limitations: `#include <stdatomic.h>` is not supported (yet). Therefore, I use `volatile`, `#include <util/atomic.h>` and `ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { .... }`.

If you want to move to a better chain, you WILL want to use 'real' atomic variables.

## Important conformance notes

- §5.7 requires an unsupported/inapplicable command byte to be handshaken and otherwise ignored. The AH machine does this.
- Device-dependent syntax and meaning are outside IEEE-488.1 (§1.4.1 and §2.1.1); callbacks provide that boundary.
- Software timeouts are a host-safety feature, not an IEEE-488.1 protocol action. Set `handshake_timeout_us=0` for indefinite standard-compliant waits.
- The implementation supports both normal and extended addressing, selected at runtime. A particular product should advertise only its actual subset.
- `TCT` is recognized but ignored because the Controller capability is excluded.

## Build

Via platformio.

Other plaforms/build methods are also possible, there are few source files.

## AI disclaimer

Copilot was used to help check validity against the IEEE 488.1 documents in the `docs` directory.
