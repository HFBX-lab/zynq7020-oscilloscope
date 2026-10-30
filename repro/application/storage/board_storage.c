#include "board_storage.h"
#include "board.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "xparameters.h"
#include "xsdps.h"
#include "ff.h"
#include "diskio.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

#if FF_FS_READONLY != 0 || FF_USE_MKFS != 0 || FF_FS_EXFAT != 0
#error Storage requires writable FAT, mkfs disabled, exfat disabled
#endif
#if XPAR_PS7_SD_0_BASEADDR != 0xE0100000 || XPAR_PS7_SD_0_HAS_CD != 1 || XPAR_PS7_SD_0_HAS_WP != 0
#error Storage hardware mapping mismatch
#endif
typedef struct {
    ScopeTfResponse ui;
    uint32_t cid[4],sectors;
    unsigned fat;
} Completion;
static XSdPs sd;
static DSTATUS media_state=STA_NOINIT;
static FATFS fs;
static FIL file;
static DIR directory;
static bool file_open,directory_open,write_allowed,in_flight;
static ScopeStorageUI *panel;
static QueueHandle_t requests,responses;
static TaskHandle_t worker_handle;
static uint8_t snapshot[SCOPE_RECORD_BLOB_BYTES],result_blob[SCOPE_RECORD_BLOB_BYTES];
static Completion last;
static ScopeStoreKind last_kind;
static unsigned active_sequence;
static ScopeStorage storage;

DWORD get_fattime(void){
    /* No RTC is configured. Files receive a fixed, valid 2000-01-01 date;
     * this is an explicit placeholder, never a claimed acquisition time. */
    return (20U<<25)|(1U<<21)|(1U<<16);
}

DSTATUS disk_status(BYTE drive){
    if(drive!=0U)return STA_NOINIT;
    if(media_state==0U&&(XSdPs_GetPresentStatusReg(sd.Config.BaseAddress)&XSDPS_PSR_CARD_INSRT_MASK)==0U)
        media_state=STA_NOINIT|STA_NODISK;
    return media_state;
}
DSTATUS disk_initialize(BYTE drive){
    XSdPs_Config *cfg;
    if(drive!=0U)return STA_NOINIT;
    if(media_state==0U)return disk_status(drive);
    cfg=XSdPs_LookupConfig(XPAR_XSDPS_0_DEVICE_ID);
    if(!cfg||XSdPs_CfgInitialize(&sd,cfg,cfg->BaseAddress)!=XST_SUCCESS)return STA_NOINIT;
    if((XSdPs_GetPresentStatusReg(cfg->BaseAddress)&XSDPS_PSR_CARD_INSRT_MASK)==0U)return media_state=STA_NOINIT|STA_NODISK;
    if(XSdPs_CardInitialize(&sd)!=XST_SUCCESS)return media_state=STA_NOINIT;
    return media_state=0U;
}
static bool sector_range(DWORD sector,UINT count){
    return count&&sector<sd.SectorCount&&count<=sd.SectorCount-sector&&
        (sd.HCS||sector<=UINT32_MAX/512U)&&
        (sd.HCS||count-1U<=UINT32_MAX/512U-sector);
}
DRESULT disk_read(BYTE drive,BYTE *buffer,DWORD sector,UINT count){
    if(!buffer||!count)return RES_PARERR;
    if(disk_status(drive)!=0U)return RES_NOTRDY;
    if(!sector_range(sector,count))return RES_PARERR;
    return XSdPs_ReadPolled(&sd,sd.HCS?sector:sector*512U,count,buffer)==XST_SUCCESS?RES_OK:RES_ERROR;
}
DRESULT disk_write(BYTE drive,const BYTE *buffer,DWORD sector,UINT count){
    if(!write_allowed||xTaskGetCurrentTaskHandle()!=worker_handle)return RES_WRPRT;
    if(!buffer||!count)return RES_PARERR;
    if(disk_status(drive)!=0U)return RES_NOTRDY;
    if(!sector_range(sector,count))return RES_PARERR;
    return XSdPs_WritePolled(&sd,sd.HCS?sector:sector*512U,count,buffer)==XST_SUCCESS?RES_OK:RES_ERROR;
}
DRESULT disk_ioctl(BYTE drive,BYTE command,void *buffer){
    if(disk_status(drive)!=0U)return RES_NOTRDY;
    /* Polled transfers have completed before this call. No TRIM/format API. */
    if(command==CTRL_SYNC)return RES_OK;
    if(!buffer)return RES_PARERR;
    switch(command){
    case GET_SECTOR_COUNT:*(DWORD*)buffer=sd.SectorCount;return RES_OK;
    case GET_SECTOR_SIZE:*(WORD*)buffer=512U;return RES_OK;
    case GET_BLOCK_SIZE:*(DWORD*)buffer=1U;return RES_OK;
    default:return RES_PARERR;
    }
}
static ScopeStoreResult map_result(FRESULT r,bool allocating){
    switch(r){
    case FR_OK:return SCOPE_STORE_OK;
    case FR_NO_FILE:case FR_NO_PATH:return SCOPE_STORE_NOT_FOUND;
    case FR_NOT_READY:return SCOPE_STORE_NO_CARD;
    case FR_NO_FILESYSTEM:return SCOPE_STORE_NO_FS;
    case FR_WRITE_PROTECTED:return SCOPE_STORE_READ_ONLY;
    case FR_DENIED:return allocating?SCOPE_STORE_FULL:SCOPE_STORE_IO;
    case FR_INVALID_NAME:case FR_INVALID_PARAMETER:return SCOPE_STORE_INVALID;
    default:return SCOPE_STORE_IO;
    }
}
static ScopeStoreResult op_stat(void *c,const char *path,bool *exists,bool *isdir,uint32_t *bytes){
    FILINFO info;FRESULT r;(void)c;
    r=f_stat(path,&info);
    if(r==FR_NO_FILE||r==FR_NO_PATH){*exists=false;*isdir=false;*bytes=0;return SCOPE_STORE_OK;}
    if(r!=FR_OK)return map_result(r,false);
    *exists=true;*isdir=(info.fattrib&AM_DIR)!=0;*bytes=(uint32_t)info.fsize;return SCOPE_STORE_OK;
}
static ScopeStoreResult op_mkdir(void *c,const char *path){
    FRESULT r=f_mkdir(path);
    if(r==FR_EXIST){bool exists=false,isdir=false;uint32_t bytes=0;
        ScopeStoreResult s=op_stat(c,path,&exists,&isdir,&bytes);
        return s!=SCOPE_STORE_OK?s:exists&&isdir?SCOPE_STORE_OK:SCOPE_STORE_INVALID;
    }
    return map_result(r,true);
}
static ScopeStoreResult op_open(void *c,const char *path,bool create){
    (void)c;if(file_open)return SCOPE_STORE_BUSY;
    FRESULT r=f_open(&file,path,create?(FA_CREATE_NEW|FA_WRITE):FA_READ);
    file_open=r==FR_OK;return map_result(r,create);
}
static ScopeStoreResult op_read(void *c,void *buffer,size_t bytes,size_t *got){
    UINT n=0;(void)c;*got=0;if(bytes>UINT_MAX)return SCOPE_STORE_INVALID;
    FRESULT r=f_read(&file,buffer,(UINT)bytes,&n);*got=n;return map_result(r,false);
}
static ScopeStoreResult op_write(void *c,const void *buffer,size_t bytes,size_t *written){
    UINT n=0;(void)c;*written=0;if(bytes>UINT_MAX)return SCOPE_STORE_INVALID;
    FRESULT r=f_write(&file,buffer,(UINT)bytes,&n);*written=n;return map_result(r,true);
}
static ScopeStoreResult op_sync(void *c){(void)c;return map_result(f_sync(&file),true);}
static ScopeStoreResult op_close(void *c){
    (void)c;FRESULT r=file_open?f_close(&file):FR_INVALID_OBJECT;
    file_open=false;memset(&file,0,sizeof(file));return map_result(r,true);
}
static ScopeStoreResult op_rename(void *c,const char *from,const char *to){
    bool exists=false,isdir=false;uint32_t bytes=0;
    ScopeStoreResult r=op_stat(c,to,&exists,&isdir,&bytes);
    if(r!=SCOPE_STORE_OK)return r;
    if(exists)return SCOPE_STORE_IO;
    return map_result(f_rename(from,to),true);
}
static ScopeStoreResult op_begin(void *c,const char *path){
    (void)c;if(directory_open)return SCOPE_STORE_BUSY;
    FRESULT r=f_opendir(&directory,path);directory_open=r==FR_OK;return map_result(r,false);
}
static ScopeStoreResult op_next(void *c,ScopeStoreEntry *entry,bool *end){
    FILINFO info;(void)c;FRESULT r=f_readdir(&directory,&info);
    if(r!=FR_OK)return map_result(r,false);
    *end=info.fname[0]==0;memset(entry,0,sizeof(*entry));
    /* Ignore long/non-8.3 names without truncating them into valid scope names. */
    size_t n=strlen(info.fname);if(n<sizeof(entry->name))memcpy(entry->name,info.fname,n+1);
    entry->bytes=(uint32_t)info.fsize;entry->directory=(info.fattrib&AM_DIR)!=0;return SCOPE_STORE_OK;
}
static ScopeStoreResult op_end(void *c){
    (void)c;FRESULT r=directory_open?f_closedir(&directory):FR_INVALID_OBJECT;
    directory_open=false;memset(&directory,0,sizeof(directory));return map_result(r,false);
}
static const ScopeStorageOps ops={op_mkdir,op_stat,op_open,op_read,op_write,op_sync,op_close,op_rename,op_begin,op_next,op_end};
static ScopeStoreResult list_page(ScopeStoreKind kind,unsigned page,ScopeStorePage *out){
    unsigned after=0;ScopeStorePage p={0};
    if(page>SCOPE_STORAGE_MAX_ID/SCOPE_STORAGE_PAGE_SIZE)return SCOPE_STORE_LIMIT;
    for(unsigned i=0;;i++){
        ScopeStoreResult r=scope_storage_list(&storage,kind,after,&p);if(r!=SCOPE_STORE_OK)return r;
        if(i==page){*out=p;return SCOPE_STORE_OK;}
        if(!p.more||!p.count){p.count=0;p.more=false;*out=p;return SCOPE_STORE_OK;}
        after=p.files[p.count-1U].id;
    }
}
static void process_request(const ScopeTfRequest *request,Completion *done){
    memset(done,0,sizeof(*done));done->ui.sequence=request->sequence;
    done->ui.id=request->action==SCOPE_TF_LOAD?request->id:0;
    write_allowed=false;
    /* Only worker owns FatFs, including mount and device re-identification. */
    (void)f_mount(NULL,"0:/",0);media_state=STA_NOINIT;
    memset(&sd,0,sizeof(sd));memset(&fs,0,sizeof(fs));
    FRESULT mount=f_mount(&fs,"0:/",1);
    done->ui.result=map_result(mount,false);
    if(media_state==0U){for(unsigned i=0;i<4;i++)done->cid[i]=sd.CardID[i];done->sectors=sd.SectorCount;}
    done->fat=mount==FR_OK?fs.fs_type:0;
    snprintf(done->ui.media,sizeof(done->ui.media),"FAT%u %lu sectors",done->fat==1?12:done->fat==2?16:done->fat==3?32:0,(unsigned long)done->sectors);
    if(mount!=FR_OK)return;
    if(request->action==SCOPE_TF_SAVE){
        write_allowed=true;
        done->ui.result=scope_storage_save(&storage,request->kind,snapshot,request->length,&done->ui.id);
        write_allowed=false;
        if(done->ui.result==SCOPE_STORE_OK){memcpy(result_blob,snapshot,request->length);done->ui.blob=result_blob;done->ui.length=request->length;
            /* A successful commit remains success even if the optional refresh fails. */
            (void)list_page(request->kind,0,&done->ui.page);
        }
    }else if(request->action==SCOPE_TF_LOAD){
        done->ui.result=scope_storage_load(&storage,request->kind,request->id,result_blob,sizeof(result_blob));
        if(done->ui.result==SCOPE_STORE_OK){done->ui.blob=result_blob;done->ui.length=request->kind==SCOPE_STORE_WAVE?SCOPE_RECORD_BLOB_BYTES:SCOPE_SETTINGS_BLOB_BYTES;
            (void)list_page(request->kind,request->page,&done->ui.page);
        }
    }else done->ui.result=list_page(request->kind,request->page,&done->ui.page);
}
static void worker(void *ignored){
    ScopeTfRequest request;Completion done;(void)ignored;
    for(;;){
        if(xQueueReceive(requests,&request,portMAX_DELAY)!=pdTRUE)continue;
        process_request(&request,&done);
        /* Submit stays blocked until UI has consumed this result and its blob. */
        (void)xQueueSend(responses,&done,portMAX_DELAY);
    }
}
int board_storage_start(ScopeStorageUI *p){
    if(!p)return -1;
    if(panel)return panel==p?0:-1;
    requests=xQueueCreate(1,sizeof(ScopeTfRequest));responses=xQueueCreate(1,sizeof(Completion));
    if(!requests||!responses)goto fail;
    storage.ops=&ops;storage.context=NULL;panel=p;
    if(xTaskCreate(worker,"ScopeTF",8192,NULL,1,&worker_handle)==pdPASS)return 0;
    panel=NULL;
fail:
    if(requests)vQueueDelete(requests);
    if(responses)vQueueDelete(responses);
    requests=NULL;responses=NULL;return -1;
}
bool board_storage_submit(void *context,const ScopeTfRequest *request){
    if(!panel||!request||in_flight||(context&&context!=panel)||!request->sequence||
       request->action>SCOPE_TF_LOAD||request->kind>SCOPE_STORE_PARAMETERS||
       request->page>SCOPE_STORAGE_MAX_ID/SCOPE_STORAGE_PAGE_SIZE)return false;
    size_t bytes=request->kind==SCOPE_STORE_WAVE?SCOPE_RECORD_BLOB_BYTES:SCOPE_SETTINGS_BLOB_BYTES;
    if(request->action==SCOPE_TF_SAVE&&(!request->blob||request->length!=bytes))return false;
    if(request->action==SCOPE_TF_LOAD&&(!request->id||request->id>SCOPE_STORAGE_MAX_ID))return false;
    ScopeTfRequest copy=*request;
    if(request->action==SCOPE_TF_SAVE){memcpy(snapshot,request->blob,bytes);copy.blob=snapshot;}
    else{copy.blob=NULL;copy.length=0;}
    in_flight=true;active_sequence=request->sequence;
    last.ui.blob=NULL;last.ui.length=0;last.ui.sequence=request->sequence;
    last.ui.id=request->id;last.ui.result=SCOPE_STORE_BUSY;last_kind=request->kind;
    if(xQueueSend(requests,&copy,0)==pdTRUE)return true;
    in_flight=false;return false;
}
void board_storage_poll(void){
    Completion done;
    if(!panel||xQueueReceive(responses,&done,0)!=pdTRUE)return;
    if(!in_flight||done.ui.sequence!=active_sequence)return;
    last=done;
    bool accepted=scope_storage_ui_complete(panel,&done.ui);
    /* Successful media I/O does not imply that UI accepted a LOAD. Epoch,
     * identity and codec checks can still reject applying the result. */
    last.ui.result=accepted?panel->result:SCOPE_STORE_BUSY;
    if(last.ui.result!=SCOPE_STORE_OK){last.ui.blob=NULL;last.ui.length=0;}
    in_flight=false;
}
void board_storage_status(void){
    board_log("TF_STATUS busy=%u ready=%u result=%u seq=%u id=%u kind=%u\r\n",in_flight?1U:0U,panel?1U:0U,(unsigned)last.ui.result,last.ui.sequence,last.ui.id,(unsigned)last_kind);
    board_log("TF_MEDIA cid=%08x%08x%08x%08x fat=%u sector_count=%lu\r\n",(unsigned)last.cid[0],(unsigned)last.cid[1],(unsigned)last.cid[2],(unsigned)last.cid[3],last.fat,(unsigned long)last.sectors);
    board_log("TF_BLOB address=%08lx bytes=%u crc=%08x\r\n",(unsigned long)(uintptr_t)last.ui.blob,(unsigned)last.ui.length,last.ui.blob?(unsigned)scope_record_crc32(last.ui.blob,last.ui.length):0U);
}
