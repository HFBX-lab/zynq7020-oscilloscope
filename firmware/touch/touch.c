#include "touch.h"
#include <string.h>
static unsigned le16(const uint8_t *p){return p[0]|((unsigned)p[1]<<8);}
static int read_reg(PanelTouch *t,unsigned r,uint8_t *p,size_t n){
    return t->bus.read(t->bus.context,(uint8_t)t->address,(uint16_t)r,t->family==2?1:2,p,n);
}
int panel_touch_probe(PanelTouch *t,TouchBus b){
    memset(t,0,sizeof(*t));t->bus=b;
    if(!b.read||!b.write)return -1;
    const unsigned addresses[]={0x5d,0x14};uint8_t info[10],verify[4];
    for(unsigned i=0;i<2;i++){
        t->address=addresses[i];t->family=1;
        if(read_reg(t,0x8140,info,sizeof(info))||read_reg(t,0x8140,verify,4)||memcmp(info,verify,4))continue;
        if(memcmp(info,"911\0",4)&&memcmp(info,"9147",4)&&memcmp(info,"9271",4)&&memcmp(info,"1158",4))continue;
        unsigned w=le16(info+6),h=le16(info+8);
        if(!((w==1024&&h==600)||(w==600&&h==1024)))continue;
        memcpy(t->id,info,4);t->raw_width=w;t->raw_height=h;t->swap=w==600;
        return 0;
    }
    t->address=0x38;t->family=2;
    /* This establishes a register-protocol candidate, not an exact FT part ID. */
    uint8_t id,id2,vendor,fw,mode;
    if(!read_reg(t,0xa3,&id,1)&&!read_reg(t,0xa3,&id2,1)&&id==id2&&id&&id!=255&&
       !read_reg(t,0xa8,&vendor,1)&&vendor&&vendor!=255&&
       !read_reg(t,0xa6,&fw,1)&&!read_reg(t,0,&mode,1)&&!(mode&0x70)){
        t->id[0]="0123456789ABCDEF"[id>>4];t->id[1]="0123456789ABCDEF"[id&15];
        t->vendor=vendor;t->firmware=fw;t->raw_width=600;t->raw_height=1024;t->swap=1;
        return 0;
    }
    t->family=0;return -1;
}
int panel_touch_map(PanelTouch *t,unsigned x,unsigned y,TouchPoint *p){
    if(x>=t->raw_width||y>=t->raw_height)return -1;
    if(t->swap){unsigned z=x;x=y;y=z;}
    if(x>=1024||y>=600)return -1;
    if(t->invert_x)x=1023-x;
    if(t->invert_y)y=599-y;
    p->x=(uint16_t)x;p->y=(uint16_t)y;return 0;
}
static int invalid(PanelTouch *t){t->point.pressed=0;t->errors++;return -1;}
int panel_touch_poll(PanelTouch *t,uint32_t now){
    uint8_t p[9],zero=0;
    if(!t->family)return invalid(t);
    if(t->family==1){
        /* One burst gives status, track ID and the complete first point. */
        if(read_reg(t,0x814e,p,sizeof(p)))return invalid(t);
        if(!(p[0]&0x80)){
            if(t->point.pressed&&(uint32_t)(now-t->last_ready)>100)return invalid(t);
            return 0;
        }
        /* A failed ACK write invalidates ownership; never publish such a frame. */
        if(t->bus.write(t->bus.context,(uint8_t)t->address,0x814e,2,&zero,1))return invalid(t);
        t->last_ready=now;t->packets++;
        unsigned count=p[0]&15;
        if(!count){t->point.pressed=0;return 1;}
        if(count!=1||(p[0]&0x40))return invalid(t);
        t->raw_x=(uint16_t)le16(p+2);t->raw_y=(uint16_t)le16(p+4);
    }else{
        if(read_reg(t,2,p,7))return invalid(t);
        t->last_ready=now;t->packets++;
        unsigned count=p[0]&15,event=p[1]>>6;
        if(!count||event==1){t->point.pressed=0;return 1;}
        if(count!=1||event==3)return invalid(t);
        t->raw_x=(uint16_t)(((p[1]&15)<<8)|p[2]);
        t->raw_y=(uint16_t)(((p[3]&15)<<8)|p[4]);
    }
    if(panel_touch_map(t,t->raw_x,t->raw_y,&t->point))return invalid(t);
    t->point.pressed=1;return 1;
}
