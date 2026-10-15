# Update: UART CLI, separate extension address and optional scaling

Apply all files in this patch over the uploaded project, preserving paths.
No compiled firmware is included: the STM32 PlatformIO package could not be downloaded in this environment. Run `pio run` before flashing.

- Base **0x3C**: SSD1306 command/data writes only. Control byte 0x02 is rejected; reads return 0xFF (no bridge status).
- Secondary **0x3E**: SSD1306 writes AND all extended commands, with the existing 0x02 framing. Status reads are only here.
- Both addresses share ONE framebuffer/panel. 0x3E is not a second display.
- Config: `include/config.h`. `BRIDGE_SCALE_TO_FIT=1` fits the full virtual width into 96 columns; `0` restores the cropped viewport.
- Fit uses nearest-neighbour horizontal resampling; height stays 64. Thin strokes can disappear. Viewport is ignored in fit mode.
- UART1: **PA9 TX → USB-UART RX; PA10 RX ← USB-UART TX; common GND; 3.3 V logic; 115200 8N1**.
- CLI uses the supplied history/editor/completion engine without FreeRTOS. History reduced to 4 lines for F103C8 RAM.
- ASCII text supports letters/digits/spaces (lowercase rendered uppercase), 6×8 cells, black background, clipped at the right edge.
- Native drawing disables automatic monochrome refresh. `mono` restores the saved framebuffer; `auto on` resumes automatic refresh.
- CLI operates on the adapter only; it never sends data to another physical OLED.

## Quick test

```text
help
colors 0xF800 0x0000
clear 0 0 95 63
text 0 0 0xF800 "Hello 123"
line 0 12 95 12 0x07E0
rect 10 20 80 55 0xFFFF 0x001F 1
dim 20 25 50 45
pixel 95 63 0xF800
status
mono
auto on
```

`ext OP BYTE ...` exposes existing extended opcodes (up to 14 payload bytes per CLI line), e.g. `ext 0x20 128 128 128 6` sets contrast/current. Longer raw/blit packets remain available through I2C. Hex bytes require a 0x prefix. `errors` clears sticky bridge errors.

## Two displays on one address: changes and remaining diagnosis

Two receivers can receive identical write broadcasts. ACK from the adapter can mask an absent or non-ACKing OLED, so a successful master transmission does not establish that both received data.

The old ESP32 helper's `init()` only sent an EXTENDED RESET to the adapter. A physical SSD1306 receives no valid charge-pump/display-on initialization from that packet and can remain dark. The updated helper first resets the adapter via 0x3E, then sends a standard 128×64 SSD1306 initialization to 0x3C. All extended packets and status reads now use 0x3E. Existing ordinary SSD1306 drivers need no address change.

`BRIDGE_NO_STRETCH=1` disables adapter clock stretching as a shared-bus diagnostic/compatibility mode. This is not proof that stretching caused the reported fault. I2C IRQs have highest priority and queue reception independent of SPI rendering; start testing at **100 kHz**, then validate 400 kHz with a logic analyzer. If adapter overrun appears, check IRQ latency or try `BRIDGE_NO_STRETCH=0`. Do not change modes live.

Hardware verification:
1. Power both displays before starting the master. Confirm the physical OLED works alone with its normal driver and that its address is also 0x3C.
2. Connect both with common GND and 3.3 V pull-ups. Avoid parallel pull-ups that excessively load SDA/SCL.
3. Run normal SSD1306 initialization, including charge pump 0x8D 0x14 and display on 0xAF. Broadcast a known 1024-byte 128×64 pattern at 0x3C.
4. Both must show the same logical pattern (color panel resampled or cropped). Query bridge status at 0x3E, never 0x3C.
5. Send colors/native drawing only to 0x3E; the physical OLED must retain its last image. CLI drawing also changes only the color panel.
6. If only the original stays dark with a normal master driver, capture startup and framebuffer writes with/without adapter; compare SCL, SDA, reset, supply and charge-pump commands. The current software alone cannot establish that root cause.

Verified locally: bridge tests in fit and crop modes, each including 20,000 malformed transactions; CLI tests for quoting/whitespace, colors, text blits, rectangle/clear, invalid input and output capture; ASan + UBSan. No physical hardware test or completed STM32 target build.

---

# STM32F103C8T6: SSD1306-compatible I²C to SSD1331 SPI bridge

PlatformIO / STM32Cube project for an 8 MHz crystal Blue Pill, 64 KiB flash,
20 KiB RAM, and a 96×64 SSD1331 color **OLED** module. The STM32 is an I²C slave;
the actual SSD1331 is driven over SPI. No Arduino core or display libraries
are required by the slave firmware.

## Quick start

1. Connect the module and host according to the table below.
2. Open this folder in VS Code with PlatformIO.
3. Attach an ST-Link (3.3 V reference, GND, PA13/SWDIO, PA14/SWCLK).
4. Build and upload: `pio run`, then `pio run -t upload`.
5. Start with I²C **100 kHz**, 7-bit slave address **0x3C**. Allow 300 ms after
   power-on before sending commands. You can later test 400 kHz.
6. Existing SSD1306 master code can send commands using `0x00` and page data
   using `0x40`. For colors and native graphics see [PROTOCOL.md](docs/PROTOCOL.md).

| Signal | STM32 pin | Other end |
| --- | --- | --- |
| I²C SCL | PB6 | Master SCL |
| I²C SDA | PB7 | Master SDA |
| SPI SCK | PA5 | SSD1331 SCL / CLK / SCK |
| SPI MOSI | PA7 | SSD1331 SDA / DIN / MOSI |
| OLED CS | PA4 | SSD1331 CS |
| OLED D/C | PB0 | SSD1331 DC / RS |
| OLED reset | PB1 | SSD1331 RES / RST |
| Ground | GND | Master and OLED GND |
| OLED power | 3.3 V | Module VCC, if its board supports 3.3 V |

**The OLED pin marked SDA is SPI MOSI, not the I²C bus.** Connect the master
only to PB6/PB7. No MISO is needed. Fit external 2.2–4.7 kΩ I²C pull-ups to
3.3 V; account for any pull-ups already fitted. Use 3.3 V logic and check the
specific module's power specification. Keep PA13/PA14 free for SWD.

## Compatibility model

- At boot the framebuffer is **128×64**, stored in SSD1306 page layout. The
  96×64 panel shows columns **0–95**; columns 96–127 are retained but invisible.
  No scaling is performed. Use viewport 16 for the center 96 columns, or 32
  for the right 96 columns.
- Extended geometry command `0x04` can select 96-column wrapping and viewport
  0. A master using this mode must write 96-byte pages and use column end 95.
- Page, horizontal and vertical addressing, column/page windows, command
  continuation across transactions, and control-byte Co/D-C framing work.
- Monochrome bits map to global RGB565 foreground/background colors. The
  default is white on black. Contrast drives all three SSD1331 contrast channels.
- Segment/COM direction, start line, offset, multiplex blanking, inversion and
  entire-display-on are applied in the renderer. A1/C8 are the baseline upright
  orientation; A0/C0 mirror it. Software mapping is relative to the selected
  physical panel remap, not a pin-by-pin emulation of the original glass.
- SSD1306 scrolling is software-emulated, using a nominal 16 ms frame period.
  It is functional emulation, not exact SSD1306 oscillator timing or glass mapping.
- SSD1306 charge pump, clock, precharge, COM pin configuration and VCOMH
  commands consume their arguments but do not change the SSD1331 electrical
  settings. They cannot be translated faithfully between these controllers.
- Unknown/reserved commands set a status error. This is **not a bit-exact
  replacement for every SSD1306 board**. Normal 128×64 driver initialization
  and framebuffer writes are covered; 128×32 layouts need adaptation.

## Monochrome versus native color drawing

The bridge stores a **1 KiB monochrome framebuffer**, not a second color
framebuffer. Color drawing commands operate directly on SSD1331 RAM.
A subsequent monochrome redraw replaces the complete color image.

For native drawing: `autoRefresh(false)`, optionally clear the native display,
then use pixel/line/rectangle/blit/copy/dim commands. Use `showMono()` to return
to the stored mono image, then optionally `autoRefresh(true)`. For overlaying
native graphics on a monochrome image, explicitly redraw mono first and issue
the color drawing afterward. This policy fits within the real C8's 20 KiB RAM.

Raw command/data operations expose the full SSD1331 command set, including
custom grayscale tables, RGB332 mode, dim mode, reverse copy, display modes,
command lock and panel tuning. They are an expert escape hatch. Raw changes
can alter drawing format/mapping; reinitialize with extended `0x00` to restore
all bridge defaults. `showMono()` restores RGB565 remap but does not undo every
other native register change. Send at most one accelerated raw drawing command
per transaction, so the bridge's execution delay separates successive operations.

## Included files

- `src/main.cpp`: clock/GPIO/SPI setup and variable-length I²C slave IRQ driver.
- `lib/Bridge`: platform-independent command parser, framebuffer and renderer.
- `include/config.h`: address, virtual width, viewport, remap and refresh interval.
- `examples/esp32_master`: Arduino master helper and demonstration (not part of
  the slave build). ESP32 pins 21/22 in this example.
- `docs/PROTOCOL.md`: complete implemented protocol and raw examples.
- `docs/VALIDATION.md`: test results and hardware acceptance checks.
- `firmware/firmware.bin`: compiled STM32 image, flash address `0x08000000`.
- `test/run.sh`: host parser tests with AddressSanitizer/UBSan; needs `g++`.

## Transfer and performance limits

I²C IRQs only enqueue incoming bytes; SPI drawing executes in the main loop.
The 4,096-entry ring uses 8 KiB RAM and holds 4,095 byte/boundary events.
Transactions are bounded by STOP or the next addressed START, including a
repeated START before a read. Extended operations are executed only at those
boundaries, after their full length has been checked.

SPI is 4.5 MHz. A full mono redraw transfers 12,288 pixel bytes: about 22 ms
of wire time plus CPU/interrupt overhead. The 20 ms refresh setting is an
eligibility interval, not a guaranteed 50 FPS rate. Dirty writes are coalesced
when the input queue drains; continuous traffic can defer automatic redraws.
Use explicit refresh after a batched framebuffer transfer.

For deterministic operation, pace the master with status reads. I²C ACK means
the peripheral received a byte, **not** that its command has executed. A write
immediately followed by a repeated-START read may return the previous parser
status. Poll until queue depth is zero and processing flag is clear, with a
short delay between polls. The supplied helper does this. A master that ignores
status should wait between frames/operations and avoid sustained bursts.

Queue overflow or a bus error discards queued work and resets parser alignment;
already-applied framebuffer bytes are not rolled back. Stop sending, wait,
clear the error and resend a complete initialization/frame. Extended reset
also reinitializes the panel and mono state, taking roughly 125 ms. After an
overflow retry from a fresh transaction; never continue a partially sent packet.
This firmware has no watchdog or automatic recovery from an externally held-low
bus; release the bus/reset the host as part of system recovery.

## Sources

- [Solomon Systech SSD1331 Rev 1.2 datasheet](https://cdn-shop.adafruit.com/datasheets/SSD1331_1.2.pdf)
- [Solomon Systech SSD1306 datasheet](https://www.displayfuture.com/Display/datasheet/controller/SSD1306.pdf)
- [PlatformIO BluePill F103C8 board](https://docs.platformio.org/en/latest/boards/ststm32/bluepill_f103c8.html)

The host tests cannot verify real STM32 bus timing, module orientation, or color
order. Run the hardware checks before treating the bridge as production firmware.
