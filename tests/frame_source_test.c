#include "frame_source.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    assert(SCREEN_STRIDE==3072 && SCREEN_FRAME_BYTES==1843200);
    assert(SCREEN_FB0+SCREEN_FRAME_BYTES<=SCREEN_FB1);
    assert(SCREEN_FB1+SCREEN_FRAME_BYTES<0x20000000u);
    uint8_t *allocation=malloc(SCREEN_FRAME_BYTES+128);
    assert(allocation);
    memset(allocation,0xa5,SCREEN_FRAME_BYTES+128);
    uint8_t *pixels=allocation+64;
    assert(screen_fill(pixels,SCREEN_FRAME_BYTES-1,SCREEN_COLORBARS)==-1);
    assert(pixels[0]==0xa5);
    assert(screen_fill(pixels,SCREEN_FRAME_BYTES,SCREEN_COLORBARS)==0);
    const uint32_t colors[]={0xffffff,0xffff00,0x00ffff,0x00ff00,0xff00ff,0xff0000,0x0000ff,0};
    for(unsigned y=0;y<600;y++)for(unsigned x=0;x<1024;x++) {
        const uint8_t *p=pixels+y*SCREEN_STRIDE+x*3;
        uint32_t actual=((uint32_t)p[2]<<16)|((uint32_t)p[1]<<8)|p[0];
        assert(actual==colors[x/128]);
    }
    for(unsigned i=0;i<64;i++) {
        assert(allocation[i]==0xa5);
        assert(allocation[SCREEN_FRAME_BYTES+64+i]==0xa5);
    }
    assert(screen_crc32(pixels,SCREEN_FRAME_BYTES)==SCREEN_COLORBARS_CRC32);
    assert(screen_fill(pixels,SCREEN_FRAME_BYTES,SCREEN_GRID)==0);
    assert(pixels[(60*SCREEN_STRIDE)+32*3]==0xaf);
    assert(pixels[(100*SCREEN_STRIDE)+112*3]==0x4b);
    uint32_t crc=screen_crc32(pixels,SCREEN_FRAME_BYTES);
    assert(crc==SCREEN_GRID_CRC32 && crc==screen_crc32(pixels,SCREEN_FRAME_BYTES));
    assert(screen_crc32((const uint8_t*)"123456789",9)==0xcbf43926u);
    assert(screen_fill(pixels,SCREEN_FRAME_BYTES,99)==-1);
    free(allocation);
    puts("PASS: RGB888 BGR byte packing, all colorbar pixels, stride, guards, grid, CRC32, DDR bounds");
    return 0;
}
