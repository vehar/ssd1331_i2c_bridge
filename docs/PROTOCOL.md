> Address update: all 0x02 extended frames and status reads below use **0x3E**. SSD1306 command/data frames use **0x3C** (also accepted at 0x3E). The base address rejects extensions and returns 0xFF on reads. Addresses are configurable in include/config.h. Both share one display state. See README.md for the CLI and shared-bus test procedure.

# I²C protocol, version 1

Slave address: **0x3C**, 7-bit (on-wire write 0x78, read 0x79).
Change `BRIDGE_I2C_ADDRESS` to 0x3D if needed. No subaddress/register selection
is required. Each write starts with a control byte.

## SSD1306 control framing

| Control | Meaning |
| --- | --- |
| 0x00 | Remaining bytes are SSD1306 commands/arguments |
| 0x40 | Remaining bytes are SSD1306 page framebuffer data |
| 0x80 | One command/argument byte, followed by another control byte |
| 0xC0 | One framebuffer byte, followed by another control byte |
| 0x02 | Bridge extended transaction (intentional vendor control byte) |

Standard control bytes require bits 5:0 zero. Co=1 returns to control parsing
after exactly one byte. Example mixed transfer: `80 B0 80 00 80 10 40 FF 00`.
Command argument parsing persists across normal transactions, so a split
`00 21 00` then `00 5F` completes a column range command. A reset or transport
abort clears a partial command.

Framebuffer layout: byte at `page * virtualWidth + column` represents 8 vertical
pixels, bit 0 at the top. Internally the fixed row stride is 128 even in 96-width
mode; transmitted data wraps according to the configured width/address window.

## SSD1306 commands

| Commands | Arguments | Bridge behavior |
| --- | --- | --- |
| 00–0F / 10–1F | none | Low/high column nibble in page addressing; resulting column wraps to virtual width |
| B0–B7 | none | Select page 0–7 |
| 20 | mode: 0 horizontal, 1 vertical, 2 page | Select addressing mode |
| 21 | column start, end | Inclusive window; sets column pointer to start |
| 22 | page start, end | Inclusive window; sets page pointer to start |
| 40–7F | none | Display start line 0–63, software mapped |
| 81 | contrast | Write SSD1331 81/82/83 with the same contrast |
| A0/A1 | none | Horizontal mirror / baseline |
| C0/C8 | none | Vertical mirror / baseline |
| A4/A5 | none | RAM display / all foreground |
| A6/A7 | none | Normal / inverse monochrome |
| AE/AF | none | Physical display off/on |
| A8 | last active row | Software multiplex/blanking |
| D3 | display offset | Software row offset, low 6 bits |
| 26/27 | dummy, start page, interval, end page, dummy, dummy | Right/left software scroll setup |
| 29/2A | dummy, start page, interval, end page, vertical step | Diagonal software scroll setup |
| 2E/2F | none | Deactivate/activate software scrolling |
| A3 | top fixed rows, scroll row count | Software vertical scrolling area |
| 8D/D5/D9/DA/DB | one each | Consume charge-pump/clock/precharge/COM/VCOMH setting; no physical change |
| E3 | none | No-op |

Software scroll interval codes 0–7 correspond to 5,64,128,256,3,4,25,2 nominal
frames; one nominal frame is 16 ms. Late ticks advance one step instead of
catching up. Invalid ranges leave prior valid settings unchanged and set error.

## Extended framing

Each extension must fit in **one I²C write transaction**:

```
02 opcode lengthLow lengthHigh payload...
```

Length is a little-endian 16-bit **payload byte count**, maximum **512**.
RGB565 colors are **big-endian**, high byte first, independently of the length
endianness. X: 0–95, Y: 0–63; endpoints are inclusive. Payload length and
coordinates are validated before execution. Truncated or excess payload is
rejected. Extensions cannot be split across STOPs; use independently addressed
blit rectangles to send large pictures. The supplied master helper uses at
most 32-byte transactions and splits RGB rows into 12-pixel segments.

| Opcode | Payload | Meaning |
| --- | --- | --- |
| 00 | empty | Reinitialize panel and mono state; restore boot geometry/colors/automatic refresh |
| 01 | fgHi,fgLo,bgHi,bgLo | Set global monochrome colors; mark redraw pending |
| 02 | 0 or 1 | Disable/enable automatic monochrome refresh |
| 03 | empty | Render complete monochrome framebuffer immediately |
| 04 | width, viewportX | Width 96/128; viewport 0..width−96; reset address windows/pointers and scroll phase |
| 05 | pattern byte | Fill monochrome framebuffer with the byte pattern |
| 06 | empty | Clear sticky error and overflow counter |
| 10 | x,y,colorHi,colorLo | Native color pixel |
| 11 | x0,y0,x1,y1,colorHi,colorLo | Hardware line |
| 12 | x0,y0,x1,y1,borderHi,borderLo,fillHi,fillLo,filled | Hardware rectangle; filled 0/1 |
| 13 | x0,y0,x1,y1 | Hardware clear window to black |
| 14 | x0,y0,x1,y1 | Hardware dim window |
| 15 | x0,y0,x1,y1,destinationX,destinationY | Hardware copy; destination rectangle must fit |
| 16 | x0,y0,x1,y1,RGB565 pixels... | Row-major image blit; payload length must be 4+2×width×height |
| 20 | contrastA,contrastB,contrastC,masterCurrent | Independent contrast 0–255; current 0–15 |
| 21 | hStep,startRow,rowCount,vStep,interval | SSD1331 hardware scroll setup; hStep 0–95, vStep 0–63, interval 0–3 |
| 22 | 0 or 1 | Stop/start SSD1331 hardware scrolling |
| 7E | native data bytes | SSD1331 SPI data passthrough; 1–512 bytes |
| 7F | native commands/arguments | SSD1331 SPI command passthrough; 1–512 bytes |

Opcode 21 first stops hardware scrolling, then programs it. Native interval
codes mean 6,10,100,200 physical panel frames. After stopping hardware scroll,
rewrite display RAM as required by the SSD1331 datasheet. Native graphics do
not update mono RAM. Disable automatic redraw when exploring them.

## Examples (hex)

| Task | Complete write bytes |
| --- | --- |
| Set mono color to green on black | `02 01 04 00 07 E0 00 00` |
| Disable automatic mono redraw | `02 02 01 00 00` |
| Configure 96-column geometry | `02 04 02 00 60 00` |
| Initialize standard horizontal windows | `00 20 00 21 00 5F 22 00 07 A1 C8 A6 AF` |
| First two mono data bytes | `40 FF 81` |
| Explicit mono redraw | `02 03 00 00` |
| Red pixel at (10,20) | `02 10 04 00 0A 14 F8 00` |
| Yellow diagonal line | `02 11 06 00 00 00 5F 3F FF E0` |
| Blue filled box with white border | `02 12 09 00 0A 0A 28 1E FF FF 00 1F 01` |
| Clear full native display | `02 13 04 00 00 00 5F 3F` |
| Raw normal/inverse display | `02 7F 01 00 A4` / `02 7F 01 00 A7` |
| Raw display all-on/all-off | `02 7F 01 00 A5` / `02 7F 01 00 A6` |
| Raw dim-mode configuration + enable | `02 7F 06 00 AB 00 20 20 20 AC` |
| Raw RGB332 data format (common remap) | `02 7F 02 00 A0 32` |
| Raw linear grayscale table | `02 7F 01 00 B9` |
| Raw reverse-copy enabled | `02 7F 02 00 26 10` |
| Raw lock / unlock | `02 7F 02 00 FD 16` / `02 7F 02 00 FD 12` |
| Reset all bridge state and panel | `02 00 00 00` |

Raw grayscale table: opcode 7F with command B8 followed by 32 table bytes
(valid increasing pulse widths per datasheet); use a larger master TX buffer
or raw command continuation over separately framed 7F transfers. Raw SPI
command state persists in the physical controller across transactions. Do not
interleave ordinary bridge operations with an incomplete raw SSD1331 command.
Raw data 7E similarly continues the physical address pointer. A color mode or
orientation change can invalidate the meaning of convenient graphics commands.
Electrical settings remain the user's responsibility when using passthrough.

## Status read (16 bytes)

A read from 0x3C returns a snapshot from byte 0 every time. Reading more than
16 bytes returns FF for the excess. No SSD1331 RAM readback is available over
this module's write-only SPI connection.

| Byte | Meaning |
| --- | --- |
| 0–1 | ASCII `SB` |
| 2 | Protocol version 1 |
| 3 | Sticky error (most recently detected error, cleared only by 06/reset) |
| 4 | bit 0 auto refresh, bit 1 mono dirty, bit 2 software scrolling, bit 3 main loop processing |
| 5 | Virtual width 96/128 |
| 6 | Viewport X |
| 7 | Mono addressing mode 0/1/2 |
| 8–9 | Queued events, little-endian; includes start/end markers |
| 10–11 | Overflow incidents, little-endian, 16-bit wrapping counter |
| 12–13 | Foreground RGB565, big-endian |
| 14–15 | Background RGB565, big-endian |

Errors: 0 OK, 1 invalid control, 2 unknown command, 3 payload length, 4 range,
5 queue overflow, 6 I²C bus error. A successful ordinary command does not clear
an earlier error. No explicit READY pin is provided. Poll until queue=0 and
processing=0; then check the sticky error. For mono redraw completion also
wait until dirty=0 (only meaningful with auto refresh on), or send explicit
opcode 03 and wait for the processing/queue condition.
