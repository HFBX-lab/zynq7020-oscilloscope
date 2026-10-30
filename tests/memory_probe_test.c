#include "memory_probe.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int main(void) {
    uint32_t memory[3][1024];
    volatile uint32_t *regions[]={memory[0],memory[1],memory[2]};
    size_t bad_region=99,bad_word=99;
    assert(screen_memory_probe(regions,3,1024,&bad_region,&bad_word)==0);
    /* Simulate address aliasing, e.g. an absent high DDR address bit. */
    regions[1]=regions[0];
    assert(screen_memory_probe(regions,3,1024,&bad_region,&bad_word)==-1);
    assert(bad_region==0 && bad_word<1024);
    assert(screen_memory_probe(regions,0,1024,&bad_region,&bad_word)==-2);
    puts("PASS: real memory patterns, complementary passes, address-alias detection, invalid sizes");
    return 0;
}
