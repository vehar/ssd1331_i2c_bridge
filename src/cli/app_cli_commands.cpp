#include "oled_cli.h"
#include "cmd_parser.h"
#include "serial_cli.h"
#include <ctype.h>
#include <string.h>
int g_cli_color_enabled = 1;
static oled::Bridge *b;
void cli_uart_init();
void oled_cli_init(oled::Bridge &bridge) { b = &bridge; cli_uart_init(); }
static bool ext(uint8_t op, const uint8_t *p = nullptr, unsigned n = 0) {
    b->start(true); b->byte(2); b->byte(op); b->byte(n); b->byte(n >> 8);
    for (unsigned i=0;i<n;++i) b->byte(p[i]);
    b->stop();
    uint8_t s[16]; b->status(s);
    if (s[3]) { cli_printf("ERR: bridge status %u (use errors)\r\n", s[3]); return false; }
    return true;
}
static bool native() { uint8_t v=0; return ext(2,&v,1); }
static int help(int, char**) {
    for(unsigned i=0;i<g_cmd_count;++i) cli_printf("%s\r\n",g_cmds[i].help);
    return 1;
}
static int status(int, char**) { uint8_t s[16];b->status(s);
    cli_printf("error=%u flags=0x%02X width=%u viewport=%u fg=0x%02X%02X bg=0x%02X%02X\r\n",s[3],s[4],s[5],s[6],s[12],s[13],s[14],s[15]);return 1; }
static int simple(int argc,char **argv) {
    if(argc!=1)return 0;
    uint8_t op=str_ieq(argv[0],"init")?0:str_ieq(argv[0],"mono")?3:6;
    ext(op); return 1;
}
static int colors(int argc,char **argv) {
    uint16_t fg,bg=0; if((argc!=2&&argc!=3)||!parse_u16(argv[1],&fg)||(argc==3&&!parse_u16(argv[2],&bg)))return 0;
    uint8_t p[]={uint8_t(fg>>8),uint8_t(fg),uint8_t(bg>>8),uint8_t(bg)};
    ext(1,p,4);return 1;
}
static int automatic(int argc,char **argv) {uint8_t v;if(argc!=2||!parse_bool(argv[1],&v))return 0;ext(2,&v,1);return 1;}
static int drawing(int argc,char **argv) {
    bool pixel=str_ieq(argv[0],"pixel"), line=str_ieq(argv[0],"line"), rect=str_ieq(argv[0],"rect");
    unsigned coords=pixel?2:4, need=rect?8:(pixel||line)?coords+2:5;
    if(unsigned(argc)!=need)return 0;
    uint8_t p[9]{};
    for(unsigned i=0;i<coords;++i)
        if(!parse_u8(argv[i+1],&p[i])||p[i]>=((i&1)?64:96))return 0;
    if(!pixel&&!line&&(p[0]>p[2]||p[1]>p[3]))return 0;
    if(pixel||line||rect) {
        uint16_t c;if(!parse_u16(argv[coords+1],&c))return 0;
        p[coords]=c>>8;p[coords+1]=c;
        if(rect) {if(!parse_u16(argv[6],&c)||!parse_bool(argv[7],&p[8]))return 0;p[6]=c>>8;p[7]=c;}
    }
    if(native())ext(pixel?0x10:line?0x11:rect?0x12:str_ieq(argv[0],"clear")?0x13:0x14,p,rect?9:pixel?4:line?6:4);
    return 1;
}
// 5x7 uppercase ASCII font, columns LSB at top. Lowercase maps to uppercase.
static const uint8_t glyphs[][5]={
{0x3e,0x51,0x49,0x45,0x3e},{0,0x42,0x7f,0x40,0},{0x42,0x61,0x51,0x49,0x46},
{0x21,0x41,0x45,0x4b,0x31},{0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},
{0x3c,0x4a,0x49,0x49,0x30},{1,0x71,9,5,3},{0x36,0x49,0x49,0x49,0x36},{6,0x49,0x49,0x29,0x1e},
{0x7e,0x11,0x11,0x11,0x7e},{0x7f,0x49,0x49,0x49,0x36},{0x3e,0x41,0x41,0x41,0x22},
{0x7f,0x41,0x41,0x22,0x1c},{0x7f,0x49,0x49,0x49,0x41},{0x7f,9,9,9,1},
{0x3e,0x41,0x49,0x49,0x7a},{0x7f,8,8,8,0x7f},{0,0x41,0x7f,0x41,0},
{0x20,0x40,0x41,0x3f,1},{0x7f,8,0x14,0x22,0x41},{0x7f,0x40,0x40,0x40,0x40},
{0x7f,2,0xc,2,0x7f},{0x7f,4,8,0x10,0x7f},{0x3e,0x41,0x41,0x41,0x3e},
{0x7f,9,9,9,6},{0x3e,0x41,0x51,0x21,0x5e},{0x7f,9,0x19,0x29,0x46},
{0x46,0x49,0x49,0x49,0x31},{1,1,0x7f,1,1},{0x3f,0x40,0x40,0x40,0x3f},
{0x1f,0x20,0x40,0x20,0x1f},{0x3f,0x40,0x38,0x40,0x3f},{0x63,0x14,8,0x14,0x63},
{7,8,0x70,8,7},{0x61,0x51,0x49,0x45,0x43}};
static int text(int argc,char **argv) {
    uint8_t x,y;uint16_t color;
    if(argc!=5||!parse_u8(argv[1],&x)||!parse_u8(argv[2],&y)||!parse_u16(argv[3],&color)||x>90||y>56)return 0;
    for(const char *s=argv[4];*s;++s)if(!isalnum((unsigned char)*s)&&*s!=' ')return 0;
    if(!native())return 1;
    // RGB565 blit: one 6x8 glyph, opaque black background.
    for(const char *s=argv[4];*s&&x<=90;++s,x+=6) {
        char c=toupper((unsigned char)*s);
        const uint8_t *g=c>='0'&&c<='9'?glyphs[c-'0']:c>='A'&&c<='Z'?glyphs[c-'A'+10]:nullptr;
        uint8_t p[100]={x,y,uint8_t(x+5),uint8_t(y+7)};
        for(unsigned yy=0;yy<8;++yy)for(unsigned xx=0;xx<6;++xx){
            uint16_t v=g&&xx<5&&(g[xx]&(1<<yy))?color:0;
            unsigned i=4+2*(yy*6+xx);p[i]=v>>8;p[i+1]=v;
        }
        if(!ext(0x16,p,sizeof p))break;
    }return 1;
}
static int extended(int argc,char **argv) {
    uint8_t op,p[14];
    if(argc<2||argc>16||!parse_u8(argv[1],&op))return 0;
    for(int i=2;i<argc;++i)if(!parse_u8(argv[i],&p[i-2]))return 0;
    ext(op,p,argc-2);return 1;
}
extern const cmd_t g_cmds[]={
{"ext","ext OP [BYTE ...] (decimal or 0xHEX, up to 14 payload bytes)",extended},
{"help","help",help},{"status","status",status},{"init","init (reset bridge)",simple},
{"errors","errors (clear error)",simple},{"mono","mono (restore framebuffer)",simple},
{"colors","colors FG [BG] (RGB565, e.g. 0xF800)",colors},{"auto","auto on|off",automatic},
{"pixel","pixel X Y COLOR",drawing},{"line","line X0 Y0 X1 Y1 COLOR",drawing},
{"rect","rect X0 Y0 X1 Y1 BORDER FILL FILLED",drawing},
{"clear","clear X0 Y0 X1 Y1",drawing},{"dim","dim X0 Y0 X1 Y1",drawing},
{"text","text X Y COLOR \"Hello 123\" (ASCII letters/digits/spaces)",text}};
extern const unsigned g_cmd_count=sizeof(g_cmds)/sizeof(g_cmds[0]);
