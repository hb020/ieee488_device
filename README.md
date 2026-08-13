# IEEE-488.1 (GPIB) device and testing tools

## IEEE-488.1 (GPIB) device

Portable gnu-11 implementation of an IEEE-488.1 **device-side** interface, excluding the Controller (`C`) interface function.

This can be used to create a standalone test device for GPIB controllers, or client programs.

This is the main use case. It supports:

- standard identification
- arbitrarily large writes
- arbitrarily large reads
- customisable inter-character delay during write
- customisable inter-character delay during read
- customisable delay for reply
- delayed or immediate srq generation
- live address change, including secondary addresses
- EOI/EOS configuration and live change between EOI/EOS modes
- T1 and T3 configuration
- TODO: Maybe also ppol, but VXI-11 and ppoll do not go well together, so that is for later

And after slight modification (that means: deactivation  of the test device code), you could use this code to create:

- a GPIB interface to any device
- a GPIB-to-LAN gateway (like ICS's 4865)

## Test tools

In `/tests/vxi-11`, there is a test suite (that can be used from automated test tools) for testing a wide range of things regarding VXI-11.2, VXI-11, Hislip and raw socket VISA communication.

It allows testing of:

- Basic communication
- End conditions: EOS
- End conditions: EOI
- End conditions: Count
- SRQ: individual, early enable
- SRQ: individual, late enable
- SRQ: individual, repeated
- SRQ: single emitter, multiple listener
- SRQ: multiple emitters, single listener
- Long read
- Long write
- Delay: in writing
- Delay: in reading
- Delay: in reply

It can be used on any VXI-11.2 compatible gateway, and on hislip, VXI-11 and Socket VISA devices, on a range of IVI backends. It supports a range of devices (which can be extended rather easily). The only device however that supports all test cases is the above mentioned ieee488 device, via a gateway. Especially the EOS to EOI switching is something that is not easily found elsewhere.

Usage:

```text
python3 run.py -h
usage: run.py [-h] [-t {vxi11,hislip,socket,gateway}] [-p PORT] [-a ADDRESSES] [-V {py,ni,keysight,rs}] [-T {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14}] [-cs]
              [-L {DEBUG,INFO,WARNING,ERROR,CRITICAL}]
              [device_ip]

Test VXI-11.2 gateways and other VISA devices

positional arguments:
  device_ip             The IP address of the gateway device to use for tests.

options:
  -h, --help            show this help message and exit
  -t, --type {vxi11,hislip,socket,gateway}
                        The type of device to test. Default is gateway.
  -p, --port PORT       The port to use for the device. Default is 0, which means the default port for the device type.
                        MUST be specified and non-0 for socket type.
  -a, --addresses ADDRESSES
                        The addresses on the bus, separated by ';'.
                        Addresses may contain secondary addresses, in which case the format is '{primary},{secondary}'.
                        Examples: '1' or '1;2,0;2,1'.
                        Is ignored for socket type.
  -V, --visa-provider {py,ni,keysight,rs}
                        The VISA provider to use. Default is the system default.
  -T, --test {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14}
                         0 All
                         1 Basic
                         2 End conditions: EOS
                         3 End conditions: EOI
                         4 End conditions: Count
                         5 SRQ: individual early enable
                         6 SRQ: individual late enable
                         7 SRQ: individual, repeated
                         8 SRQ: single emitter
                         9 SRQ: multiple emitters
                        10 Long read
                        11 Long write
                        12 Delay: write
                        13 Delay: read
                        14 Delay: reply
  -cs, --auto-chunk-size
                        Enable automatic chunk size correction, needed with some gateways for the long reads/writes.
  -L, --log-level {DEBUG,INFO,WARNING,ERROR,CRITICAL}
                        The logging level.
```

## Status

The device is pretty feature complete, works well behind on multiple controllers, and has a rather complete test suite. I will however need to move to faster device hardware if I want to be able to use it when there are other devices on the bus.

Working:

- extended addressing
- serial poll
- parallel poll
- read/write
- trigger
- remote/local
- clear
- EOI/EOS handling
- SCPI commands for testing
- control over bus timing (T1 and T3)
- serial interface for debugging and configuration

Not compliant:

- timing on an ATmega4809.
  - The fastest I can get is about 3 µs for t2 or t5, while it should be 200ns. (but what do you expect on a 16/20 MHz device...)
  - None of my gateways have problems with it, PROVIDED there are no other devices on the bus.
  - When other devices are on the bus, things break: others will have finished the handshake before I can even start it. Hence: I'm missing commands.
  - A faster CPU, or elaborated CCL, is required.
  - Interrupt handling on DAV (fetching commands) might be a possible protection against missing commands, but it is unlikely to succeed, since I'd need below 1 µs handling.

### TODO

- In some cases, on very large reads, when using NI-VISA and my custom gateway, I need to set `inst.chunk_size = ...`, whereas on E5810A I do not need that. You can automatically adapt the chunk size to be bigger than the expected data, via the `--auto-chunk-size` command line parameter to the test script.
- Allow it to support multiple addresses, and have independent SCPI handlers. That risks being messy though, since the state machine is geared towards a single address, especially around the SPAS state and the SRQ message handling.
- Add support for parallel polling control commands to the device
  - `*IST?` Individual Status Query?
  - `*PRE` Parallel Poll Register Enable Command
  - `*PRE?` Parallel Poll Register Enable Query

# More about the IEEE-488.1 (GPIB) device

## Notes on compatibility of the device with gateways

### E5810A

The E5810A will not see the device during a web interface 'Find' if you use a secondary address other than 0. Using no secondary address or using 0 as secondary address is OK. The reason is that it will only initiate in scanning for secondary addresses other than 0 if it finds a reply on secondary address 0.

### AR488

At the time of writing, a couple of PRs are open at the AR488 repo regarding ppoll and serial poll behaviour.

### Side quests on "find"

- E5810A tests adress "N". If it does not reply, it tests adresses "N,0". If that replies, it will also test the other "N,(1-30)" address combinations one by one.
- AR488 tests address "N". If it does not reply, it sends the primary address and then all secondary addresses in sequence without looking at NDAC. If at the end it finds NDAC asserted, it will test address "N,0" properly, and then tests all secondary addresses one by one without interlacing the "Talk to" address between the tests. Unless AR488 PR #87 is solved, it will however fail to detect any address other than 0 because it is non compliant.

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

## IEEE-488.1 SCPI test commands

> This is WIP

The built-in SCPI interpreter is a basic interpreter, meant only for IEEE-488.1 compliance tests for both gateways and client software. It does not support command chaining, and does not support the full mandatory IEEE-488.2 command set.

The supported commands are:

**General**:

- `*IDN?` replies with `Bateau,ieee488_device,{GPIB address},{software version}`, where
  - `{GPIB address}` is the address on the bus, potentially with `.` separator for secondary address, like `5.1`
  - `{software version}` is the software version, like `0.9`
- `:SYSTEM:ERROR?` replies with the last error. As usual.
- `*CLS` will clear errors and buffers, clears SRQ, and resets the Read/Write/Reply delay settings
- `*RST` will reset to default configuration (except address)
- `ADDR {PRIMARY}[,{SECONDARY}]` set the address, where
  - `{PRIMARY}` is the primary address (0-30). Default: 5.
  - `{SECONDARY}` is the secondary address (0-30). If not given, only the primary address is taken into account. Default: none.
  - Note that the separator is ',', but '.' or ':' or ' ' is also allowed.
- `EOS [{TERMCHAR}]` sets the terminating character, where
  - `{TERMCHAR}` is the decimal value of the terminating character (0-255). If set, it is used for both in- and outgoing communication. If not given or -1, EOS is disabled, and EOI is used for both in- and outgoing communication.  Default: -1 (disabled).
- `EOS?` queries the actual terminating character and replies with `{TERMCHAR}`, or -1 when EOS is disabled. See above.

Note that `EOS` and use of `EOI` are made to be mutually exclusive in this device. So 'end of command' is either based on EOS, either on EOI. End of command is purposefully not automatically detected from CR/LF, CR, LF, or ';'. When `EOS` is deactivated (hence `EOI` activated), all outgoing communications will be terminated with CR/LF, and all trailing whitespace is stripped the input, as is the custom.

**Communications**:

- `LONGWR? "{ASCII data}"` writes an arbitrary quantity of ASCII data to the device. The reply will be in the format `{LEN},{START},"{ERROR}"`, where
  - `{ASCII data}` is ascii data (in quotes). It should be made of sequentially increasing characters in the range 0x30-0x7E (`0`-`~`) (looping).
  - `{LEN}` is the length of data received. It will be -1 in case of an error (non sequentiality or format error).
  - `{START}` is the decimal code of the starting character.
  - `{ERROR}` is the error reason (if there was an error).
- `LONGRD? [{LEN} [{START}]]` will result in a reply of an arbitrary length (not surrounded by quotes), made of sequentially increasing characters in the range 0x30-0x7E (`0`-`~`) (looping), where
  - `{LEN}` is the length of the data to send. You can go up to almost 4GB. Not that it would be advisable.... Default value is 1024.
  - `{START}` is the decimal code of the starting character. Capped between 48 and 126 (which is 0x30-0x7E). Default value value is 48 (0x30, '0').

**Communications timing**:

- `SLOWWR {MSECS}` Adds an arbitrary delay time between data byte acceptance by the device, where
  - `{MSECS}` is the delay time in milliseconds. 0 to disable. Capped between 0 and 10000 (10 secs). Default: 0 (disabled).
- `SLOWWR?` Returns the value (in decimal) of the time set by `SLOWWR`
- `SLOWRD {MSECS}` Adds an arbitrary delay time between transmitting of data bytes by the device, where
  - `{MSECS}` is the delay time in milliseconds. 0 to disable. Capped between 0 and 10000 (10 secs). Default: 0 (disabled).
- `SLOWRD?` Returns the value (in decimal) of the time set by `SLOWRD`
- `DELAYRD {MSECS}` Adds an arbitrary delay time between a received command and transmission of a reply, where
  - `{MSECS}` is the delay time in milliseconds. 0 to disable. Capped between 0 and 10800000 (3 hours). Default: 0 (disabled).
- `DELAYRD?` Returns the value (in decimal) of the time set by `DELAYRD`

**SRQ**:

- `SRQ [{MSECS}]` force assertion of SRQ, after an optional delay, where
  - `{MSECS}` is the delay time in milliseconds. The delay will not be reset by other commands (except `*CLS` and `*RST`, but they're supposed to do that), so you can time the SRQ to arrive during a communication. Capped between 0 and 10800000 (3 hours). If 0 or not provided: SRQ is immediate.

**Bus timing**:

- `T1 {USECS}` sets the T1 time (settling time for multiline messages) where
  - `{USECS}` is the delay time in microseconds. Capped between 2 and 1000000 (2 µs to 1 sec). Default: 10µs.
- `T1?` Returns the value (in decimal) of the time set by `T1 {USECS}`
- `T3 {USECS}` sets the T3 time (interface message accept time, or handshake time) where
  - `{USECS}` is the delay time in microseconds. Capped between 0 and 10000000 (10 secs). 0 means: no timeout. If you set it to a small value, data will be lost. Default: 0 (disabled).
- `T3?` Returns the value (in decimal) of the time set by `T3 {USECS}`

The serial interface menu also gives control over some of these parameters, but is more fine grained for some, and without limit checking. It also allows debug output over the serial port.

## Build

Via platformio.

Other plaforms/build methods are also possible, there are few source files.

## AI disclaimer

Copilot was used to help check validity against the IEEE 488.1 documents in the `docs` directory.
