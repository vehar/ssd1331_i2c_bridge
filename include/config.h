#pragma once
#define BRIDGE_I2C_ADDRESS 0x3C  // 7-bit; optionally 0x3D
#define BRIDGE_VIRTUAL_WIDTH 128 // 96 or 128
#define BRIDGE_VIEWPORT_X 0      // 0..(virtual width-96)
#define BRIDGE_REMAP 0x72        // common RGB SSD1331 modules, horizontal RGB565
#define BRIDGE_REFRESH_MS 20
#define BRIDGE_QUEUE_SIZE 4096 // power of two, uint16_t events = 8 KiB
