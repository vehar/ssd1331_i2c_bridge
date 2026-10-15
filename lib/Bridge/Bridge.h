#pragma once
#include <stddef.h>
#include <stdint.h>
namespace oled
{
struct Panel
{
    virtual ~Panel() = default;
    virtual void init() = 0;
    virtual void command(const uint8_t *, size_t) = 0;
    virtual void data(const uint8_t *, size_t) = 0;
    virtual void wait(uint32_t ms) = 0;
};
enum Error : uint8_t
{
    OK = 0,
    CONTROL = 1,
    COMMAND = 2,
    LENGTH = 3,
    RANGE = 4,
    OVERFLOW = 5,
    BUS = 6
};
class Bridge
{
public:
    explicit Bridge(Panel &p, uint8_t width = 128, uint8_t viewport = 0);
    void start(bool allowExtensions = true); // called at every addressed write (including repeated START)
    void byte(uint8_t b);
    void stop();
    void reset();
    void abort()
    {
        pending = expected = have = 0;
        invalid = true;
        extension = false;
        extHave = 0;
    }
    void tick(uint32_t now);
    bool workDue(uint32_t now) const;
    void render();
    void status(uint8_t out[16], uint16_t queued = 0) const;
    void fault(Error e)
    {
        error = e;
        if (e == OVERFLOW)
            ++overflows;
    }
    uint8_t ramAt(uint8_t x, uint8_t page) const { return ram[page * 128 + x]; }

private:
    Panel &panel;
    uint8_t ram[1024]{};
    uint8_t bootWidth, bootViewport;
    uint8_t width, viewport, col = 0, page = 0, cs = 0, ce = 127, ps = 0, pe = 7, mode = 2;
    uint8_t startLine = 0, offset = 0, multiplex = 63;
    bool flipX = false, flipY = false, inverse = false, entire = false, dirty = true, automatic = true;
    uint16_t fg = 0xFFFF, bg = 0, overflows = 0;
    Error error = OK;
    bool allowExtensions = true;
    bool wantControl = true, co = false, isData = false, invalid = false, extension = false;
    uint8_t pending = 0, expected = 0, have = 0, args[6]{};
    uint8_t ext[516]{};
    uint16_t extHave = 0;
    bool scrolling = false, scrollLeft = false;
    uint8_t scrollStart = 0, scrollEnd = 7, scrollInterval = 0, scrollVertical = 0;
    uint8_t scrollTop = 0, scrollRows = 64, scrollX = 0, scrollY = 0;
    uint32_t lastScroll = 0, lastRender = 0;
    void cmdByte(uint8_t);
    void apply();
    void monoByte(uint8_t);
    void executeExtended();
    void send(uint8_t c, uint8_t a);
    bool window(const uint8_t *p) const;
    bool bit(uint8_t x, uint8_t y) const;
};
} // namespace oled
