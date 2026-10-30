"""Offline adapter regression harness. No hardware access or downloads."""
from pathlib import Path
import hashlib,json,subprocess
root=Path(__file__).resolve().parents[1]
out=Path('D:/FPGA_Project/SP2_Storage_Build/adapter-tests')
out.mkdir(parents=True,exist_ok=True)
mock=r'''
#ifndef ADAPTER_MOCK_H
#define ADAPTER_MOCK_H
#include <stddef.h>
#include <stdint.h>
typedef uint8_t BYTE;typedef uint16_t WORD;typedef uint32_t DWORD;typedef unsigned UINT;
typedef uint8_t DSTATUS;typedef int DRESULT;typedef uint32_t u32;
typedef void *QueueHandle_t;typedef void *TaskHandle_t;
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
#define XST_SUCCESS 0
#define STA_NOINIT 1
#define STA_NODISK 2
#define RES_OK 0
#define RES_ERROR 1
#define RES_WRPRT 2
#define RES_NOTRDY 3
#define RES_PARERR 4
#define CTRL_SYNC 0
#define GET_SECTOR_COUNT 1
#define GET_SECTOR_SIZE 2
#define GET_BLOCK_SIZE 3
#define FF_FS_READONLY 0
#define FF_USE_MKFS 0
#define FF_FS_EXFAT 0
#define XPAR_PS7_SD_0_BASEADDR 0xE0100000
#define XPAR_PS7_SD_0_HAS_CD 1
#define XPAR_PS7_SD_0_HAS_WP 0
#define XPAR_XSDPS_0_DEVICE_ID 0
#define XSDPS_PSR_CARD_INSRT_MASK 0x10000
#define FA_READ 1
#define FA_WRITE 2
#define FA_CREATE_NEW 4
#define AM_DIR 16
typedef enum {FR_OK,FR_DISK_ERR,FR_INT_ERR,FR_NOT_READY,FR_NO_FILE,FR_NO_PATH,FR_INVALID_NAME,FR_DENIED,FR_EXIST,FR_INVALID_OBJECT,FR_WRITE_PROTECTED,FR_INVALID_DRIVE,FR_NOT_ENABLED,FR_NO_FILESYSTEM,FR_MKFS_ABORTED,FR_TIMEOUT,FR_LOCKED,FR_NOT_ENOUGH_CORE,FR_TOO_MANY_OPEN_FILES,FR_INVALID_PARAMETER} FRESULT;
typedef struct{unsigned fs_type;} FATFS;
typedef struct{int dummy;} FIL;
typedef struct{int dummy;} DIR;
typedef struct{char fname[32];DWORD fsize;BYTE fattrib;} FILINFO;
typedef struct{u32 BaseAddress;} XSdPs_Config;
typedef struct{XSdPs_Config Config;u32 HCS,SectorCount,CardID[4];} XSdPs;
QueueHandle_t xQueueCreate(unsigned,size_t);
int xQueueSend(QueueHandle_t,const void*,unsigned);
int xQueueReceive(QueueHandle_t,void*,unsigned);
void vQueueDelete(QueueHandle_t);
int xTaskCreate(void(*)(void*),const char*,unsigned,void*,unsigned,TaskHandle_t*);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
XSdPs_Config *XSdPs_LookupConfig(unsigned);
int XSdPs_CfgInitialize(XSdPs*,XSdPs_Config*,u32);
u32 XSdPs_GetPresentStatusReg(u32);
int XSdPs_CardInitialize(XSdPs*);
int XSdPs_ReadPolled(XSdPs*,u32,UINT,BYTE*);
int XSdPs_WritePolled(XSdPs*,u32,UINT,const BYTE*);
FRESULT f_mount(FATFS*,const char*,BYTE);
FRESULT f_mkdir(const char*);
FRESULT f_stat(const char*,FILINFO*);
FRESULT f_open(FIL*,const char*,BYTE);
FRESULT f_read(FIL*,void*,UINT,UINT*);
FRESULT f_write(FIL*,const void*,UINT,UINT*);
FRESULT f_sync(FIL*);
FRESULT f_close(FIL*);
FRESULT f_rename(const char*,const char*);
FRESULT f_opendir(DIR*,const char*);
FRESULT f_readdir(DIR*,FILINFO*);
FRESULT f_closedir(DIR*);
#endif
'''
(out/'adapter_mock.h').write_text(mock)
for name in ['FreeRTOS.h','task.h','queue.h','xparameters.h','xsdps.h','ff.h','diskio.h']:
    (out/name).write_text('#include "adapter_mock.h"\n')
exe=out/'board_storage_test.exe'
cmd=['D:/MinGW/bin/gcc.exe','-std=c11','-Wall','-Wextra','-Werror','-I'+str(out),'-I'+str(root/'src'),'-I'+str(root/'firmware/lvgl'),str(root/'tests/board_storage_test.c'),'-o',str(exe)]
subprocess.run(cmd,check=True)
result=subprocess.run([str(exe)],check=True,capture_output=True,text=True)
print(result.stdout,end='')
(out/'test-status.json').write_text(json.dumps({
    'state':'verified-offline-only','hardware_access':False,'compiler':cmd,
    'stdout':result.stdout,'source_sha256':{
        str(path):hashlib.sha256(path.read_bytes()).hexdigest() for path in [
            root/'firmware/storage/board_storage.c',root/'firmware/storage/board_storage.h',
            root/'tests/board_storage_test.c',Path(__file__).resolve()]}},indent=2),encoding='utf-8')
