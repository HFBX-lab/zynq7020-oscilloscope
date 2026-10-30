#ifndef SCOPE_PANEL_TOUCH_H
#define SCOPE_PANEL_TOUCH_H
#include <stddef.h>
#include <stdint.h>
typedef struct {
    void *context;
    int (*read)(void *,uint8_t,uint16_t,unsigned,uint8_t *,size_t);
    int (*write)(void *,uint8_t,uint16_t,unsigned,const uint8_t *,size_t);
} TouchBus;
typedef struct {uint16_t x,y;uint8_t pressed;} TouchPoint;
typedef struct {
    TouchBus bus;
    unsigned family,address,raw_width,raw_height,swap,invert_x,invert_y;
    char id[5];uint8_t vendor,firmware;
    TouchPoint point;uint32_t last_ready,errors,packets;
    uint16_t raw_x,raw_y;
} PanelTouch;
/* family 1: GTXX, 2: FTXX protocol (exact part needs board identification). */
int panel_touch_probe(PanelTouch *,TouchBus);
int panel_touch_poll(PanelTouch *,uint32_t);
int panel_touch_map(PanelTouch *,unsigned,unsigned,TouchPoint *);
#endif
