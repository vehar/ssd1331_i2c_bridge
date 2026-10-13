# Validation and board acceptance

## STM32 firmware build

A clean `pio run` completed successfully for `bluepill_f103c8`, using ST STM32
19.0.0, STM32CubeF1 1.8.7 and ARM GCC 7.2.1. Flash: **8,524 / 65,536 bytes**.
Static RAM: **10,088 / 20,480 bytes**; stack/heap runtime usage is additional.
Warnings came from unused parameters in upstream STM32Cube sources; project
sources compiled without warnings. The archive includes the resulting binary
in `firmware/firmware.bin`, linked at **0x08000000**.

## Automated parser validation

Run `test/run.sh` on Linux with g++. It compiles the actual Bridge library with
`-Wall -Wextra -Werror -fsanitize=address,undefined` and asserts:

- Standard SSD1306 initialization consumes the correct argument lengths.
- Horizontal/vertical/page addressing, Co framing and split commands.
- Full 128×64 writes, crop/viewport and 96-column memory behavior.
- Exact RGB565 foreground/background and mono pixel orientation.
- Native drawing command bytes, image blit, copy bounds, malformed packets.
- Automatic-refresh suppression, inversion, all-on, horizontal scrolling.
- Status, overflow reporting and parser resynchronization after abort.
- 20,000 seeded arbitrary transactions up to 599 bytes without sanitizer errors.

Result in the creation environment: **PASS**. Leak detection is disabled by
default because this container prevents the proc access LeakSanitizer needs;
AddressSanitizer and UndefinedBehaviorSanitizer remain enabled.

These are parser/rendering tests. They do not simulate the I²C peripheral or
prove the physical module's electrical compatibility.

## Required hardware checks

1. At 100 kHz, scan for 0x3C after 300 ms startup. Read 16 status bytes: `SB`,
   protocol 1, error 0, virtual width 128, viewport 0. Repeat reads of 1, 16 and
   20 bytes; bytes beyond 16 should be FF. Confirm the following write still works.
2. Send the normal SSD1306 initialization and a 1,024-byte test frame, split
   into the host's supported Wire size. Confirm no row/page drift across STOPs.
   Compare text, checkerboard and all four corners with the expected crop.
3. Send commands with Co=1 and parameters split across multiple writes.
   Exercise a repeated START write→read and write→write. Poll until processing
   and queue are idle; confirm no missing or duplicated first/last bytes.
4. Switch to 96-column geometry; send exactly 768 page bytes. Check all four
   corners, A0/A1 and C0/C8 mirror behavior, inversion, contrast and display-off.
5. Run the included ESP32 example. Verify the left/middle/right boxes are
   red/green/blue, and compare a native colored pixel with a line of the same
   RGB565 value. Panel variants can require changing `BRIDGE_REMAP`; verify
   hardware primitive color order too rather than assuming a generic module.
6. Test filled/unfilled rectangle, clear, copy, dim, image row and native scroll.
   After stopping scroll, rewrite the full display RAM. Disable auto mono redraw
   during native operations; confirm explicit mono redraw restores the image.
7. With a logic analyzer verify SPI mode 0 at 4.5 MHz, D/C=0 for all command
   arguments, D/C=1 for RGB565 pixels, and at least the programmed waits between
   hardware accelerated operations. Measure full-frame redraw latency.
8. Repeat I²C transfers at 400 kHz, with back-to-back frames and polling. Stress
   traffic during an explicit reset and drawing. If overflow occurs, check error
   5, clear/reset and resend a whole frame; confirm next transactions recover.
9. Send invalid length/coordinates/control bytes, then clear the error. Confirm
   valid transfers still work. Interrupt a transfer, release/reset the master
   and confirm a fresh transaction works. Test power cycling host and bridge
   independently with common ground and safe pull-up supplies.

No physical STM32 or OLED was connected during creation. Passing these checks
is required before claiming hardware-tested compatibility.
