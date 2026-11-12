# Host MCU API

This guide describes the host-side Arduino API in
`examples/esp32_master/BridgeClient.h`. It is intended for an MCU acting as the
I²C master. The complete wire format and opcode payloads are in
[PROTOCOL.md](PROTOCOL.md).

## Bus setup

The bridge uses 7-bit I²C addresses. The default base address is `0x3C`; the
extended-command and status address is `0x3E`. These are distinct addresses on
the same bridge and display, not separate displays. Use `BRIDGE_I2C_ADDRESS`
and `BRIDGE_EXT_I2C_ADDRESS` in `include/config.h` as the firmware source of
truth if addresses have been changed.

Start the bus at 100 kHz, allow 300 ms after bridge power-up, then initialize
the client. The ESP32 example uses SDA 21 and SCL 22; select pins for the host
board in use. Use 3.3 V logic, a common ground, and suitable external pull-ups.
400 kHz can be tested after confirming reliable operation at 100 kHz.

```cpp
#include "BridgeClient.h"

BridgeClient bridge;

void setup() {
    Wire.begin(21, 22);       // ESP32 example pins; board-specific
    Wire.setClock(100000);
    delay(300);
    if (!bridge.init()) {
        // Handle initialization failure.
    }
}
```

On non-ESP32 Arduino boards, initialize `Wire` with the platform's supported
API. The client constructor accepts a `TwoWire` reference and optional base and
extended addresses:

```cpp
BridgeClient bridge(Wire, 0x3C, 0x3E);
```

## Completion and errors

An I²C write ACK means the bridge received the bytes; it does not mean the
command has executed. `BridgeClient` waits for completion after each write by
polling status until the event queue is empty and the processing flag clears.
The call then succeeds only when the sticky error byte is zero. A timeout,
short/invalid status read, I²C transmission error, or bridge error returns
`false`.

On failure, call `status()` to inspect the 16-byte status snapshot. Byte 3 is
the sticky error; bytes 8–9 are the queued-event count, little-endian; bit 3 of
byte 4 indicates processing. Error values and the full status layout are
documented in [PROTOCOL.md](PROTOCOL.md). Errors remain sticky until
`clearErrors()` or `init()` resets the bridge. Do not assume a failed operation
was rolled back: a malformed, interrupted, or overflowed transfer can follow
partial earlier work. Recover by stopping traffic, waiting for the bridge to
become idle, clearing the error or reinitializing, then resending a complete
operation/frame.

`ready(timeout)` defaults to 1000 ms. It reports `false` on a sticky error as
well as on timeout. It waits for the queue and processing flag, not for the
monochrome dirty flag to clear. After changing monochrome pixels with automatic
refresh enabled, use `showMono()` when the host needs a completed explicit
redraw.

## `BridgeClient` reference

All methods return `bool`; `true` means the method's transfers completed and
the bridge reported no sticky error. The helper uses a 32-byte transmit
capacity by default, matching common Arduino `Wire` buffers.

| Method | Parameters | Behavior |
| --- | --- | --- |
| `status(s)` | `uint8_t s[16]` | Read and validate the 16-byte status block from the extended address. |
| `ready(timeout)` | Optional timeout in ms, default 1000 | Poll until queue and processing are idle, then require error 0. |
| `extended(op, data, length)` | Opcode, payload pointer, payload length | Send one complete extension packet. Payload defaults to null/zero. |
| `commands(data, length)` | SSD1306 command bytes | Send command/control frames to the base address, split into chunks. |
| `monoData(data, length)` | SSD1306 page bytes | Send framebuffer data/control frames to the base address, split into chunks. |
| `init()` | None | Reset bridge state, then send a standard SSD1306 initialization to the base address. |
| `clearErrors()` | None | Clear sticky error and overflow counter. |
| `colors(fg, bg)` | RGB565 foreground; background defaults to black | Set global monochrome foreground/background colors. |
| `autoRefresh(on)` | Boolean | Enable or disable automatic monochrome redraw. |
| `showMono()` | None | Render the stored monochrome framebuffer immediately. |
| `geometry(width, x)` | Width 96 or 128; viewport X defaults to 0 | Set mono virtual geometry and viewport. |
| `clearMono(pattern)` | Pattern defaults to 0 | Fill the monochrome framebuffer with a byte pattern. |
| `pixel(x, y, color)` | Coordinates and RGB565 color | Draw one native-color pixel. |
| `line(x0, y0, x1, y1, color)` | Endpoints and RGB565 color | Draw a native-color line. |
| `rectangle(x0, y0, x1, y1, border, fill, filled)` | Inclusive bounds, colors; `filled` defaults true | Draw a native-color rectangle. |
| `window(op, x0, y0, x1, y1)` | Opcode `0x13` or `0x14`, inclusive bounds | Clear or dim a native-color window. |
| `copy(x0, y0, x1, y1, dx, dy)` | Source bounds and destination origin | Copy a native-color region. |
| `row(x, y, colors, count)` | RGB565 array and pixel count | Send one row as independently addressed blits, split to fit the transmit buffer. |
| `contrast(a, b, c, master)` | Three channel values and master current; defaults to 6 | Set SSD1331 contrast/current values. |
| `scroll(h, start, rows, v, interval)` | Hardware scroll parameters | Configure SSD1331 hardware scrolling. |
| `scrolling(on)` | Boolean | Stop or start hardware scrolling. Rewrite display RAM after stopping, per the controller behavior. |
| `rawCommands(data, length)` | Native SSD1331 command bytes | Send raw SSD1331 commands; expert use. |
| `rawData(data, length)` | Native SSD1331 data bytes | Send raw SSD1331 data; expert use. |

RGB565 values are passed as host `uint16_t` values and serialized high byte
first. `row()` accepts at most 96 pixels, requires `y < 64` and `x + count <=
96`, and splits the row into chunks of up to 12 pixels with the default
32-byte transmit buffer. Empty rows succeed without sending a transaction.

The helper's `extended()` method rejects payloads over 512 bytes and any packet
that exceeds `TX_CAPACITY`; with the default capacity, the largest payload is
28 bytes. `rawCommands()` and `rawData()` use this same limit. To send larger
images, use `row()` or implement separately addressed blit rectangles. If
increasing `TX_CAPACITY`, the host platform's actual `Wire` buffer must also be
large enough for the four-byte extension header and payload.

## Typical use

Disable automatic mono refresh before native-color drawing so a pending mono
refresh does not overwrite the color image. Call `showMono()` to restore the
stored mono image; optionally re-enable automatic refresh afterward.

```cpp
if (!bridge.init() ||
    !bridge.autoRefresh(false) ||
    !bridge.window(0x13, 0, 0, 95, 63) ||
    !bridge.rectangle(8, 8, 40, 32, 0xFFFF, 0xF800)) {
    // Inspect bridge.status() and recover before sending more work.
}
```

Monochrome framebuffer writes and native SSD1331 graphics are separate:
native drawing does not update mono RAM, and a mono redraw replaces the full
native image. For SSD1306-compatible command/data framing, use `commands()` and
`monoData()`; for the full extension opcode set and payload definitions, see
[PROTOCOL.md](PROTOCOL.md).