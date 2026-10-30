#ifndef MEMORY_PROBE_H
#define MEMORY_PROBE_H
#include <stddef.h>
#include <stdint.h>
int screen_memory_probe(volatile uint32_t *const regions[],size_t count,size_t words,
                        size_t *bad_region,size_t *bad_word);
#endif
