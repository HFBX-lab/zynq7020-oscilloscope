#ifndef SCOPE_TOUCH_I2C_H
#define SCOPE_TOUCH_I2C_H
#include <stddef.h>
#include <stdint.h>
typedef struct {
 void *context;
 void (*drive)(void *,unsigned,int); /* 0=SDA,1=SCL; 1 releases the line */
 unsigned (*sample)(void *); /* bit0 SDA, bit1 SCL */
 void (*delay_us)(void *,unsigned);
 uint32_t (*millis)(void *);
} TouchI2C;
int touch_i2c_recover(TouchI2C *);
int touch_i2c_read(void *,uint8_t,uint16_t,unsigned,uint8_t *,size_t);
int touch_i2c_write(void *,uint8_t,uint16_t,unsigned,const uint8_t *,size_t);
#endif
