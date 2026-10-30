/* Offline-built candidate. No board run, ADC, FreeRTOS, or LVGL is implied. */
#include "xparameters.h"
#include "xil_printf.h"
#include "xil_cache.h"
#include "xuartps.h"
#include "sleep.h"
#include "memory_probe.h"
#ifndef PROBE_ONLY
#include "xaxivdma.h"
#include "xvtc.h"
#include "xgpio.h"
#include "frame_source.h"
#include <string.h>
#endif

static XUartPs uart;
static int init_uart(void) {
    XUartPs_Config *cfg=XUartPs_LookupConfig(XPAR_PS7_UART_1_DEVICE_ID);
    if(!cfg || XUartPs_CfgInitialize(&uart,cfg,cfg->BaseAddress)!=XST_SUCCESS)return -1;
    return XUartPs_SetBaudRate(&uart,115200);
}
static int check_ddr(void) {
    volatile uint32_t *regions[]={(volatile uint32_t*)0x01000000u,
        (volatile uint32_t*)0x11000000u,(volatile uint32_t*)0x1f000000u};
    size_t region=0,word=0;
    Xil_DCacheDisable();
    xil_printf("DDR_TEST_BEGIN: 3 x 64KiB reserved regions, cache disabled\r\n");
    int result=screen_memory_probe(regions,3,16384,&region,&word);
    __asm__ volatile("dsb sy" ::: "memory");
    if(result) {
        xil_printf("DDR_FAIL: rc=%d region=%d word=%d\r\n",result,(int)region,(int)word);
        return -1;
    }
    xil_printf("DDR_PATTERN_PASS: complements and selected high-address aliases checked\r\n");
    xil_printf("DDR_TEST_LIMIT: not a full-capacity or stress test\r\n");
    return 0;
}

#ifndef PROBE_ONLY
static XAxiVdma dma;
static XVtc vtc;
static XGpio status_gpio;
static int start_video(void) {
    Xil_DCacheEnable();
    uint8_t *frame0=(uint8_t*)SCREEN_FB0,*frame1=(uint8_t*)SCREEN_FB1;
    if(screen_fill(frame0,SCREEN_FRAME_BYTES,SCREEN_COLORBARS) ||
       screen_fill(frame1,SCREEN_FRAME_BYTES,SCREEN_GRID))return -1;
    Xil_DCacheFlushRange(SCREEN_FB0,SCREEN_FRAME_BYTES);
    Xil_DCacheFlushRange(SCREEN_FB1,SCREEN_FRAME_BYTES);
    __asm__ volatile("dsb sy" ::: "memory");
    Xil_DCacheInvalidateRange(SCREEN_FB0,SCREEN_FRAME_BYTES);
    Xil_DCacheInvalidateRange(SCREEN_FB1,SCREEN_FRAME_BYTES);
    uint32_t crc0=screen_crc32(frame0,SCREEN_FRAME_BYTES),crc1=screen_crc32(frame1,SCREEN_FRAME_BYTES);
    xil_printf("FB0_COLORBARS_CRC32=%08x FB1_GRID_CRC32=%08x\r\n",crc0,crc1);
    if(crc0!=SCREEN_COLORBARS_CRC32 || crc1!=SCREEN_GRID_CRC32) {
        xil_printf("FRAMEBUFFER_DDR_READBACK_FAIL: display not started\r\n");return -1;
    }
    /* Read diagnostics before touching the pixel-clock-dependent VDMA. */
    xil_printf("VIDEO_CTRL_CHECK_BEGIN\r\n");
    if(XGpio_Initialize(&status_gpio,XPAR_STATUS_GPIO_DEVICE_ID)!=XST_SUCCESS)return -1;
    XGpio_SetDataDirection(&status_gpio,1,0xf);
    u32 clock_status=0;
    for(unsigned wait=0;wait<100;wait++) {
        clock_status=XGpio_DiscreteRead(&status_gpio,1);
        if(clock_status&8)break;
        usleep(10000);
    }
    xil_printf("VIDEO_CTRL_READY: status=%08x pixel_clock_locked=%d\r\n",clock_status,(int)((clock_status>>3)&1));
    if(!(clock_status&8)) {
        xil_printf("VIDEO_PIXEL_CLOCK_NOT_LOCKED: VDMA not accessed\r\n");return -1;
    }
    XAxiVdma_Config *dcfg=XAxiVdma_LookupConfig(XPAR_VDMA_DEVICE_ID);
    XVtc_Config *vcfg=XVtc_LookupConfig(XPAR_VTC_DEVICE_ID);
    if(!dcfg || !vcfg)return -1;
    if(XAxiVdma_CfgInitialize(&dma,dcfg,dcfg->BaseAddress)!=XST_SUCCESS)return -1;
    if(XVtc_CfgInitialize(&vtc,vcfg,vcfg->BaseAddress)!=XST_SUCCESS)return -1;
    XAxiVdma_Reset(&dma,XAXIVDMA_READ);
    unsigned timeout=1000000;
    while(XAxiVdma_ResetNotDone(&dma,XAXIVDMA_READ) && timeout)--timeout;
    if(!timeout)return -1;
    XAxiVdma_DmaSetup setup;
    memset(&setup,0,sizeof(setup));
    setup.VertSizeInput=SCREEN_HEIGHT;setup.HoriSizeInput=SCREEN_STRIDE;
    setup.Stride=SCREEN_STRIDE;setup.EnableCircularBuf=0;setup.FixedFrameStoreAddr=0;
    UINTPTR buffers[2]={SCREEN_FB0,SCREEN_FB1};
    if(XAxiVdma_DmaConfig(&dma,XAXIVDMA_READ,&setup)!=XST_SUCCESS)return -1;
    if(XAxiVdma_DmaSetBufferAddr(&dma,XAXIVDMA_READ,buffers)!=XST_SUCCESS)return -1;
    XVtc_Timing timing;
    memset(&timing,0,sizeof(timing));
    timing.HActiveVideo=1024;timing.HFrontPorch=160;timing.HSyncWidth=20;timing.HBackPorch=140;
    timing.VActiveVideo=600;timing.V0FrontPorch=12;timing.V0SyncWidth=3;timing.V0BackPorch=20;
    timing.V1FrontPorch=12;timing.V1SyncWidth=3;timing.V1BackPorch=20;
    /* PG044 requires active-high VTG signals. RGB IO inverts only at panel pins. */
    timing.HSyncPolarity=1;timing.VSyncPolarity=1;timing.Interlaced=0;
    XVtc_SourceSelect source;
    memset(&source,1,sizeof(source));source.InterlacedMode=0;
    XVtc_DisableGenerator(&vtc);
    XVtc_SetGeneratorTiming(&vtc,&timing);
    XVtc_SetSource(&vtc,&source);
    XVtc_RegUpdateEnable(&vtc);
    if(XAxiVdma_DmaStart(&dma,XAXIVDMA_READ)!=XST_SUCCESS)return -1;
    XVtc_EnableGenerator(&vtc);
    xil_printf("VIDEO_STARTED: 1024x600 RGB888, stride 3072, parked on FB0\r\n");
    unsigned stable=0;
    for(unsigned second=0;second<5;second++) {
        sleep(1);
        u32 signals=XGpio_DiscreteRead(&status_gpio,1);
        u32 errors=XAxiVdma_GetDmaChannelErrors(&dma,XAXIVDMA_READ);
        xil_printf("VIDEO_DIAG: locked=%d underflow_sample=%d overflow_sample=%d pixel_clock_locked=%d dma_errors=%08x\r\n",
          (int)(signals&1),(int)((signals>>1)&1),(int)((signals>>2)&1),(int)((signals>>3)&1),errors);
        if(errors){XAxiVdma_DmaStop(&dma,XAXIVDMA_READ);return -1;}
        stable=(signals&9)==9 ? stable+1 : 0;
        if(stable>=3) {
            xil_printf("VIDEO_INTERNAL_LOCK_STABLE: 3 one-second samples; panel presence/color/touch are NOT verified\r\n");
            return 0;
        }
    }
    XAxiVdma_DmaStop(&dma,XAXIVDMA_READ);
    return -1;
}
#endif

int main(void) {
    Xil_DCacheDisable();
    if(init_uart()!=XST_SUCCESS)return 1;
    xil_printf("\r\nSP2_PS_UART_READY: V1.3B, UART1 MIO52/53, 115200 8N1\r\n");
    xil_printf("CANDIDATE_DDR: TW-107 hardware, RE-125 timing profile, x16 512MiB ~400MHz; reserved-region check follows\r\n");
#ifdef PROBE_ONLY
    xil_printf("OCM_PROBE_READY: DDR has not been accessed by this application\r\n");
    xil_printf("WAIT_D: send uppercase D only AFTER the debugger initialized DDR\r\n");
    while(XUartPs_RecvByte(uart.Config.BaseAddress)!='D'){}
#endif
    if(check_ddr()) {
        xil_printf("BRINGUP_STOP: DDR failure; display not started\r\n");
        for(;;)sleep(1);
    }
#ifdef PROBE_ONLY
    xil_printf("OCM_PROBE_DONE: DDR sample passed; video not started\r\n");
#else
    if(start_video())xil_printf("BRINGUP_STOP: video configuration/status failure\r\n");
#endif
    for(;;)sleep(1);
}
