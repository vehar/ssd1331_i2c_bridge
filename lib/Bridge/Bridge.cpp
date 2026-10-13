#ifdef BRIDGE_STM32
#include "config.h"
#define BRIDGE_REMAP_VALUE BRIDGE_REMAP
#define BRIDGE_REFRESH_VALUE BRIDGE_REFRESH_MS
#endif
#ifndef BRIDGE_REMAP_VALUE
#define BRIDGE_REMAP_VALUE 0x72
#endif
#ifndef BRIDGE_REFRESH_VALUE
#define BRIDGE_REFRESH_VALUE 20
#endif
#include "Bridge.h"
#include <string.h>
namespace oled
{
Bridge::Bridge(Panel &p, uint8_t w, uint8_t v) : panel(p), width(w == 96 ? 96 : 128), viewport(v)
{
    if (viewport > width - 96)
        viewport = 0;
    bootWidth = width;
    bootViewport = viewport;
    ce = width - 1;
}
void Bridge::send(uint8_t c, uint8_t a)
{
    uint8_t b[] = { c, a };
    panel.command(b, 2);
}
void Bridge::reset()
{
    panel.init();
    memset(ram, 0, sizeof ram);
    width = bootWidth;
    viewport = bootViewport;
    col = page = cs = ps = 0;
    ce = width - 1;
    pe = 7;
    mode = 2;
    startLine = offset = 0;
    multiplex = 63;
    flipX = flipY = inverse = entire = false;
    fg = 0xF800;
    bg = 0x0000;
    automatic = true;
    dirty = true;
    pending = expected = have = 0;
    scrolling = false;
    scrollX = scrollY = 0;
    scrollTop = 0;
    scrollRows = 64;
    error = OK;
    overflows = 0;
    lastScroll = lastRender = 0;
}
void Bridge::start()
{
    // SSD command arguments may continue across ordinary I2C transactions.
    wantControl = true;
    co = isData = invalid = extension = false;
    extHave = 0;
}
void Bridge::byte(uint8_t b)
{
    if (invalid)
        return;
    if (wantControl)
    {
        wantControl = false;
        if (b == 0x02)
        {
            extension = true;
            return;
        }
        if (b & 0x3F)
        {
            error = CONTROL;
            invalid = true;
            return;
        }
        co = b & 0x80;
        isData = b & 0x40;
        return;
    }
    if (extension)
    {
        if (extHave >= sizeof ext)
        {
            error = LENGTH;
            invalid = true;
            return;
        }
        ext[extHave++] = b;
        return;
    }
    if (isData)
        monoByte(b);
    else
        cmdByte(b);
    if (co)
        wantControl = true;
}
void Bridge::stop()
{
    if (extension && !invalid)
        executeExtended();
    extension = false;
}
void Bridge::monoByte(uint8_t b)
{
    ram[page * 128 + col] = b;
    dirty = true;
    if (mode == 2)
    {
        col = (col + 1) % width;
        return;
    }
    if (mode == 0)
    {
        if (col >= ce)
        {
            col = cs;
            page = page >= pe ? ps : page + 1;
        }
        else
            ++col;
    }
    else
    {
        if (page >= pe)
        {
            page = ps;
            col = col >= ce ? cs : col + 1;
        }
        else
            ++page;
    }
}
void Bridge::cmdByte(uint8_t b)
{
    if (expected)
    {
        args[have++] = b;
        if (have == expected)
        {
            expected = 0;
            apply();
        }
        return;
    }
    pending = b;
    have = 0;
    if (b <= 0x0F)
    {
        col = ((col & 0xF0) | b) % width;
        return;
    }
    if (b >= 0x10 && b <= 0x1F)
    {
        col = ((col & 15) | ((b & 15) << 4)) % width;
        return;
    }
    if (b >= 0xB0 && b <= 0xB7)
    {
        page = b & 7;
        return;
    }
    if (b >= 0x40 && b <= 0x7F)
    {
        startLine = b & 63;
        dirty = true;
        return;
    }
    switch (b)
    {
    case 0x20:
    case 0x81:
    case 0x8D:
    case 0xA8:
    case 0xD3:
    case 0xD5:
    case 0xD9:
    case 0xDA:
    case 0xDB:
        expected = 1;
        return;
    case 0x21:
    case 0x22:
    case 0xA3:
        expected = 2;
        return;
    case 0x26:
    case 0x27:
        expected = 6;
        return;
    case 0x29:
    case 0x2A:
        expected = 5;
        return;
    case 0xA0:
        flipX = true;
        dirty = true;
        return;
    case 0xA1:
        flipX = false;
        dirty = true;
        return;
    case 0xC0:
        flipY = true;
        dirty = true;
        return;
    case 0xC8:
        flipY = false;
        dirty = true;
        return;
    case 0xA4:
        entire = false;
        dirty = true;
        return;
    case 0xA5:
        entire = true;
        dirty = true;
        return;
    case 0xA6:
        inverse = false;
        dirty = true;
        return;
    case 0xA7:
        inverse = true;
        dirty = true;
        return;
    case 0xAE:
    case 0xAF:
        panel.command(&b, 1);
        return;
    case 0x2E:
        scrolling = false;
        scrollX = scrollY = 0;
        dirty = true;
        return;
    case 0x2F:
        scrolling = true;
        dirty = true;
        return;
    case 0xE3:
        return;
    default:
        error = COMMAND;
        return;
    }
}
void Bridge::apply()
{
    switch (pending)
    {
    case 0x20:
        if (args[0] > 2)
        {
            error = RANGE;
            break;
        }
        mode = args[0];
        break;
    case 0x21:
        if (args[0] > args[1] || args[1] >= width)
        {
            error = RANGE;
            break;
        }
        cs = args[0];
        ce = args[1];
        col = cs;
        break;
    case 0x22:
        if (args[0] > args[1] || args[1] > 7)
        {
            error = RANGE;
            break;
        }
        ps = args[0];
        pe = args[1];
        page = ps;
        break;
    case 0x81:
        send(0x81, args[0]);
        send(0x82, args[0]);
        send(0x83, args[0]);
        break;
    case 0xA8:
        if (args[0] > 63)
        {
            error = RANGE;
            break;
        }
        multiplex = args[0];
        dirty = true;
        break;
    case 0xD3:
        offset = args[0] & 63;
        dirty = true;
        break;
    case 0x26:
    case 0x27:
    case 0x29:
    case 0x2A:
        if (args[1] > args[3] || args[3] > 7 || args[2] > 7)
        {
            error = RANGE;
            break;
        }
        scrollStart = args[1];
        scrollEnd = args[3];
        scrollInterval = args[2];
        scrollLeft = pending == 0x27 || pending == 0x2A;
        scrollVertical = (pending == 0x29 || pending == 0x2A) ? args[4] & 63 : 0;
        scrollX = scrollY = 0;
        break;
    case 0xA3:
        if (args[0] > 63 || !args[1] || uint16_t(args[0]) + args[1] > 64)
        {
            error = RANGE;
            break;
        }
        scrollTop = args[0];
        scrollRows = args[1];
        break;
    // Electrical/timing values belong to a different panel. Consume but do not
    // apply SSD1306 charge-pump, COM-pin, precharge, clock or VCOMH settings.
    case 0x8D:
    case 0xD5:
    case 0xD9:
    case 0xDA:
    case 0xDB:
        break;
    default:
        break;
    }
}
bool Bridge::bit(uint8_t x, uint8_t y) const
{
    int sy = (int(y) - offset + 64) % 64;
    if (sy > multiplex)
        return false;
    if (flipY)
        sy = multiplex - sy;
    sy = (sy + startLine) % 64;
    if (scrolling && scrollVertical && sy >= scrollTop && sy < scrollTop + scrollRows)
        sy = scrollTop + (sy - scrollTop + scrollRows - scrollY) % scrollRows;
    int sx = x + viewport;
    if (flipX)
        sx = width - 1 - sx;
    if (scrolling && sy / 8 >= scrollStart && sy / 8 <= scrollEnd)
        sx = (sx + (scrollLeft ? scrollX : width - scrollX)) % width;
    return entire || (((ram[(sy / 8) * 128 + sx] >> (sy & 7)) & 1) != inverse);
}
void Bridge::render()
{
    // Restore bridge-managed RGB565 format after native/raw experimentation.
    send(0xA0, BRIDGE_REMAP_VALUE);
    uint8_t windowCmd[] = { 0x15, 0, 95, 0x75, 0, 63 };
    panel.command(windowCmd, 6);
    uint8_t row[192];
    for (uint8_t y = 0; y < 64; ++y)
    {
        for (uint8_t x = 0; x < 96; ++x)
        {
            uint16_t c = bit(x, y) ? fg : bg;
            row[2 * x] = c >> 8;
            row[2 * x + 1] = c;
        }
        panel.data(row, sizeof row);
    }
    dirty = false;
}
bool Bridge::workDue(uint32_t now) const
{
    static const uint16_t frames[] = { 5, 64, 128, 256, 3, 4, 25, 2 };
    return (scrolling && uint32_t(now - lastScroll) >= uint32_t(frames[scrollInterval]) * 16)
           || (automatic && dirty && uint32_t(now - lastRender) >= BRIDGE_REFRESH_VALUE);
}
void Bridge::tick(uint32_t now)
{
    static const uint16_t frames[] = { 5, 64, 128, 256, 3, 4, 25, 2 };
    if (scrolling && uint32_t(now - lastScroll) >= uint32_t(frames[scrollInterval]) * 16)
    {
        lastScroll = now;
        scrollX = (scrollX + 1) % width;
        scrollY = (scrollY + scrollVertical) % scrollRows;
        dirty = true;
    }
    if (automatic && dirty && uint32_t(now - lastRender) >= BRIDGE_REFRESH_VALUE)
    {
        lastRender = now;
        render();
    }
}
bool Bridge::window(const uint8_t *p) const { return p[0] <= p[2] && p[1] <= p[3] && p[2] < 96 && p[3] < 64; }
static uint16_t u16(const uint8_t *p) { return (uint16_t(p[0]) << 8) | p[1]; }
static void rgb(uint16_t c, uint8_t *p)
{
    p[0] = ((c >> 11) & 31) << 1;
    p[1] = (c >> 5) & 63;
    p[2] = (c & 31) << 1;
}
void Bridge::executeExtended()
{
    if (extHave < 3)
    {
        error = LENGTH;
        return;
    }
    uint16_t n = ext[1] | (uint16_t(ext[2]) << 8);
    uint8_t op = ext[0];
    const uint8_t *p = ext + 3;
    if (n > 512 || extHave != n + 3)
    {
        error = LENGTH;
        return;
    }
    uint8_t c[12]{};
    switch (op)
    {
    case 0x00:
        if (n)
            break;
        reset();
        return;
    case 0x01:
        if (n != 4)
            break;
        fg = u16(p);
        bg = u16(p + 2);
        dirty = true;
        return;
    case 0x02:
        if (n != 1)
            break;
        if (p[0] > 1)
        {
            error = RANGE;
            return;
        }
        automatic = p[0];
        return;
    case 0x03:
        if (n)
            break;
        render();
        return;
    case 0x04:
        if (n != 2)
            break;
        if ((p[0] != 96 && p[0] != 128) || p[1] > p[0] - 96)
        {
            error = RANGE;
            return;
        }
        width = p[0];
        viewport = p[1];
        scrollX = scrollY = 0;
        cs = col = 0;
        ce = width - 1;
        ps = page = 0;
        pe = 7;
        dirty = true;
        return;
    case 0x05:
        if (n != 1)
            break;
        memset(ram, p[0], sizeof ram);
        dirty = true;
        return;
    case 0x06:
        if (n)
            break;
        error = OK;
        overflows = 0;
        return;
    case 0x10: // RGB565 pixel
        if (n != 4)
            break;
        if (p[0] >= 96 || p[1] >= 64)
        {
            error = RANGE;
            return;
        }
        c[0] = 0x15;
        c[1] = c[2] = p[0];
        c[3] = 0x75;
        c[4] = c[5] = p[1];
        send(0xA0, BRIDGE_REMAP_VALUE);
        panel.command(c, 6);
        panel.data(p + 2, 2);
        return;
    case 0x11: // native hardware line
        if (n != 6)
            break;
        if (p[0] >= 96 || p[2] >= 96 || p[1] >= 64 || p[3] >= 64)
        {
            error = RANGE;
            return;
        }
        c[0] = 0x21;
        memcpy(c + 1, p, 4);
        rgb(u16(p + 4), c + 5);
        panel.command(c, 8);
        panel.wait(1);
        return;
    case 0x12: // native rectangle, border + fill + enable
        if (n != 9)
            break;
        if (!window(p) || p[8] > 1)
        {
            error = RANGE;
            return;
        }
        send(0x26, p[8]);
        c[0] = 0x22;
        memcpy(c + 1, p, 4);
        rgb(u16(p + 4), c + 5);
        rgb(u16(p + 6), c + 8);
        panel.command(c, 11);
        panel.wait(2);
        return;
    case 0x13:
    case 0x14: // clear/dim window
        if (n != 4)
            break;
        if (!window(p))
        {
            error = RANGE;
            return;
        }
        c[0] = op == 0x13 ? 0x25 : 0x24;
        memcpy(c + 1, p, 4);
        panel.command(c, 5);
        panel.wait(2);
        return;
    case 0x15: // copy window to destination
        if (n != 6)
            break;
        if (!window(p) || int(p[4]) + p[2] - p[0] >= 96 || int(p[5]) + p[3] - p[1] >= 64)
        {
            error = RANGE;
            return;
        }
        c[0] = 0x23;
        memcpy(c + 1, p, 6);
        panel.command(c, 7);
        panel.wait(2);
        return;
    case 0x16: // RGB565 blit rectangle, inclusive coordinates
        if (n < 6)
            break;
        if (!window(p))
        {
            error = RANGE;
            return;
        }
        if (n != 4 + 2 * (uint16_t(p[2] - p[0] + 1) * (p[3] - p[1] + 1)))
            break;
        send(0xA0, BRIDGE_REMAP_VALUE);
        c[0] = 0x15;
        c[1] = p[0];
        c[2] = p[2];
        c[3] = 0x75;
        c[4] = p[1];
        c[5] = p[3];
        panel.command(c, 6);
        panel.data(p + 4, n - 4);
        return;
    case 0x20: // individual contrast + master current
        if (n != 4)
            break;
        if (p[3] > 15)
        {
            error = RANGE;
            return;
        }
        send(0x81, p[0]);
        send(0x82, p[1]);
        send(0x83, p[2]);
        send(0x87, p[3]);
        return;
    case 0x21: // native scrolling: h step,start row,row count,v step,interval
        if (n != 5)
            break;
        if (p[0] > 95 || p[1] > 63 || !p[2] || p[1] + p[2] > 64 || p[3] > 63 || p[4] > 3)
        {
            error = RANGE;
            return;
        }
        c[0] = 0x2E;
        panel.command(c, 1);
        c[0] = 0x27;
        memcpy(c + 1, p, 5);
        panel.command(c, 6);
        return;
    case 0x22:
        if (n != 1)
            break;
        if (p[0] > 1)
        {
            error = RANGE;
            return;
        }
        c[0] = p[0] ? 0x2F : 0x2E;
        panel.command(c, 1);
        return;
    case 0x7E:
        if (!n)
            break;
        panel.data(p, n);
        return;
    case 0x7F:
        if (!n)
            break;
        panel.command(p, n);
        panel.wait(2);
        return;
    default:
        error = COMMAND;
        return;
    }
    error = LENGTH;
}
void Bridge::status(uint8_t o[16], uint16_t queued) const
{
    memset(o, 0, 16);
    o[0] = 'S';
    o[1] = 'B';
    o[2] = 1;
    o[3] = error;
    o[4] = (automatic ? 1 : 0) | (dirty ? 2 : 0) | (scrolling ? 4 : 0);
    o[5] = width;
    o[6] = viewport;
    o[7] = mode;
    o[8] = queued;
    o[9] = queued >> 8;
    o[10] = overflows;
    o[11] = overflows >> 8;
    o[12] = fg >> 8;
    o[13] = fg;
    o[14] = bg >> 8;
    o[15] = bg;
}
} // namespace oled
