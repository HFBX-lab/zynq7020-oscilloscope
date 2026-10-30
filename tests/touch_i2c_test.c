#include "i2c_bitbang.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
/* A wired-bus slave model follows SCL edges, captures bytes and provides ACK/data. */
typedef struct {
 int sda,scl,active,tx,bits,expect_address,nack,stuck_clock,master_nack;
 unsigned us,starts,stops,used,read_at;uint8_t byte,last,received[32],data[4];
} Wire;
static void drive(void *ctx,unsigned pin,int release){
 Wire *w=ctx;
 if(pin==0){
  if(w->scl&&w->sda&&!release){w->active=1;w->tx=0;w->bits=0;w->byte=0;w->expect_address=1;w->starts++;}
  if(w->scl&&!w->sda&&release){w->active=0;w->stops++;}
  w->sda=release;return;
 }
 int previous=w->scl;w->scl=release;
 if(!w->active)return;
 if(!previous&&release){
  w->bits++;
  if(!w->tx&&w->bits<=8){w->byte=(uint8_t)((w->byte<<1)|w->sda);
   if(w->bits==8){assert(w->used<sizeof(w->received));w->received[w->used++]=w->byte;w->last=w->byte;}}
  if(w->tx&&w->bits==9)w->master_nack=w->sda;
 }
 if(previous&&!release&&w->bits==9){
  if(w->tx)w->read_at++;
  else if(w->expect_address){w->tx=w->last&1;w->expect_address=0;}
  w->bits=0;w->byte=0;
 }
}
static unsigned sample(void *ctx){
 Wire *w=ctx;unsigned data=w->sda;
 if(w->active&&w->scl){
  if(w->tx&&w->bits>=1&&w->bits<=8){assert(w->read_at<4);data&=(w->data[w->read_at]>>(8-w->bits))&1;}
  if(!w->tx&&w->bits==9&&!w->nack)data=0;
 }
 return data|((w->scl&&!w->stuck_clock)?2:0);
}
static void delay(void *ctx,unsigned n){((Wire*)ctx)->us+=n;}
static uint32_t millis(void *ctx){return ((Wire*)ctx)->us/1000;}
static void init(Wire *w){memset(w,0,sizeof(*w));w->sda=w->scl=1;}
int main(void){
 Wire w;init(&w);TouchI2C b={&w,drive,sample,delay,millis};
 w.data[0]=0x91;w.data[1]=0x0f;uint8_t out[2];
 assert(touch_i2c_read(&b,0x5d,0x8140,2,out,2)==0);
 const uint8_t expect[]={0xba,0x81,0x40,0xbb};
 assert(w.used==4&&!memcmp(w.received,expect,4)&&w.starts==2&&w.stops==1);
 assert(out[0]==0x91&&out[1]==0x0f&&w.read_at==2&&w.master_nack==1);
 puts("PASS: actual I2C edges, GT register bytes, repeated start, final NACK and STOP");
 init(&w);uint8_t zero=0;assert(touch_i2c_write(&b,0x5d,0x814e,2,&zero,1)==0);
 const uint8_t ack[]={0xba,0x81,0x4e,0};assert(w.used==4&&!memcmp(w.received,ack,4));
 init(&w);w.nack=1;assert(touch_i2c_read(&b,0x38,0xa3,1,out,1)<0&&w.stops==1&&w.scl&&w.sda);
 puts("PASS: status ACK write and missing-slave NACK cleanup");
 init(&w);w.stuck_clock=1;assert(touch_i2c_read(&b,0x5d,0x8140,2,out,1)<0);
 assert(w.us<10000&&w.sda&&w.scl);puts("PASS: bounded SCL timeout, bus released");
 init(&w);assert(touch_i2c_recover(&b)==0&&w.sda&&w.scl);
 puts("TOUCH_I2C_TEST_PASS");return 0;
}
