#include "memory_probe.h"

static uint32_t pattern(size_t region,size_t word,unsigned pass) {
    uint32_t value=0xa5a55a5au ^ ((uint32_t)region*0x10204081u) ^ ((uint32_t)word*0x01010101u);
    return pass ? ~value : value;
}
static void memory_barrier(void) {
#if defined(__arm__)
    __asm__ volatile("dsb sy" ::: "memory");
#else
    __asm__ volatile("" ::: "memory");
#endif
}
int screen_memory_probe(volatile uint32_t *const regions[],size_t count,size_t words,
                        size_t *bad_region,size_t *bad_word) {
    if(!regions || !count || !words)return -2;
    for(size_t r=0;r<count;r++)if(!regions[r])return -2;
    for(unsigned pass=0;pass<2;pass++) {
        /* Write every region before reading any: independent tests would miss aliases. */
        for(size_t r=0;r<count;r++)for(size_t w=0;w<words;w++)regions[r][w]=pattern(r,w,pass);
        memory_barrier();
        for(size_t r=0;r<count;r++)for(size_t w=0;w<words;w++) {
            if(regions[r][w]!=pattern(r,w,pass)) {
                if(bad_region)*bad_region=r;
                if(bad_word)*bad_word=w;
                return -1;
            }
        }
    }
    return 0;
}
