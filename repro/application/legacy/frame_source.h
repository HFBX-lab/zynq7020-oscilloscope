#ifndef FRAME_SOURCE_H
#define FRAME_SOURCE_H
#include <stddef.h>
#include <stdint.h>
#define SCREEN_WIDTH 1024u
#define SCREEN_HEIGHT 600u
#define SCREEN_STRIDE (SCREEN_WIDTH*3u)
#define SCREEN_FRAME_BYTES (SCREEN_STRIDE*SCREEN_HEIGHT)
#define SCREEN_FB0 0x10000000u
#define SCREEN_FB1 0x10200000u
#define SCREEN_COLORBARS_CRC32 0xdb424875u
#define SCREEN_GRID_CRC32 0x6a57b038u
enum {SCREEN_COLORBARS=0,SCREEN_GRID=1};
int screen_fill(uint8_t *frame,size_t capacity,unsigned pattern);
uint32_t screen_crc32(const uint8_t *data,size_t bytes);
#endif
