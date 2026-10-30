#ifndef LV_CONF_H
#define LV_CONF_H
#define LV_COLOR_DEPTH 32
#ifdef SCOPE_FREERTOS
#define LV_USE_OS LV_OS_FREERTOS
#define LV_DRAW_THREAD_STACK_SIZE 16384
#else
#define LV_USE_OS LV_OS_NONE
#endif
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB
#define LV_DEF_REFR_PERIOD 20
#define LV_DPI_DEF 130
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_DEFAULT &lv_font_montserrat_16
#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
void board_lv_assert(const char *file,int line);
#define LV_ASSERT_HANDLER do {board_lv_assert(__FILE__,__LINE__);} while(0);
#define LV_BUILD_EXAMPLES 0
#define LV_USE_DEMO_WIDGETS 0
#endif
