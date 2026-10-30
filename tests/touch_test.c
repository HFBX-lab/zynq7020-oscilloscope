#include "touch.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t mem[65536];static unsigned address=0x5d,clears;static int fail,write_fail;
static int rd(void *ctx,uint8_t a,uint16_t r,unsigned w,uint8_t *p,size_t n){
    (void)ctx;assert(w==(a==0x38?1u:2u));
    if(fail||a!=address)return -1;memcpy(p,mem+r,n);return 0;
}
static int wr(void *ctx,uint8_t a,uint16_t r,unsigned w,const uint8_t *p,size_t n){
    (void)ctx;assert(w==(a==0x38?1u:2u));
    if(fail||write_fail||a!=address)return -1;
    assert(r==0x814e&&n==1&&p[0]==0);clears++;memcpy(mem+r,p,n);return 0;
}
static TouchBus bus={NULL,rd,wr};
static void gt_init(PanelTouch *t){
    memset(mem,0,sizeof(mem));address=0x5d;fail=0;clears=0;
    memcpy(mem+0x8140,"911",4);mem[0x8146]=0;mem[0x8147]=4;
    mem[0x8148]=0x58;mem[0x8149]=2;
    assert(panel_touch_probe(t,bus)==0);assert(t->family==1&&t->address==0x5d);
}
static void gt_packet(unsigned x,unsigned y,unsigned count){
    mem[0x814e]=0x80|count;mem[0x814f]=3;
    mem[0x8150]=x;mem[0x8151]=x>>8;mem[0x8152]=y;mem[0x8153]=y>>8;
}
int main(void){
    PanelTouch t;gt_init(&t);puts("PASS: GT ID and native geometry");
    gt_packet(1023,599,1);assert(panel_touch_poll(&t,10)==1);
    assert(t.point.pressed&&t.point.x==1023&&t.point.y==599&&clears==1);
    assert(panel_touch_poll(&t,60)==0&&t.point.pressed);
    assert(panel_touch_poll(&t,111)<0&&!t.point.pressed);
    gt_packet(120,130,1);assert(panel_touch_poll(&t,120)==1);
    gt_packet(0,0,0);assert(panel_touch_poll(&t,130)==1&&!t.point.pressed);
    gt_packet(50,60,2);assert(panel_touch_poll(&t,140)<0&&!t.point.pressed);
    gt_packet(1024,10,1);assert(panel_touch_poll(&t,150)<0&&!t.point.pressed);
    gt_packet(10,600,1);assert(panel_touch_poll(&t,160)<0&&!t.point.pressed);
    gt_packet(10,10,1);assert(panel_touch_poll(&t,170)==1&&t.point.pressed);
    fail=1;assert(panel_touch_poll(&t,180)<0&&!t.point.pressed);fail=0;
    gt_packet(11,12,1);write_fail=1;
    assert(panel_touch_poll(&t,190)<0&&!t.point.pressed);write_fail=0;
    assert(panel_touch_poll(&t,0xfffffff0u)==1&&t.point.pressed);
    assert(panel_touch_poll(&t,0x20u)==0&&t.point.pressed);
    assert(panel_touch_poll(&t,0x60u)<0&&!t.point.pressed);
    puts("PASS: GT packet ACK, release, stale/error/multitouch/bounds");
    t.raw_width=600;t.raw_height=1024;t.swap=1;t.invert_x=1;t.invert_y=1;
    TouchPoint p;assert(panel_touch_map(&t,599,1023,&p)==0&&p.x==0&&p.y==0);
    assert(panel_touch_map(&t,0,0,&p)==0&&p.x==1023&&p.y==599);
    assert(panel_touch_map(&t,600,0,&p)<0);puts("PASS: swap, invert and edge rejection");
    memset(mem,0,sizeof(mem));address=0x38;mem[0xa3]=0x54;mem[0xa8]=0x11;mem[0xa6]=7;
    assert(panel_touch_probe(&t,bus)==0&&t.family==2&&t.swap==1);
    mem[2]=1;mem[3]=2;mem[4]=0x57;mem[5]=0x83;mem[6]=0xff;
    assert(panel_touch_poll(&t,200)==1&&t.point.pressed&&t.point.x==1023&&t.point.y==599);
    mem[3]=0x42;assert(panel_touch_poll(&t,220)==1&&!t.point.pressed);
    mem[3]=0xc2;assert(panel_touch_poll(&t,240)<0&&!t.point.pressed);
    mem[2]=2;assert(panel_touch_poll(&t,260)<0&&!t.point.pressed);
    puts("PASS: FT family probe, portrait mapping, event release and invalid frame");
    address=0x5d;memset(mem,0xff,sizeof(mem));assert(panel_touch_probe(&t,bus)<0&&!t.family);
    puts("PASS: unknown ID rejected; TOUCH_TEST_PASS");return 0;
}
