#pragma once
#include <Arduino.h>
#include <Wire.h>
// Master helper. Works with a 32-byte Wire TX buffer. Longer raw/blit payloads
// require increasing both TX_CAPACITY below and the platform's actual buffer.
class BridgeClient
{
    TwoWire &wire;
    uint8_t address, extendedAddress;

public:
    static constexpr size_t TX_CAPACITY = 32;
    explicit BridgeClient(TwoWire &w = Wire, uint8_t a = 0x3C, uint8_t ext = 0x3E) : wire(w), address(a), extendedAddress(ext) {}
    bool status(uint8_t s[16])
    {
        if (wire.requestFrom(extendedAddress, uint8_t(16)) != 16)
        {
            while (wire.available())
                wire.read();
            return false;
        }
        for (int i = 0; i < 16; ++i)
            s[i] = wire.read();
        return s[0] == 'S' && s[1] == 'B' && s[2] == 1;
    }
    bool ready(uint32_t timeout = 1000)
    {
        uint32_t start = millis();
        uint8_t s[16];
        do
        {
            delay(1);
            if (status(s) && !(s[4] & 8) && !s[8] && !s[9])
                return s[3] == 0;
        } while (uint32_t(millis() - start) < timeout);
        return false;
    }
    bool extended(uint8_t op, const uint8_t *data = nullptr, uint16_t length = 0)
    {
        if (length > 512 || length + 4 > TX_CAPACITY)
            return false;
        wire.beginTransmission(extendedAddress);
        wire.write(2);
        wire.write(op);
        wire.write(uint8_t(length));
        wire.write(uint8_t(length >> 8));
        if (length)
            wire.write(data, length);
        return wire.endTransmission() == 0 && ready();
    }
    bool commands(const uint8_t *p, size_t n)
    {
        while (n)
        {
            size_t chunk = n < TX_CAPACITY - 1 ? n : TX_CAPACITY - 1;
            wire.beginTransmission(address);
            wire.write(0);
            wire.write(p, chunk);
            if (wire.endTransmission() != 0 || !ready())
                return false;
            p += chunk;
            n -= chunk;
        }
        return true;
    }
    bool monoData(const uint8_t *p, size_t n)
    {
        while (n)
        {
            size_t chunk = n < TX_CAPACITY - 1 ? n : TX_CAPACITY - 1;
            wire.beginTransmission(address);
            wire.write(0x40);
            wire.write(p, chunk);
            if (wire.endTransmission() != 0 || !ready())
                return false;
            p += chunk;
            n -= chunk;
        }
        return true;
    }
    bool init() {
        if (!extended(0)) return false;
        // Initialize physical SSD1306 too; an adapter-only reset cannot power it on.
        const uint8_t init1306[] = {0xAE,0xD5,0x80,0xA8,63,0xD3,0,0x40,
            0x8D,0x14,0x20,0,0xA1,0xC8,0xDA,0x12,0x81,0x7F,
            0xD9,0xF1,0xDB,0x40,0xA4,0xA6,0x2E,0xAF};
        return commands(init1306,sizeof init1306);
    }
    bool clearErrors() { return extended(6); }
    bool colors(uint16_t fg, uint16_t bg = 0)
    {
        uint8_t p[] = { uint8_t(fg >> 8), uint8_t(fg), uint8_t(bg >> 8), uint8_t(bg) };
        return extended(1, p, 4);
    }
    bool autoRefresh(bool on)
    {
        uint8_t p = on;
        return extended(2, &p, 1);
    }
    bool showMono() { return extended(3); }
    bool geometry(uint8_t width, uint8_t x = 0)
    {
        uint8_t p[] = { width, x };
        return extended(4, p, 2);
    }
    bool clearMono(uint8_t pattern = 0) { return extended(5, &pattern, 1); }
    bool pixel(uint8_t x, uint8_t y, uint16_t color)
    {
        uint8_t p[] = { x, y, uint8_t(color >> 8), uint8_t(color) };
        return extended(0x10, p, 4);
    }
    bool line(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint16_t color)
    {
        uint8_t p[] = { x0, y0, x1, y1, uint8_t(color >> 8), uint8_t(color) };
        return extended(0x11, p, 6);
    }
    bool rectangle(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint16_t border, uint16_t fill, bool filled = true)
    {
        uint8_t p[] = {
            x0, y0, x1, y1, uint8_t(border >> 8), uint8_t(border), uint8_t(fill >> 8), uint8_t(fill), uint8_t(filled)
        };
        return extended(0x12, p, 9);
    }
    bool window(uint8_t op, uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1)
    {
        uint8_t p[] = { x0, y0, x1, y1 };
        return extended(op, p, 4); // 0x13 clear / 0x14 dim
    }
    bool copy(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint8_t dx, uint8_t dy)
    {
        uint8_t p[] = { x0, y0, x1, y1, dx, dy };
        return extended(0x15, p, 6);
    }
    // Split a row into independently addressed blits, safe even with AVR Wire.
    bool row(uint8_t x, uint8_t y, const uint16_t *colors, size_t count)
    {
        if (y >= 64 || count > 96 || x + count > 96)
            return false;
        constexpr size_t maxPixels = (TX_CAPACITY - 8) / 2;
        uint8_t p[TX_CAPACITY - 4];
        while (count)
        {
            size_t n = count < maxPixels ? count : maxPixels;
            p[0] = x;
            p[1] = y;
            p[2] = x + n - 1;
            p[3] = y;
            for (size_t i = 0; i < n; ++i)
            {
                p[4 + 2 * i] = colors[i] >> 8;
                p[5 + 2 * i] = colors[i];
            }
            if (!extended(0x16, p, 4 + 2 * n))
                return false;
            colors += n;
            x += n;
            count -= n;
        }
        return true;
    }
    bool contrast(uint8_t a, uint8_t b, uint8_t c, uint8_t master = 6)
    {
        uint8_t p[] = { a, b, c, master };
        return extended(0x20, p, 4);
    }
    bool scroll(uint8_t h, uint8_t start, uint8_t rows, uint8_t v, uint8_t interval)
    {
        uint8_t p[] = { h, start, rows, v, interval };
        return extended(0x21, p, 5);
    }
    bool scrolling(bool on)
    {
        uint8_t p = on;
        return extended(0x22, &p, 1);
    }
    bool rawCommands(const uint8_t *p, uint16_t n) { return extended(0x7F, p, n); }
    bool rawData(const uint8_t *p, uint16_t n) { return extended(0x7E, p, n); }
};
