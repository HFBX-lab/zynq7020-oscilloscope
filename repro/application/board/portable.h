#ifndef SCOPE_BOARD_PORTABLE_H
#define SCOPE_BOARD_PORTABLE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define SCOPE_PORT_WIDTH 1024
#define SCOPE_PORT_HEIGHT 600
typedef struct {int16_t x,y;uint8_t pressed;} ScopeTouchEvent;
typedef struct {
    ScopeTouchEvent events[16],last;
    unsigned head,count;
} ScopeTouchQueue;
int scope_pack_rgb888(uint8_t *out,size_t capacity,const uint8_t *in,size_t bytes);
bool scope_touch_tap(ScopeTouchQueue *queue,int x,int y);
bool scope_touch_next(ScopeTouchQueue *queue,ScopeTouchEvent *event,bool *more);
#endif
