#ifndef SCOPE_BOARD_TOUCH_H
#define SCOPE_BOARD_TOUCH_H
#include "touch.h"
int board_touch_init(void);
void board_touch_poll(void);
TouchPoint board_touch_point(void);
void board_touch_status(void);
int board_touch_mapping(unsigned,unsigned,unsigned);
#endif
