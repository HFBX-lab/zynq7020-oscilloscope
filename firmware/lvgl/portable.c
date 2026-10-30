#include "portable.h"
int scope_pack_rgb888(uint8_t *out,size_t capacity,const uint8_t *in,size_t bytes) {
    if(!bytes)return 0;
    if(!out || !in || bytes%4 || capacity<bytes/4*3)return -1;
    for(size_t i=0;i<bytes/4;i++) {
        out[3*i]=in[4*i];out[3*i+1]=in[4*i+1];out[3*i+2]=in[4*i+2];
    }
    return 0;
}
bool scope_touch_tap(ScopeTouchQueue *q,int x,int y) {
    if(!q || x<0 || y<0 || x>=SCOPE_PORT_WIDTH || y>=SCOPE_PORT_HEIGHT || q->count>14)return false;
    unsigned at=(q->head+q->count)%16;
    q->events[at]=(ScopeTouchEvent){(int16_t)x,(int16_t)y,1};
    q->events[(at+1)%16]=(ScopeTouchEvent){(int16_t)x,(int16_t)y,0};
    q->count+=2;return true;
}
bool scope_touch_next(ScopeTouchQueue *q,ScopeTouchEvent *event,bool *more) {
    if(!q || !event || !more)return false;
    bool available=q->count!=0;
    if(available) {q->last=q->events[q->head];q->head=(q->head+1)%16;q->count--;}
    *event=q->last;*more=q->count!=0;return available;
}
