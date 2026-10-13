#include "BridgeClient.h"
BridgeClient oled;
void checked(bool success) {
    if(success)return;
    uint8_t s[16];Serial.println("Bridge command failed");
    if(oled.status(s))Serial.printf("error=%u flags=%u queued=%u\n",s[3],s[4],s[8]|(uint16_t(s[9])<<8));
    while(true)delay(1000);
}
void setup() {
    Serial.begin(115200);Wire.begin(21,22);Wire.setClock(100000);delay(300);
    checked(oled.init());checked(oled.autoRefresh(false));checked(oled.geometry(96));
    // Monochrome page data converted to green on black.
    checked(oled.colors(0x07E0));
    const uint8_t commands[]={0xAE,0x20,0x00,0x21,0,95,0x22,0,7,0xA1,0xC8,0xA6,0xAF};
    checked(oled.commands(commands,sizeof commands));
    uint8_t page[96];
    for(int y=0;y<8;++y){for(int x=0;x<96;++x)page[x]=(x/8+y)%2?0xAA:0x55;checked(oled.monoData(page,96));}
    checked(oled.showMono());delay(2000);
    // Native graphics are independent of the monochrome framebuffer.
    checked(oled.window(0x13,0,0,95,63));
    checked(oled.rectangle(0,0,31,31,0xFFFF,0xF800));
    checked(oled.rectangle(32,0,63,31,0xFFFF,0x07E0));
    checked(oled.rectangle(64,0,95,31,0xFFFF,0x001F));
    checked(oled.line(0,32,95,63,0xFFE0));checked(oled.pixel(48,48,0xFFFF));delay(2000);
    checked(oled.window(0x14,0,0,31,31));delay(1000);
    uint16_t colors[96];for(int x=0;x<96;++x)colors[x]=((x*31/95)<<11)|((x*63/95)<<5)|(x*31/95);
    for(int y=40;y<64;++y)checked(oled.row(0,y,colors,96));
    delay(1000);checked(oled.scroll(1,0,64,0,0));checked(oled.scrolling(true));delay(3000);
    checked(oled.scrolling(false)); // SSD1331 requires RAM rewrite after stopping
    checked(oled.showMono());
    checked(oled.autoRefresh(true));Serial.println("Demo complete");
}
void loop() {delay(1000);}
