#ifndef SCOPE_BOARD_H
#define SCOPE_BOARD_H
#include <stdint.h>
#include <stddef.h>
#define SCOPE_XRGB_ADDRESS 0x12000000u
#define SCOPE_XRGB_BYTES (1024u*600u*4u)
#define SCOPE_LOG_CAPACITY 1024u
uint32_t board_millis(void);
void board_delay(unsigned milliseconds);
void board_log(const char *format,...);
void board_halt(const char *reason);
void board_panic(const char *reason,const char *detail);
int board_init(void);
int board_uart_get(void);
int board_video_init(void);
int board_video_present(const uint8_t *xrgb);
void board_video_stop(void);
uint32_t board_video_signals(void);
uint32_t board_video_errors(void);
uint32_t board_video_front_address(void);
uint32_t board_video_crc(void);
unsigned board_video_frames(void);
#endif
