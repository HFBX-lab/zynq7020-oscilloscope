#include "xparameters.h"
#include "xil_printf.h"
#include "xil_exception.h"
#include "xscugic.h"
#include "xscutimer.h"
#include "xuartps.h"
#include "xsdps.h"
#include "ff.h"
#include "diskio.h"

#if FF_FS_READONLY != 1 || FF_USE_MKFS != 0 || FF_FS_EXFAT != 0
#error Readonly probe requires read_only=true, use_mkfs=false, exfat=false
#endif
#define PROBE_TIMEOUT_SECONDS 30U
#define DIRECTORY_LIMIT 1024U
#define UART_POLL_LIMIT 100000U
static volatile unsigned uart_failed;
/* Override vendor outbyte: a full/broken UART must never hang the probe. */
void outbyte(char c) {
    unsigned polls=UART_POLL_LIMIT;
    if (uart_failed) return;
    while (XUartPs_IsTransmitFull(XPAR_PS7_UART_1_BASEADDR) && polls) --polls;
    if (!polls) {uart_failed=1U;return;}
    XUartPs_WriteReg(XPAR_PS7_UART_1_BASEADDR,XUARTPS_FIFO_OFFSET,(u32)(unsigned char)c);
}
static XSdPs sd;
static XScuTimer timer;
static XScuGic gic;
static DSTATUS media_state=STA_NOINIT;
static volatile unsigned seconds;
static FATFS fs;
static DIR directory;
static FILINFO info;

static void idle(void) {
    if (timer.Config.BaseAddr) XScuTimer_Stop(&timer);
    Xil_ExceptionDisable();
    for (;;) { __asm__ volatile("wfi"); }
}
static void timeout_tick(void *ignored) {
    (void)ignored;
    XScuTimer_ClearInterruptStatus(&timer);
    if (++seconds >= PROBE_TIMEOUT_SECONDS) {
        xil_printf("PROBE_TIMEOUT seconds=%u board_test=incomplete\r\n",seconds);
        idle();
    }
}
static int arm_timeout(void) {
    XScuGic_Config *gc=XScuGic_LookupConfig(XPAR_SCUGIC_SINGLE_DEVICE_ID);
    XScuTimer_Config *tc=XScuTimer_LookupConfig(XPAR_SCUTIMER_DEVICE_ID);
    if (!gc || !tc || XScuGic_CfgInitialize(&gic,gc,gc->CpuBaseAddress)!=XST_SUCCESS ||
        XScuTimer_CfgInitialize(&timer,tc,tc->BaseAddr)!=XST_SUCCESS) return -1;
    Xil_ExceptionInit();
    Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT,(Xil_ExceptionHandler)XScuGic_InterruptHandler,&gic);
    if (XScuGic_Connect(&gic,XPAR_SCUTIMER_INTR,(Xil_InterruptHandler)timeout_tick,&timer)!=XST_SUCCESS) return -1;
    XScuGic_Enable(&gic,XPAR_SCUTIMER_INTR);
    XScuTimer_LoadTimer(&timer,XPAR_CPU_CORTEXA9_0_CPU_CLK_FREQ_HZ/2U);
    XScuTimer_EnableAutoReload(&timer);
    XScuTimer_EnableInterrupt(&timer);
    Xil_ExceptionEnable();
    XScuTimer_Start(&timer);
    return 0;
}
DSTATUS disk_status(BYTE drive) {
    if (drive!=0U) return STA_NOINIT;
    if (media_state==0U && (XSdPs_GetPresentStatusReg(sd.Config.BaseAddress)&XSDPS_PSR_CARD_INSRT_MASK)==0U) media_state=STA_NOINIT|STA_NODISK;
    return media_state;
}
DSTATUS disk_initialize(BYTE drive) {
    XSdPs_Config *cfg;
    if (drive!=0U) return STA_NOINIT;
    if (media_state==0U) return disk_status(drive);
    cfg=XSdPs_LookupConfig(XPAR_XSDPS_0_DEVICE_ID);
    if (!cfg || XSdPs_CfgInitialize(&sd,cfg,cfg->BaseAddress)!=XST_SUCCESS) return STA_NOINIT;
    if ((XSdPs_GetPresentStatusReg(cfg->BaseAddress)&XSDPS_PSR_CARD_INSRT_MASK)==0U) return STA_NOINIT|STA_NODISK;
    if (XSdPs_CardInitialize(&sd)!=XST_SUCCESS) return STA_NOINIT;
    media_state=0U;
    return media_state;
}
DRESULT disk_read(BYTE drive,BYTE *buffer,DWORD sector,UINT count) {
    u32 location=sector;
    if (!buffer || count==0U) return RES_PARERR;
    if (disk_status(drive)!=0U) return RES_NOTRDY;
    if (sector>=sd.SectorCount || count>sd.SectorCount-sector) return RES_PARERR;
    if (sd.HCS==0U) location*=512U;
    return XSdPs_ReadPolled(&sd,location,count,buffer)==XST_SUCCESS?RES_OK:RES_ERROR;
}
DRESULT disk_write(BYTE drive,const BYTE *buffer,DWORD sector,UINT count) {
    (void)drive;(void)buffer;(void)sector;(void)count;
    return RES_WRPRT;
}
DRESULT disk_ioctl(BYTE drive,BYTE command,void *buffer) {
    if (disk_status(drive)!=0U) return RES_NOTRDY;
    if (command==CTRL_SYNC) return RES_OK;
    if (!buffer) return RES_PARERR;
    switch(command) {
    case GET_SECTOR_COUNT:*(DWORD*)buffer=sd.SectorCount;return RES_OK;
    case GET_SECTOR_SIZE:*(WORD*)buffer=512U;return RES_OK;
    case GET_BLOCK_SIZE:*(DWORD*)buffer=1U;return RES_OK;
    default:return RES_PARERR;
    }
}
int main(void) {
    XUartPs uart;XUartPs_Config *uc=XUartPs_LookupConfig(XPAR_XUARTPS_0_DEVICE_ID);
    FRESULT result;unsigned entries=0;
    if (!uc || XUartPs_CfgInitialize(&uart,uc,uc->BaseAddress)!=XST_SUCCESS || XUartPs_SetBaudRate(&uart,115200U)!=XST_SUCCESS) for(;;) {__asm__ volatile("wfi");}
    xil_printf("M5_READONLY_PROBE v1 uart=115200 writes=blocked mkfs=disabled\r\n");
    if (arm_timeout()!=0) {xil_printf("PROBE_FAIL timer\r\n");idle();}
    xil_printf("SD_INIT status=%u\r\n",disk_initialize(0));
    if (media_state!=0U) {xil_printf("PROBE_FAIL card-init\r\n");idle();}
    xil_printf("SD_ID type=%u version=%u hcs=%lu sectors=%lu bytes_hi=%lu bytes_lo=%lu\r\n",sd.CardType,sd.Card_Version,(unsigned long)sd.HCS,(unsigned long)sd.SectorCount,(unsigned long)(((u64)sd.SectorCount*512U)>>32),(unsigned long)((u64)sd.SectorCount*512U));
    xil_printf("SD_CID %08lx %08lx %08lx %08lx\r\n",(unsigned long)sd.CardID[0],(unsigned long)sd.CardID[1],(unsigned long)sd.CardID[2],(unsigned long)sd.CardID[3]);
    xil_printf("SD_CSD %08lx %08lx %08lx %08lx\r\n",(unsigned long)sd.CardSpecData[0],(unsigned long)sd.CardSpecData[1],(unsigned long)sd.CardSpecData[2],(unsigned long)sd.CardSpecData[3]);
    result=f_mount(&fs,"0:/",1);xil_printf("FAT_MOUNT result=%u type=%u volume_sector=%lu cluster_sectors=%u\r\n",result,fs.fs_type,(unsigned long)fs.volbase,fs.csize);
    if (result!=FR_OK) {xil_printf("PROBE_FAIL filesystem\r\n");idle();}
    result=f_opendir(&directory,"0:/");
    if (result!=FR_OK) {xil_printf("PROBE_FAIL opendir=%u\r\n",result);idle();}
    while(entries<DIRECTORY_LIMIT) {
        result=f_readdir(&directory,&info);
        if(result!=FR_OK || !info.fname[0])break;
        xil_printf("ROOT name=%s size=%lu attr=%02x\r\n",info.fname,(unsigned long)info.fsize,info.fattrib);entries++;
    }
    xil_printf("ROOT_END result=%u entries=%u truncated=%u\r\n",result,entries,entries==DIRECTORY_LIMIT);
    {FRESULT close_result=f_closedir(&directory);xil_printf("FAT_CLOSEDIR result=%u\r\n",close_result);}
    xil_printf("PROBE_IDLE writes=0 seconds=%u\r\n",seconds);
    idle();return 0;
}
