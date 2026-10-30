/* Temporary DDR-only probe. Protocol module exposes read operations only. */
#include "qspi_readonly.h"
#include "xqspips.h"
#include "xparameters.h"
#include "xil_cache.h"
#include "xil_io.h"
#include "xil_printf.h"
#include "xstatus.h"

#define BUFFER_ADDRESS UINT32_C(0x14000000)
static XQspiPs controller;
static int transfer(void *context,const uint8_t *tx,uint8_t *rx,unsigned count){
    /* Vendor API is not const-qualified, but does not modify the TX buffer. */
    return XQspiPs_PolledTransfer((XQspiPs *)context,(u8 *)tx,(u8 *)rx,(u32)count)==XST_SUCCESS?0:-1;
}
static BootFlashReadOnly flash={.transfer=transfer,.context=&controller};
static void fail(const char *reason){
    xil_printf("QSPI_READONLY_FAIL %s\r\n",reason);
    for(;;){}
}
int main(void){
    uint8_t id[3],status[3];
    XQspiPs_Config *config=XQspiPs_LookupConfig(XPAR_XQSPIPS_0_DEVICE_ID);
    xil_printf("QSPI_READONLY_START boot_mode=%lu\r\n",(unsigned long)(Xil_In32(0xf800025c)&7));
    if(!config||config->ConnectionMode!=XQSPIPS_CONNECTION_MODE_SINGLE)fail("configuration");
    if(XQspiPs_CfgInitialize(&controller,config,config->BaseAddress)!=XST_SUCCESS)fail("initialize");
    if(XQspiPs_SetOptions(&controller,XQSPIPS_MANUAL_START_OPTION|XQSPIPS_FORCE_SSELECT_OPTION|XQSPIPS_HOLD_B_DRIVE_OPTION)!=XST_SUCCESS)fail("options");
    if(XQspiPs_SetClkPrescaler(&controller,XQSPIPS_CLK_PRESCALE_16)!=XST_SUCCESS)fail("clock");
    if(XQspiPs_SetSlaveSelect(&controller)!=XST_SUCCESS)fail("select");
    if(boot_flash_read_id(&flash,id))fail("id-read");
    for(unsigned i=0;i<3;i++)if(boot_flash_read_status(&flash,i+1,&status[i]))fail("status-read");
    xil_printf("QSPI_READONLY_ID jedec=%02x%02x%02x sr1=%02x sr2=%02x sr3=%02x\r\n",
        (unsigned)id[0],(unsigned)id[1],(unsigned)id[2],(unsigned)status[0],(unsigned)status[1],(unsigned)status[2]);
    if(id[0]!=0xef||id[1]!=0x40||id[2]!=0x18)fail("unexpected-device-no-data-read");
    if(status[0]&1u)fail("device-busy");
    uint8_t *buffer=(uint8_t *)(UINTPTR)BUFFER_ADDRESS;
    for(uint32_t address=0;address<BOOT_FLASH_CAPACITY;address+=UINT32_C(0x100000)){
        if(boot_flash_read(&flash,address,buffer+address,UINT32_C(0x100000)))fail("data-read");
        xil_printf("QSPI_READONLY_PROGRESS bytes=%lu\r\n",(unsigned long)(address+UINT32_C(0x100000)));
    }
    uint32_t crc=boot_flash_crc32(buffer,BOOT_FLASH_CAPACITY);
    Xil_DCacheFlushRange((INTPTR)buffer,BOOT_FLASH_CAPACITY);
    xil_printf("QSPI_READONLY_READY address=%08x bytes=%lu crc32=%08x\r\n",(unsigned)BUFFER_ADDRESS,(unsigned long)BOOT_FLASH_CAPACITY,(unsigned)crc);
    for(;;){}
}
