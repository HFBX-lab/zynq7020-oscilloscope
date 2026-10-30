#include "frame_source.h"

int screen_fill(uint8_t *frame,size_t capacity,unsigned pattern) {
    static const uint32_t colors[]={0xffffff,0xffff00,0x00ffff,0x00ff00,0xff00ff,0xff0000,0x0000ff,0};
    if(!frame || capacity<SCREEN_FRAME_BYTES || pattern>SCREEN_GRID)return -1;
    for(unsigned y=0;y<SCREEN_HEIGHT;y++)for(unsigned x=0;x<SCREEN_WIDTH;x++) {
        uint32_t rgb=colors[x/128];
        if(pattern==SCREEN_GRID) {
            rgb=0x101925;
            if(x>=32 && x<=991 && y>=60 && y<=539) {
                if((x-32)%80==0 || (y-60)%60==0)rgb=0x29384b;
                if(x==512 || y==300)rgb=0x526981;
                if(x==32 || x==991 || y==60 || y==539)rgb=0x8798af;
            }
        }
        uint8_t *p=frame+y*SCREEN_STRIDE+x*3;
        /* Little-endian DMA stream: B in low byte -> RGB888[23:0]. */
        p[0]=(uint8_t)rgb;p[1]=(uint8_t)(rgb>>8);p[2]=(uint8_t)(rgb>>16);
    }
    return 0;
}
uint32_t screen_crc32(const uint8_t *data,size_t bytes) {
    uint32_t crc=0xffffffffu;
    while(bytes--) {
        crc^=*data++;
        for(unsigned bit=0;bit<8;bit++)crc=(crc>>1)^((0u-(crc&1u))&0xedb88320u);
    }
    return ~crc;
}
