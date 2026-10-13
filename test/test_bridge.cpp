#include "Bridge.h"
#include <vector>
#include <cassert>
#include <cstdio>
#include <random>
using namespace oled;
struct Mock:Panel {
    std::vector<std::vector<uint8_t>> commands;
    std::vector<uint8_t> pixels;
    unsigned inits=0;
    void init() override {++inits;}
    void command(const uint8_t* p,size_t n) override {commands.emplace_back(p,p+n);}
    void data(const uint8_t* p,size_t n) override {pixels.insert(pixels.end(),p,p+n);}
    void wait(uint32_t) override {}
    void clear() {commands.clear();pixels.clear();}
};
void tx(Bridge& b,std::initializer_list<uint8_t> v) {b.start();for(auto x:v)b.byte(x);b.stop();}
void ext(Bridge& b,uint8_t op,std::initializer_list<uint8_t> p) {
    b.start();b.byte(2);b.byte(op);b.byte(p.size());b.byte(p.size()>>8);for(auto x:p)b.byte(x);b.stop();
}
uint8_t err(Bridge& b) {uint8_t s[16];b.status(s);return s[3];}
uint16_t pixel(Mock& m,int x,int y) {size_t i=2*(y*96+x);return (uint16_t(m.pixels.at(i))<<8)|m.pixels.at(i+1);}
int main() {
    Mock m;Bridge b(m);b.reset();
    // Common Adafruit-style initialization must not leak SSD1306 electrical values.
    tx(b,{0,0xAE,0xD5,0x80,0xA8,63,0xD3,0,0x40,0x8D,0x14,0x20,0,0xA1,0xC8,
          0xDA,0x12,0x81,0x7F,0xD9,0xF1,0xDB,0x40,0xA4,0xA6,0xAF});
    assert(err(b)==OK);
    tx(b,{0,0x21,0,127,0x22,0,7});
    b.start();b.byte(0x40);for(int i=0;i<1024;++i)b.byte(i==0?1:i==1023?0x80:0);b.stop();
    assert(b.ramAt(0,0)==1 && b.ramAt(127,7)==0x80);
    m.clear();b.render();assert(m.pixels.size()==12288 && pixel(m,0,0)==0xFFFF && pixel(m,1,0)==0);
    // Foreground/background RGB565 and viewport crop.
    ext(b,1,{0xF8,0,0,0x1F});m.clear();b.render();assert(pixel(m,0,0)==0xF800 && pixel(m,1,0)==0x001F);
    ext(b,4,{128,32});m.clear();b.render();assert(pixel(m,95,63)==0xF800);
    // Vertical addressing, page mode, page/column nibble writes.
    ext(b,4,{96,0});tx(b,{0,0x20,1,0x21,4,5,0x22,1,2});tx(b,{0x40,11,12,13,14});
    assert(b.ramAt(4,1)==11 && b.ramAt(4,2)==12 && b.ramAt(5,1)==13 && b.ramAt(5,2)==14);
    tx(b,{0,0x20,2,0xB3,0x02,0x10});tx(b,{0x40,0xA5});assert(b.ramAt(2,3)==0xA5);
    // Co=1 mixed command/data; split parameters survive STOP.
    tx(b,{0x80,0xB4,0x80,0x03,0xC0,0x77});assert(b.ramAt(3,4)==0x77);
    tx(b,{0,0x21,10});tx(b,{0,11});tx(b,{0x40,9});assert(b.ramAt(10,4)==9);
    // Native opcodes match wire-level SSD1331 command layouts.
    m.clear();ext(b,0x11,{0,0,95,63,0xF8,0});
    assert(m.commands.back()==std::vector<uint8_t>({0x21,0,0,95,63,62,0,0}));
    ext(b,0x12,{1,2,3,4,0x07,0xE0,0,0x1F,1});
    assert(m.commands.back()==std::vector<uint8_t>({0x22,1,2,3,4,0,63,0,0,0,62}));
    ext(b,0x15,{0,0,10,10,85,53});assert(err(b)==OK);
    ext(b,0x15,{0,0,10,10,86,53});assert(err(b)==RANGE);ext(b,6,{});
    m.clear();ext(b,0x16,{0,0,1,0,0xF8,0,0x07,0xE0});assert(m.pixels==std::vector<uint8_t>({0xF8,0,0x07,0xE0}));
    m.clear();ext(b,0x7F,{0xB9});assert(m.commands.back()[0]==0xB9);
    ext(b,0x20,{10,20,30,16});assert(err(b)==RANGE);ext(b,6,{});
    tx(b,{2,0x01,4,0,1,2});assert(err(b)==LENGTH);ext(b,6,{});
    tx(b,{0x01,0xFF});assert(err(b)==CONTROL);ext(b,6,{});
    tx(b,{0,0x21,0,128});assert(err(b)==RANGE);ext(b,6,{});
    tx(b,{0,0xFF});assert(err(b)==COMMAND);ext(b,6,{});
    // Inversion, entire-display-on, software remap and disabled auto refresh.
    b.reset();tx(b,{0x40,1});ext(b,2,{0});m.clear();b.tick(1000);assert(m.pixels.empty());
    b.render();assert(pixel(m,0,0)==0xFFFF);
    tx(b,{0,0xA7});m.clear();b.render();assert(pixel(m,0,0)==0 && pixel(m,1,0)==0xFFFF);
    tx(b,{0,0xA5});m.clear();b.render();assert(pixel(m,0,0)==0xFFFF);
    tx(b,{0,0xA4,0xA6,0xA0});m.clear();b.render();assert(pixel(m,0,0)==0);
    ext(b,4,{128,32});m.clear();b.render();assert(pixel(m,95,0)==0xFFFF);
    b.reset();tx(b,{0x40,1});tx(b,{0,0x26,0,0,7,0,0,0xFF,0x2F});b.tick(2000);
    m.clear();b.render();assert(pixel(m,1,0)==0xFFFF);
    // Status and recovery after aborted, partially received command.
    b.fault(OVERFLOW);uint8_t status[16];b.status(status,100);assert(status[3]==OVERFLOW && status[8]==100 && status[10]==1);
    tx(b,{0,0x21});b.abort();tx(b,{0,0xB0,0,0x10});tx(b,{0x40,0x5A});assert(b.ramAt(0,0)==0x5A);
    // Fuzz malformed bus payloads and exact extension boundaries under sanitizers.
    std::mt19937 rng(12345);
    for(int k=0;k<20000;++k) {
        b.start();int n=rng()%600;for(int j=0;j<n;++j)b.byte(rng());b.stop();
        if(k%50==0){b.abort();b.reset();}
    }
    puts("PASS: SSD1306 init/addressing/Co/split commands/rendering, native commands, validation, abort recovery and 20,000 fuzz transactions");
}
