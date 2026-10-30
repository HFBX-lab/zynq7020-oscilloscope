#include "board_touch.h"
#include "i2c_bitbang.h"
#include "board.h"
#include "xgpio.h"
#include "xparameters.h"
#include "sleep.h"
#if XPAR_TOUCH_GPIO_BASEADDR != 0x41210000
#error Touch GPIO address differs from verified platform contract
#endif
static XGpio gpio;
static unsigned direction=7,value;
static PanelTouch touch;
static uint32_t polled,reported;
static int available,last_pressed;
static void apply(void){
 XGpio_DiscreteWrite(&gpio,1,value);XGpio_SetDataDirection(&gpio,1,direction);
}
static void drive(void *ctx,unsigned pin,int release){
 (void)ctx;if(release)direction|=1u<<pin;else direction&=~(1u<<pin);
 XGpio_SetDataDirection(&gpio,1,direction);
}
static unsigned sample(void *ctx){(void)ctx;return XGpio_DiscreteRead(&gpio,1);}
static void delay_us(void *ctx,unsigned n){(void)ctx;usleep(n);}
static uint32_t millis(void *ctx){(void)ctx;return board_millis();}
static TouchI2C i2c={NULL,drive,sample,delay_us,millis};
int board_touch_init(void){
 available=0;last_pressed=0;
 if(XGpio_Initialize(&gpio,XPAR_TOUCH_GPIO_DEVICE_ID)!=XST_SUCCESS)return -1;
 value=0;direction=3;apply(); /* reset=0 and INT=0, bus released */
 board_delay(20);
 value=8;apply();board_delay(10);
 direction=7;apply();board_delay(250); /* INT becomes input; both families boot */
 unsigned levels=sample(NULL);
 board_log("TOUCH_BUS addr_gpio=41210000 levels=%x SDA=%u SCL=%u INT=%u\r\n",levels,levels&1,(levels>>1)&1,(levels>>2)&1);
 if(touch_i2c_recover(&i2c)){
  board_log("TOUCH_UNAVAILABLE: bus stuck; UI display remains active\r\n");return -1;
 }
 TouchBus bus={&i2c,touch_i2c_read,touch_i2c_write};
 if(panel_touch_probe(&touch,bus)){
  board_log("TOUCH_UNAVAILABLE: no supported ID/geometry at 5D/14/38\r\n");return -1;
 }
 available=1;polled=board_millis();
 board_touch_status();return 0;
}
void board_touch_status(void){
 board_log("TOUCH_STATUS available=%u family=%s address=%02x id=%s vendor=%02x firmware=%02x raw=%ux%u swap=%u invert_x=%u invert_y=%u packets=%u errors=%u levels=%x\r\n",
  available,touch.family==1?"GTXX":touch.family==2?"FTXX-protocol-candidate":"unknown",touch.address,touch.id,
  touch.vendor,touch.firmware,touch.raw_width,touch.raw_height,touch.swap,touch.invert_x,touch.invert_y,
  (unsigned)touch.packets,(unsigned)touch.errors,sample(NULL));
}
void board_touch_poll(void){
 if(!available)return;
 uint32_t now=board_millis();if((uint32_t)(now-polled)<20)return;polled=now;
 (void)panel_touch_poll(&touch,now);
 if(touch.point.pressed!=last_pressed||(touch.point.pressed&&(uint32_t)(now-reported)>=200)){
  board_log("TOUCH_POINT pressed=%u x=%u y=%u raw_x=%u raw_y=%u packets=%u errors=%u\r\n",
   touch.point.pressed,touch.point.x,touch.point.y,touch.raw_x,touch.raw_y,(unsigned)touch.packets,(unsigned)touch.errors);
  last_pressed=touch.point.pressed;reported=now;
 }
}
TouchPoint board_touch_point(void){return touch.point;}
int board_touch_mapping(unsigned swap,unsigned ix,unsigned iy){
 if(swap>1||ix>1||iy>1)return -1;
 touch.point.pressed=0;touch.swap=swap;touch.invert_x=ix;touch.invert_y=iy;
 touch.raw_width=swap?600:1024;touch.raw_height=swap?1024:600;
 board_touch_status();return 0;
}
