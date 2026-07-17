# IEEE-488.1 traceability

| Standard item | Implementation |
| --- | --- |
| §1.4.3, sixteen signal lines | data + `ieee488_ctrl_line_t`, HAL logical-line API |
| §2.1 functional partition | callbacks separate device functions from interface functions |
| §2.3 SH | `source()`, states `SIDS/SDYS/STRS/SWNS`; DAV/DIO/EOI generation |
| §2.4 AH | `acceptor()`, states `AIDS/ACRS/ACDS`; NRFD/NDAC interlock |
| §2.5 T/TE | primary/secondary addressing, TIDS/TADS/TACS/SPAS, SPE/SPD, serial-poll status byte |
| §2.6 L/LE | primary/secondary addressing, LIDS/LADS/LACS, receive callback |
| §2.7 SR | NPRS/SQRS/APRS, SRQ and RQS bit behavior |
| §2.8 RL | LOCS/LWLS/REMS/RWLS, REN, MLA, GTL, LLO, local `rtl` API |
| §2.9 PP | PPC/PPE/PPD/PPU configuration and ATN+EOI (`IDY`) response on assigned DIO line |
| §2.10 DC | DCL and addressed SDC callbacks |
| §2.11 DT | addressed GET callback |
| §2.12 C | intentionally absent; ATN/IFC/REN are inputs only |
| §2.13, Annex D/E | command constants and decoder |
| §3 | delegated to HAL/transceiver hardware |
| §5.7 | all command bytes are accepted by AH; irrelevant commands are ignored |

## Command table

| Code | Message | Scope/action |
| ---: | --- | --- |
| `0x01` | GTL | addressed listener; RL transition |
| `0x04` | SDC | addressed listener; selected device clear |
| `0x05` | PPC | addressed listener; enter parallel-poll configuration |
| `0x08` | GET | addressed listener; device trigger |
| `0x09` | TCT | ignored (no C function) |
| `0x11` | LLO | universal local lockout |
| `0x14` | DCL | universal device clear |
| `0x15` | PPU | universal parallel-poll unconfigure |
| `0x18` | SPE | universal serial-poll enable |
| `0x19` | SPD | universal serial-poll disable |
| `0x20..0x3e` | LAD | listener primary address |
| `0x3f` | UNL | unlisten |
| `0x40..0x5e` | TAD | talker primary address |
| `0x5f` | UNT | untalk |
| `0x60..0x6f` | PPE/SAD | parallel-poll enable or secondary address, state-dependent |
| `0x70..0x7f` | PPD/SAD | parallel-poll disable or secondary address, state-dependent |
