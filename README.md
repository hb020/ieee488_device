# IEEE-488.1 (GPIB) device library

Portable C99 implementation of an IEEE-488.1 **device-side** interface, excluding the Controller (`C`) interface function.

This can be used to create:

- a GPIB interface to any device
- a standalone test device for GPIB controllers
- a GPIB-to-LAN gateway (like ICS's 4865)

## Status

This is an early version.

Mostly working:

- serial poll
- read/write

No working:

- timing

To be tested:

- parallel poll

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

1. Implement the seven HAL functions in `ieee488_hal.h`.
2. Set primary/secondary addresses and timing in `ieee488_config_t`.
3. Implement callbacks for device-dependent data and actions.

The polling implementation is non-blocking. For high transfer rates, use threading and/or GPIO edge interrupts, or translate the same FSM into an FPGA/peripheral implementation.

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
