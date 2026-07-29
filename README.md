# IEEE-488.1 (GPIB) device library

Portable gnu-11 implementation of an IEEE-488.1 **device-side** interface, excluding the Controller (`C`) interface function.

This can be used to create:

- a standalone test device for GPIB controllers and testing for client programs. This is the main use case. It supports:
  - identification `*IDN?`
  - large writes
  - large reads
  - delay in write
  - delay in read
  - delay in reply
  - srq generation from command, with delay
  - srq generation from trigger, with delay
  - address change
  - EOI/EOS configuration
  - TODO: Maybe also ppol, but VXI-11 and ppoll do not go well together, so that is for later

And after slight modification (that means: deactivation  of the test device code):

- a GPIB interface to any device
- a GPIB-to-LAN gateway (like ICS's 4865)

## Status

This is an early version, and will not work when there are other devices on the bus.

Working:

- extended addressing
- serial poll
- parallel poll
- read/write
- trigger
- remote/local
- clear
- basic config menu
- EOI/EOS handling
- SCPI commands for testing

Not compliant:

- timing on an ATmega4809.
  - The fastest I can get is about 3us for t2 or t5, while it should be 200ns.
  - None of my gateways have problems with it, PROVIDED there are no other devices on the bus.
  - When other devices are on the bus, things break: others will have finished the handshake before I can even start it. Hence: I'm missing commands.
  - A faster CPU, or elaborated CCL, is required.
  - Interrupt handling on DAV (fetching commands) might be a possible protection against missing commands, but it is unlikely to succeed, since I'd need below 1 us handling.

TODO:

- finish SCPI commands (long read/write, SRQ)
- more testing

## Notes on compatibility with gateways

### E5810A

The E5810A will not see the device during a web interface 'Find' if you use a secondary address other than 0. Using no secondary address or using 0 as secondary address is OK. The reason is that it will only initiate in scanning for secondary addresses other than 0 if it finds a reply on secondary address 0.

### AR488

At the time of writing, a couple of PRs are open at the AR488 repo regarding ppoll and serial poll behaviour.

### Side quests on "find"

- E5810A tests adress "N". If it does not reply, it tests adresses "N,0". If that replies, it will also test the other "N,(1-30)" address combinations. The sequence for a test of '5,x' where T = 21 and 5,0 exists is: `...,UNL,T21,L05,UNL,T21,L05,S00,UNL,T21,L05,S01,UNL,T21,L05,S02,UNL,...`
- AR488 tests address "N". If it does not reply, it sends the primary address and then all secondary addresses in sequence without looking at NDAC. If at the end it finds NDAC asserted, it will test address "N,0" properly, and then tests all secondary addresses without interlacing the "Talk to" address between the tests. Unless AR488 PR #87 is solved, it will however fail to detect any address other than 0 because it is non compliant.

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

# IEEE-488.1 SCPI test commands

> This is WIP

The built-in SCPI interpreter is a basic interpreter, meant only for IEEE-488.1 compliance tests for both gateways and client software. It does not support command chaining, and does not support the full mandatory IEEE-488.2 command set.

The supported commands are:

- `*IDN?` replies with `Bateau,ieee488_device,{GPIB address},{software version}`, where
  - `{GPIB address}` is the address on the bus, potentially with ':' separator for secondary address, like `5:1`
  - `{software version}` is the software version, like `0.9`
- `:SYSTEM:ERROR?` replies with the last error. As usual.
- `*CLS`
- `*RST`
- `LONGWR? {ASCII data}` writes an arbitrary quantity of ASCII data to the device. The reply will be in the format `{LEN},{START}`, where
  - `{ASCII data}` is ascii data in the range 0x30-0x7E. It should be made of sequentially increasing characters in the range 0x30-0x7E (looping).
  - `{LEN}` is the length of data received
  - `{START}` is the decimal code of the starting character. It will be 0 when the data that was sent does not respect the sequentiality mentioned above.
- `LONGRD? {LEN} {START}` will result in a reply of an arbitrary length, made of sequentially increasing characters in the range 0x30-0x7E (looping), where
  - `{LEN}` is the length of the data to send
  - `{START}` is the decimal code of the starting character (48-126)
- `SLOWWR {MSECS}` Adds an arbitrary time before acknowledging any received data byte, where
  - `{MSECS}` is the delay time in milliseconds. 0 to disable. Max: 32 bits.
- `SLOWRD {MSECS}` Adds an arbitrary delay time before transmitting any data byte, where
  - `{MSECS}` is the delay time in milliseconds. 0 to disable.
- `DELAYRD {MSECS}` Adds an arbitrary delay time before transmitting a reply, where
  - `{MSECS}` is the delay time in milliseconds. 0 to disable.
- `SLOWWR?` Returns the value (in decimal) of the time set by `SLOWWR`
- `SLOWRD?` Returns the value (in decimal) of the time set by `SLOWRD`
- `DELAYRD?` Returns the value (in decimal) of the time set by `DELAYRD`
- `SRQ [{MSECS}]` for the activation of SRQ, after an optional delay, where
  - `{MSECS}` is the delay time in milliseconds. The delay will not be reset by other commands, so you can time the SRQ to arrive during a communication.
- `ADDR {PRIMARY} [{SECONDARY}]` set the address, where
  - `{PRIMARY}` is the primary address (0-30)
  - `{SECONDARY}` is the secondary address (0-30)
- `EOS [{TERMCHAR}]` sets the terminating character, where
  - `{TERMCHAR}` is the decimal value of the terminating character. If set, it is used for both in- and outgoing communication. If not given or 0, EOS is disabled, and EOI is used for both in- and outgoing communication.
- `EOS?` queries the actual terminating character and replies with `{TERMCHAR}`, or 0 when EOS is disabled. See above.

Note that `EOS` and `EOI` made to be mutually exclusive in this device. So 'end of command' is either based on EOS, either on EOI. End of command is not automatically detected from CR/LF, CR, LF, or ';', on purpose. When `EOS` is deactivated (hence `EOI` activated), all outgoing communications will still be terminated with LF, as is the custom.

# Build

Via platformio.

Other plaforms/build methods are also possible, there are few source files.

## AI disclaimer

Copilot was used to help check validity against the IEEE 488.1 documents in the `docs` directory.
