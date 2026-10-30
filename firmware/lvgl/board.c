#include "board.h"
#include "portable.h"
#include "frame_source.h"
#include "memory_probe.h"
#include "xparameters.h"
#include "xil_cache.h"
#include "xuartps.h"
#include "xaxivdma.h"
#include "xvtc.h"
#include "xgpio.h"
#include "xtime_l.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

_Static_assert(SCREEN_WIDTH==SCOPE_PORT_WIDTH && SCREEN_HEIGHT==SCOPE_PORT_HEIGHT,"Geometry mismatch");
static XUartPs uart;
static XAxiVdma dma;
static XVtc vtc;
static XGpio gpio;
static unsigned front,frames;
static int video_failed;
static void frame_cache(uintptr_t address,int invalidate) {
    /* The 2020.2 A9 BSP masks IRQ/FIQ for an entire range operation. Keep
       each critical section short so a full RGB frame cannot lose RTOS ticks. */
    for(unsigned offset=0;offset<SCREEN_FRAME_BYTES;offset+=16384u) {
        unsigned length=SCREEN_FRAME_BYTES-offset;
        if(length>16384u)length=16384u;
        if(invalidate)Xil_DCacheInvalidateRange(address+offset,length);
        else Xil_DCacheFlushRange(address+offset,length);
    }
    __asm__ volatile("dsb sy" ::: "memory");
}

uint32_t board_millis(void) {
    XTime now;XTime_GetTime(&now);
    return (uint32_t)(now/(COUNTS_PER_SECOND/1000u));
}
void board_log(const char *format,...) {
    char text[SCOPE_LOG_CAPACITY];va_list args;va_start(args,format);
    int length=vsnprintf(text,sizeof(text),format,args);va_end(args);
    if(length<0||(size_t)length>=sizeof(text))strcpy(text,"APP_LOG_ERROR: format/truncation\r\n");
    for(const char *p=text;*p;p++)XUartPs_SendByte(uart.Config.BaseAddress,(uint8_t)*p);
}
static void raw_text(const char *text) {
    if(text)for(;*text;text++)XUartPs_SendByte(uart.Config.BaseAddress,(uint8_t)*text);
}
void board_panic(const char *reason,const char *detail) {
    __asm__ volatile("cpsid if" ::: "memory");
    raw_text("\r\nAPP_FATAL: ");raw_text(reason);
    if(detail){raw_text(" : ");raw_text(detail);}raw_text("\r\n");
    for(;;)__asm__ volatile("wfi");
}
void board_halt(const char *reason) {board_panic(reason,NULL);}
int board_uart_get(void) {
    if(!XUartPs_IsReceiveData(uart.Config.BaseAddress))return -1;
    return XUartPs_RecvByte(uart.Config.BaseAddress);
}
int board_init(void) {
    Xil_DCacheDisable();
    XUartPs_Config *config=XUartPs_LookupConfig(XPAR_PS7_UART_1_DEVICE_ID);
    if(!config || XUartPs_CfgInitialize(&uart,config,config->BaseAddress)!=XST_SUCCESS)return -1;
    if(XUartPs_SetBaudRate(&uart,115200)!=XST_SUCCESS)return -1;
    board_log("SP2_LVGL_UART_READY: 115200 8N1\r\n");
    volatile uint32_t *regions[]={(volatile uint32_t*)0x01000000u,
        (volatile uint32_t*)0x11000000u,(volatile uint32_t*)0x1f000000u};
    size_t region=0,word=0;
    if(screen_memory_probe(regions,3,16384,&region,&word)) {
        board_log("DDR_FAIL: region=%u word=%u\r\n",(unsigned)region,(unsigned)word);return -1;
    }
    board_log("DDR_SAMPLE_PASS: 3x64KiB, not full-capacity/stress\r\n");
    Xil_DCacheEnable();return 0;
}
uint32_t board_video_signals(void) {return XGpio_DiscreteRead(&gpio,1);}
uint32_t board_video_errors(void) {return XAxiVdma_GetDmaChannelErrors(&dma,XAXIVDMA_READ);}
static int healthy(void) {
    return !board_video_errors() && !(XAxiVdma_ReadReg(dma.BaseAddr,4)&1u);
}
void board_video_stop(void) {
    video_failed=1;XAxiVdma_DmaStop(&dma,XAXIVDMA_READ);
    uint32_t start=board_millis();
    while(!(XAxiVdma_ReadReg(dma.BaseAddr,4)&1u)) {
        if((uint32_t)(board_millis()-start)>100) {board_log("VIDEO_STOP_TIMEOUT: frame slots remain reserved\r\n");return;}
        board_delay(1);
    }
    board_log("VIDEO_STOPPED: frame slots will not be reused\r\n");
}
int board_video_init(void) {
    front=frames=0;video_failed=0;
    memset((void*)SCREEN_FB0,0,SCREEN_FRAME_BYTES);
    memset((void*)SCREEN_FB1,0,SCREEN_FRAME_BYTES);
    frame_cache(SCREEN_FB0,0);
    frame_cache(SCREEN_FB1,0);
    __asm__ volatile("dsb sy" ::: "memory");
    if(XGpio_Initialize(&gpio,XPAR_STATUS_GPIO_DEVICE_ID)!=XST_SUCCESS)return -1;
    XGpio_SetDataDirection(&gpio,1,15);
    uint32_t start=board_millis();
    while(!(board_video_signals()&8u)) {
        if((uint32_t)(board_millis()-start)>1000)return -1;
        board_delay(10);
    }
    XAxiVdma_Config *dcfg=XAxiVdma_LookupConfig(XPAR_VDMA_DEVICE_ID);
    XVtc_Config *vcfg=XVtc_LookupConfig(XPAR_VTC_DEVICE_ID);
    if(!dcfg || !vcfg)return -1;
    if(XAxiVdma_CfgInitialize(&dma,dcfg,dcfg->BaseAddress)!=XST_SUCCESS)return -1;
    if(XVtc_CfgInitialize(&vtc,vcfg,vcfg->BaseAddress)!=XST_SUCCESS)return -1;
    XAxiVdma_Reset(&dma,XAXIVDMA_READ);start=board_millis();
    while(XAxiVdma_ResetNotDone(&dma,XAXIVDMA_READ)) {
        if((uint32_t)(board_millis()-start)>100)return -1;
        board_delay(1);
    }
    XAxiVdma_DmaSetup setup={0};
    setup.VertSizeInput=SCREEN_HEIGHT;setup.HoriSizeInput=SCREEN_STRIDE;
    setup.Stride=SCREEN_STRIDE;setup.EnableCircularBuf=0;setup.FixedFrameStoreAddr=0;
    UINTPTR addresses[2]={SCREEN_FB0,SCREEN_FB1};
    if(XAxiVdma_DmaConfig(&dma,XAXIVDMA_READ,&setup)!=XST_SUCCESS)return -1;
    if(XAxiVdma_DmaSetBufferAddr(&dma,XAXIVDMA_READ,addresses)!=XST_SUCCESS)return -1;
    XVtc_Timing timing={0};
    timing.HActiveVideo=1024;timing.HFrontPorch=160;timing.HSyncWidth=20;timing.HBackPorch=140;
    timing.VActiveVideo=600;timing.V0FrontPorch=12;timing.V0SyncWidth=3;timing.V0BackPorch=20;
    timing.V1FrontPorch=12;timing.V1SyncWidth=3;timing.V1BackPorch=20;
    timing.HSyncPolarity=1;timing.VSyncPolarity=1;
    XVtc_SourceSelect source;memset(&source,1,sizeof(source));source.InterlacedMode=0;
    XVtc_DisableGenerator(&vtc);XVtc_SetGeneratorTiming(&vtc,&timing);
    XVtc_SetSource(&vtc,&source);XVtc_RegUpdateEnable(&vtc);
    if(XAxiVdma_DmaStart(&dma,XAXIVDMA_READ)!=XST_SUCCESS)return -1;
    XVtc_EnableGenerator(&vtc);start=board_millis();
    while((board_video_signals()&9u)!=9u) {
        if(!healthy() || (uint32_t)(board_millis()-start)>1000){board_video_stop();return -1;}
        board_delay(10);
    }
    if(XAxiVdma_CurrFrameStore(&dma,XAXIVDMA_READ)!=0 || !healthy()){board_video_stop();return -1;}
    board_log("VIDEO_BASE_READY: RGB888 1024x600, two parked frame stores\r\n");return 0;
}
int board_video_present(const uint8_t *xrgb) {
    if(video_failed)return -1;
    if(!healthy() || XAxiVdma_CurrFrameStore(&dma,XAXIVDMA_READ)!=front)goto fail;
    unsigned target=front^1u;
    uintptr_t address=target?SCREEN_FB1:SCREEN_FB0;
    if(scope_pack_rgb888((uint8_t*)address,SCREEN_FRAME_BYTES,xrgb,SCOPE_XRGB_BYTES))goto fail;
    frame_cache(address,0);
    __asm__ volatile("dsb sy" ::: "memory");
    if(XAxiVdma_StartParking(&dma,(int)target,XAXIVDMA_READ)!=XST_SUCCESS)goto fail;
    uint32_t start=board_millis();
    for(;;) {
        if(!healthy())goto fail;
        if(XAxiVdma_CurrFrameStore(&dma,XAXIVDMA_READ)==target) {
            if(!healthy())goto fail;
            front=target;frames++;return 0;
        }
        if((uint32_t)(board_millis()-start)>100)goto fail;
        board_delay(1);
    }
fail:
    board_log("VIDEO_PRESENT_FAIL: frame=%u errors=%08x\r\n",frames,(unsigned)board_video_errors());
    board_video_stop();return -1;
}
uint32_t board_video_front_address(void) {return front?SCREEN_FB1:SCREEN_FB0;}
unsigned board_video_frames(void) {return frames;}
uint32_t board_video_crc(void) {
    uintptr_t address=board_video_front_address();
    frame_cache(address,1);
    __asm__ volatile("dsb sy" ::: "memory");
    return screen_crc32((const uint8_t*)address,SCREEN_FRAME_BYTES);
}
