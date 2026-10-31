/* Internal-test platform; load the matching bitstream and ELF through JTAG.
 * This tests an INTERNAL DIGITAL source, not a connected ADC or analog signal. */
#include "adc_abi.h"
#include "adc_format.h"
#include "board.h"
#include "xil_io.h"
#include "xil_cache.h"
#include <stdint.h>

#define EXPORT0 0x13000000u
#define EXPORT1 0x13004000u
#define POLL_LIMIT_MS 2000u
static uint32_t recovery[ADC_RECORD_WORDS];
static uint32_t reg_read(unsigned offset) {return Xil_In32(ADC_BASE+offset);}
static void reg_write(unsigned offset,uint32_t value) {
    Xil_Out32(ADC_BASE+offset,value);
    __asm__ volatile("dsb sy" ::: "memory");
}
void board_delay(unsigned milliseconds) {
    uint32_t start=board_millis();
    while((uint32_t)(board_millis()-start)<milliseconds) {}
}
static int wait_idle(void) {
    uint32_t start=board_millis();
    do {
        if(!(reg_read(ADC_REG_STATUS)&(ADC_STATUS_BUSY|ADC_STATUS_RESETTING)))return 0;
    }while((uint32_t)(board_millis()-start)<POLL_LIMIT_MS);
    board_log("ADC_IDLE_TIMEOUT: status=%08x\r\n",(unsigned)reg_read(ADC_REG_STATUS));
    return -1;
}
static int capture(uint32_t *destination,const char *tag) {
    if(wait_idle())return -1;
    uint32_t old_id=reg_read(ADC_REG_CAPTURE_ID);
    reg_write(ADC_REG_CONTROL,ADC_CONTROL_ARM);
    uint32_t start=board_millis(),status;
    do {
        status=reg_read(ADC_REG_STATUS);
        if(status&(ADC_STATUS_ERROR|ADC_STATUS_TIMEOUT|ADC_STATUS_REJECTED))goto fail;
        if((status&(ADC_STATUS_DONE|ADC_STATUS_READABLE))==(ADC_STATUS_DONE|ADC_STATUS_READABLE))break;
    }while((uint32_t)(board_millis()-start)<POLL_LIMIT_MS);
    if((status&(ADC_STATUS_BUSY|ADC_STATUS_RESETTING|ADC_STATUS_DONE|ADC_STATUS_READABLE)) !=
       (ADC_STATUS_DONE|ADC_STATUS_READABLE))goto fail;
    uint32_t capture_id=reg_read(ADC_REG_CAPTURE_ID);
    if(capture_id!=old_id+1u)goto fail;
    /* Read only while this ARM's record is frozen/readable. No speculative or
       deliberate SLVERR access: a Cortex-A9 MMIO abort is not a C error code. */
    for(unsigned i=0;i<ADC_RECORD_WORDS;i++)destination[i]=reg_read(ADC_WINDOW+4u*i);
    Xil_DCacheFlushRange((UINTPTR)destination,ADC_RECORD_BYTES);
    __asm__ volatile("dsb sy" ::: "memory");
    if(reg_read(ADC_REG_CAPTURE_ID)!=capture_id || reg_read(ADC_REG_STATUS)!=status)goto fail;
    AdcRecordReport report;
    if(adc_validate_record(destination,ADC_RECORD_WORDS,&report)) {
        board_log("ADC_FORMAT_FAIL: %s word=%u\r\n",tag,(unsigned)report.bad_word);return -1;
    }
    board_log("ADC_INTERNAL_RECORD_PASS: %s id=%u address=%08x bytes=%u first_n=%05x crc32=%08x\r\n",
              tag,(unsigned)capture_id,(unsigned)(uintptr_t)destination,ADC_RECORD_BYTES,
              (unsigned)report.first_sequence,(unsigned)report.crc32);
    return 0;
fail:
    board_log("ADC_CAPTURE_FAIL: %s status=%08x id=%u old_id=%u\r\n",tag,
              (unsigned)reg_read(ADC_REG_STATUS),(unsigned)reg_read(ADC_REG_CAPTURE_ID),(unsigned)old_id);
    return -1;
}
static int timeout_recovery(void) {
    uint32_t old_id=reg_read(ADC_REG_CAPTURE_ID);
    reg_write(ADC_REG_TEST_CONTROL,0);
    reg_write(ADC_REG_CONTROL,ADC_CONTROL_ARM);
    uint32_t start=board_millis(),status;
    do {
        status=reg_read(ADC_REG_STATUS);
        if(status&ADC_STATUS_TIMEOUT)break;
    }while((uint32_t)(board_millis()-start)<POLL_LIMIT_MS);
    if((status&(ADC_STATUS_TIMEOUT|ADC_STATUS_ERROR))!=(ADC_STATUS_TIMEOUT|ADC_STATUS_ERROR))return -1;
    if(wait_idle())return -1;
    status=reg_read(ADC_REG_STATUS);
    /* DONE denotes completion of an attempt, including timeout. A timed-out
       attempt must expose DONE+ERROR+TIMEOUT, while its window stays unreadable. */
    if(status!=(ADC_STATUS_INTERNAL|ADC_STATUS_DONE|ADC_STATUS_ERROR|ADC_STATUS_TIMEOUT) ||
       reg_read(ADC_REG_CAPTURE_ID)!=old_id)return -1;
    board_log("ADC_INTERNAL_TIMEOUT_PASS: status=%08x id=%u (no record-window access)\r\n",
              (unsigned)status,(unsigned)old_id);
    reg_write(ADC_REG_CONTROL,ADC_CONTROL_RESET);
    if(wait_idle())return -1;
    if(reg_read(ADC_REG_STATUS)!=ADC_STATUS_INTERNAL || reg_read(ADC_REG_CAPTURE_ID)!=old_id)return -1;
    reg_write(ADC_REG_TEST_CONTROL,1);
    return capture(recovery,"recovery");
}
int main(void) {
    if(board_init())board_halt("ADC board initialization failed");
    board_log("ADC_INTERNAL_SELFTEST: INTERNAL DIGITAL source only; no real ADC or analog validation\r\n");
    if(board_video_init())board_halt("ADC preserved video initialization failed");
    if(reg_read(ADC_REG_ID)!=0x53414443u || reg_read(ADC_REG_ABI)!=0x00010000u ||
       reg_read(ADC_REG_DEPTH)!=ADC_RECORD_WORDS || reg_read(ADC_REG_BYTES)!=ADC_RECORD_BYTES ||
       reg_read(ADC_REG_TIMEOUT_CYCLES)!=1000000u || !(reg_read(ADC_REG_STATUS)&ADC_STATUS_INTERNAL))
        board_halt("ADC candidate ABI mismatch");
    if(wait_idle())board_halt("ADC initial reset timeout");
    reg_write(ADC_REG_CONTROL,ADC_CONTROL_RESET);
    if(wait_idle() || reg_read(ADC_REG_STATUS)!=ADC_STATUS_INTERNAL)board_halt("ADC initial reset failed");
    reg_write(ADC_REG_TEST_CONTROL,1);
    if(capture((uint32_t*)EXPORT0,"copy0") || capture((uint32_t*)EXPORT1,"copy1"))
        board_halt("ADC record selftest failed");
    if(timeout_recovery())board_halt("ADC timeout/recovery selftest failed");
    if(board_video_errors() || (board_video_signals()&9u)!=9u)board_halt("ADC video coexistence check failed");
    board_log("ADC_INTERNAL_SELFTEST_PASS: two DDR exports retained; timeout/reset/recovery; internal DIGITAL only\r\n");
    for(;;)__asm__ volatile("wfi");
}
