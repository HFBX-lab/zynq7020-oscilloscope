#include "i2c_bitbang.h"
static void pause(TouchI2C *b){b->delay_us(b->context,5);}
static void line(TouchI2C *b,unsigned pin,int release){b->drive(b->context,pin,release);pause(b);}
static int high(TouchI2C *b){
 line(b,1,1);uint32_t start=b->millis(b->context);
 while(!(b->sample(b->context)&2)){
  if((uint32_t)(b->millis(b->context)-start)>=2)return -1;
  pause(b);
 }
 return 0;
}
static int stop(TouchI2C *b){
 line(b,1,0);line(b,0,0);int rc=high(b);line(b,0,1);
 return rc||((b->sample(b->context)&3)!=3)?-1:0;
}
static int start(TouchI2C *b){
 line(b,0,1);if(high(b)||!(b->sample(b->context)&1))return -1;
 line(b,0,0);line(b,1,0);return 0;
}
static int send(TouchI2C *b,uint8_t byte){
 for(unsigned bit=0;bit<8;bit++){
  line(b,0,(byte&128)!=0);if(high(b))return -1;line(b,1,0);byte<<=1;
 }
 line(b,0,1);if(high(b))return -1;
 int nack=(b->sample(b->context)&1)!=0;line(b,1,0);return nack?-1:0;
}
static int receive(TouchI2C *b,uint8_t *byte,int last){
 *byte=0;line(b,0,1);
 for(unsigned bit=0;bit<8;bit++){
  if(high(b))return -1;
  *byte=(uint8_t)((*byte<<1)|(b->sample(b->context)&1));line(b,1,0);
 }
 line(b,0,last);if(high(b))return -1;line(b,1,0);line(b,0,1);return 0;
}
int touch_i2c_recover(TouchI2C *b){
 line(b,0,1);
 for(unsigned i=0;i<9;i++){line(b,1,0);if(high(b))return stop(b),-1;}
 return stop(b);
}
static int prefix(TouchI2C *b,uint8_t addr,uint16_t reg,unsigned width){
 if(start(b)||send(b,(uint8_t)(addr<<1)))return -1;
 if(width==2&&send(b,(uint8_t)(reg>>8)))return -1;
 return send(b,(uint8_t)reg);
}
int touch_i2c_read(void *ctx,uint8_t a,uint16_t r,unsigned w,uint8_t *p,size_t n){
 TouchI2C *b=ctx;int rc=-1;
 if(!n||!p||(w!=1&&w!=2)||a>127)return -1;
 if(prefix(b,a,r,w)||start(b)||send(b,(uint8_t)((a<<1)|1)))goto done;
 for(size_t i=0;i<n;i++)if(receive(b,p+i,i+1==n))goto done;
 rc=0;
done:if(stop(b))rc=-1;return rc;
}
int touch_i2c_write(void *ctx,uint8_t a,uint16_t r,unsigned w,const uint8_t *p,size_t n){
 TouchI2C *b=ctx;int rc=-1;
 if(!n||!p||(w!=1&&w!=2)||a>127)return -1;
 if(prefix(b,a,r,w))goto done;
 for(size_t i=0;i<n;i++)if(send(b,p[i]))goto done;
 rc=0;
done:if(stop(b))rc=-1;return rc;
}
