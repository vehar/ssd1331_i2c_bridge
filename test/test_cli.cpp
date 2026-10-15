#include "Bridge.h"
#include "../src/cli/oled_cli.h"
#include "../src/cli/serial_cli.h"
#include <cassert>
#include <cstring>
#include <string>
#include <vector>
static std::string output;
int serialAvailable(){return 0;} char serialRead(){return 0;}
void serialWrite(char c){output+=c;} void cli_uart_init(){}
struct Mock:oled::Panel{
std::vector<uint8_t> pixels;
void init() override {}
void command(const uint8_t*,size_t) override {}
void data(const uint8_t*p,size_t n) override {pixels.insert(pixels.end(),p,p+n);}
void wait(uint32_t) override {}
};
int main(){
Mock m;oled::Bridge b(m);b.reset();oled_cli_init(b);cli_init();
cmd_parse_line("colors   0x07E0   0x001F");uint8_t s[16];b.status(s);assert(s[12]==7&&s[13]==0xE0&&s[15]==0x1F);
cmd_parse_line("text 0 0 0xF800 \"Hello 123\""); assert(m.pixels.size()==9*96);
b.status(s);assert(s[3]==0);
cmd_parse_line("rect 0 0 95 63 0xFFFF 0x001F 1");b.status(s);assert(s[3]==0);
cmd_parse_line("clear 0 0 95 63");b.status(s);assert(s[3]==0);
output.clear();cmd_parse_line("pixel -1 0 0");assert(output.find("bad args")!=std::string::npos);
output.clear();cmd_parse_line("text 0 0 0xFFFF \"bad");assert(output.find("unterminated")!=std::string::npos);
char capture[256];bool truncated;assert(cli_execute_captured("help",capture,sizeof capture,&truncated));assert(truncated);
}
