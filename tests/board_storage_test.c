/* The SDK and queue mocks are generated only in the D-drive test workdir. */
#define SCOPE_UI_H
typedef void lv_obj_t;
typedef struct {unsigned capture_epoch;} ScopeUI;
#include "../firmware/storage/board_storage.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
typedef struct {size_t bytes;bool full;uint8_t data[512];} FakeQueue;
static int task_fail,queue_fail,init_count,read_count,write_count,mount_count,complete_count,list_count,rename_count,close_count,dirclose_count,log_count;
static unsigned stack_words,task_priority,last_after;
static BYTE open_flags;
static bool card_present=true,stat_exists,stat_directory;
static FRESULT fat_result=FR_OK,mount_result=FR_OK,stat_result=FR_NO_FILE;
static ScopeStoreResult save_result=SCOPE_STORE_OK,load_result=SCOPE_STORE_OK,list_result=SCOPE_STORE_OK;
static uint8_t saved_byte;
static ScopeTfResponse completed;
static bool completion_accepted=true,override_ui_result;
static ScopeStoreResult ui_result;
static char status_line[256],blob_line[256];
static TaskHandle_t current_handle=(void*)1;
QueueHandle_t xQueueCreate(unsigned n,size_t bytes){assert(n==1);if(queue_fail)return NULL;FakeQueue*q=calloc(1,sizeof(*q));q->bytes=bytes;assert(bytes<=512);return q;}
int xQueueSend(QueueHandle_t h,const void*v,unsigned ticks){FakeQueue*q=h;(void)ticks;if(q->full)return 0;memcpy(q->data,v,q->bytes);q->full=true;return 1;}
int xQueueReceive(QueueHandle_t h,void*v,unsigned ticks){FakeQueue*q=h;(void)ticks;if(!q->full)return 0;memcpy(v,q->data,q->bytes);q->full=false;return 1;}
void vQueueDelete(QueueHandle_t q){free(q);}
int xTaskCreate(void(*fn)(void*),const char*name,unsigned words,void*arg,unsigned prio,TaskHandle_t*out){(void)fn;(void)name;(void)arg;stack_words=words;task_priority=prio;*out=(void*)1;return task_fail?0:1;}
TaskHandle_t xTaskGetCurrentTaskHandle(void){return current_handle;}
XSdPs_Config *XSdPs_LookupConfig(unsigned id){static XSdPs_Config cfg={0xE0100000};assert(id==0);return &cfg;}
int XSdPs_CfgInitialize(XSdPs*s,XSdPs_Config*c,u32 addr){assert(addr==c->BaseAddress);s->Config=*c;return 0;}
u32 XSdPs_GetPresentStatusReg(u32 addr){(void)addr;return card_present?XSDPS_PSR_CARD_INSRT_MASK:0;}
int XSdPs_CardInitialize(XSdPs*s){init_count++;s->HCS=1;s->SectorCount=61067264;s->CardID[0]=0x12345678;s->CardID[1]=2;s->CardID[2]=3;s->CardID[3]=4;return 0;}
int XSdPs_ReadPolled(XSdPs*s,u32 sector,UINT n,BYTE*b){(void)s;(void)sector;(void)n;(void)b;read_count++;return 0;}
int XSdPs_WritePolled(XSdPs*s,u32 sector,UINT n,const BYTE*b){(void)s;(void)sector;(void)n;(void)b;write_count++;return 0;}
FRESULT f_mount(FATFS*f,const char*p,BYTE immediate){assert(!strcmp(p,"0:/"));if(!f)return FR_OK;assert(immediate==1);mount_count++;if(mount_result!=FR_OK)return mount_result;if(disk_initialize(0))return FR_NOT_READY;f->fs_type=3;return FR_OK;}
FRESULT f_mkdir(const char*p){(void)p;return fat_result;}
FRESULT f_stat(const char*p,FILINFO*i){(void)p;if(stat_result!=FR_OK)return stat_result;if(!stat_exists)return FR_NO_FILE;memset(i,0,sizeof(*i));i->fattrib=stat_directory?AM_DIR:0;i->fsize=96;return FR_OK;}
FRESULT f_open(FIL*f,const char*p,BYTE flags){(void)f;(void)p;open_flags=flags;return fat_result;}
FRESULT f_read(FIL*f,void*b,UINT n,UINT*out){(void)f;(void)b;*out=n/2;return fat_result;}
FRESULT f_write(FIL*f,const void*b,UINT n,UINT*out){(void)f;(void)b;*out=n/2;return fat_result;}
FRESULT f_sync(FIL*f){(void)f;return fat_result;}
FRESULT f_close(FIL*f){(void)f;close_count++;return fat_result;}
FRESULT f_rename(const char*a,const char*b){(void)a;(void)b;rename_count++;return fat_result;}
FRESULT f_opendir(DIR*d,const char*p){(void)d;(void)p;return fat_result;}
FRESULT f_readdir(DIR*d,FILINFO*i){(void)d;strcpy(i->fname,"W0000001.SCW.long");i->fsize=16512;i->fattrib=0;return fat_result;}
FRESULT f_closedir(DIR*d){(void)d;dirclose_count++;return fat_result;}
ScopeStoreResult scope_storage_save(ScopeStorage*s,ScopeStoreKind k,const uint8_t*b,size_t n,unsigned*out){assert(s->ops==&ops);assert(k==SCOPE_STORE_PARAMETERS);assert(n==96);assert(write_allowed);saved_byte=b[0];if(save_result==SCOPE_STORE_OK)*out=17;return save_result;}
ScopeStoreResult scope_storage_load(ScopeStorage*s,ScopeStoreKind k,unsigned id,uint8_t*b,size_t n){(void)s;(void)k;assert(id==7);assert(n==sizeof(result_blob));assert(!write_allowed);memset(b,0x42,96);return load_result;}
ScopeStoreResult scope_storage_list(ScopeStorage*s,ScopeStoreKind k,unsigned after,ScopeStorePage*p){(void)s;(void)k;assert(!write_allowed);list_count++;last_after=after;if(list_result!=SCOPE_STORE_OK)return list_result;memset(p,0,sizeof(*p));p->total=13;for(unsigned i=0;i<6&&after+i+1<=13;i++){p->files[i].id=after+i+1;p->count++;}p->more=after+p->count<13;return SCOPE_STORE_OK;}
bool scope_storage_ui_complete(ScopeStorageUI*p,const ScopeTfResponse*r){assert(in_flight);complete_count++;completed=*r;if(!completion_accepted)return false;p->result=r->result;if(override_ui_result)p->result=ui_result;return true;}
uint32_t scope_record_crc32(const void*b,size_t n){(void)b;return (uint32_t)n;}
void board_log(const char*format,...){assert(strchr(format,'\n'));log_count++;va_list args;va_start(args,format);if(!strncmp(format,"TF_STATUS",9))vsnprintf(status_line,sizeof(status_line),format,args);else if(!strncmp(format,"TF_BLOB",7))vsnprintf(blob_line,sizeof(blob_line),format,args);va_end(args);}
static void step_worker(void){ScopeTfRequest r;Completion c;assert(xQueueReceive(requests,&r,0));process_request(&r,&c);assert(xQueueSend(responses,&c,0));}
int main(void){
    assert(get_fattime()==((20U<<25)|(1U<<21)|(1U<<16)));
    ScopeStorageUI p={0};assert(board_storage_start(NULL)==-1);task_fail=1;assert(board_storage_start(&p)==-1);assert(!panel&&!requests&&!responses);task_fail=0;
    queue_fail=1;assert(board_storage_start(&p)==-1);queue_fail=0;
    assert(board_storage_start(&p)==0);assert(stack_words==8192&&task_priority==1);assert(board_storage_start(&p)==0);
    uint8_t blob[96];memset(blob,0x31,sizeof(blob));
    ScopeTfRequest req={SCOPE_TF_SAVE,SCOPE_STORE_PARAMETERS,1,0,0,42,blob,sizeof(blob)};
    assert(board_storage_submit(NULL,&req));blob[0]=0x99;assert(!board_storage_submit(NULL,&req));assert(init_count==0&&mount_count==0);
    step_worker();assert(saved_byte==0x31&&init_count==1&&!write_allowed);assert(!board_storage_submit(NULL,&req));
    board_storage_poll();assert(!in_flight&&complete_count==1&&completed.id==17&&completed.blob[0]==0x31&&completed.length==96);
    assert(last.cid[0]==0x12345678&&last.fat==3&&last.sectors==61067264);
    req.action=SCOPE_TF_LIST;req.sequence=2;req.page=2;assert(board_storage_submit(NULL,&req));
    Completion late={0};late.ui.sequence=1;assert(xQueueSend(responses,&late,0));board_storage_poll();assert(in_flight&&complete_count==1);
    step_worker();board_storage_poll();assert(init_count==2&&completed.page.count==1&&completed.page.files[0].id==13&&last_after==12);
    req.page=3;assert(board_storage_submit(NULL,&req));step_worker();board_storage_poll();assert(completed.page.count==0&&!completed.page.more);
    req.action=SCOPE_TF_LOAD;req.page=0;req.id=7;assert(board_storage_submit(NULL,&req));step_worker();board_storage_poll();assert(completed.blob[0]==0x42&&completed.id==7&&completed.length==96);
    assert(last.ui.result==SCOPE_STORE_OK&&last.ui.blob&&last.ui.length==96);
    /* UI receives valid file bytes, then rejects them because the acquisition
     * epoch changed. Diagnostics must report the UI's final decision. */
    override_ui_result=true;ui_result=SCOPE_STORE_BUSY;
    assert(board_storage_submit(NULL,&req));step_worker();board_storage_poll();
    assert(completed.result==SCOPE_STORE_OK&&p.result==SCOPE_STORE_BUSY);
    assert(last.ui.result==SCOPE_STORE_BUSY&&!last.ui.blob&&!last.ui.length&&!in_flight);
    board_storage_status();assert(strstr(status_line,"result=9 ")&&strstr(blob_line,"address=00000000 bytes=0 crc=00000000"));
    /* A mismatched ID/import rejection is also a failed application of LOAD. */
    ui_result=SCOPE_STORE_INVALID;assert(board_storage_submit(NULL,&req));step_worker();board_storage_poll();
    assert(p.result==SCOPE_STORE_INVALID&&last.ui.result==SCOPE_STORE_INVALID&&!last.ui.blob&&!last.ui.length);
    override_ui_result=false;completion_accepted=false;p.result=SCOPE_STORE_OK;
    assert(board_storage_submit(NULL,&req));step_worker();board_storage_poll();
    assert(last.ui.result==SCOPE_STORE_BUSY&&!last.ui.blob&&!last.ui.length&&!in_flight);
    completion_accepted=true;
    req.action=SCOPE_TF_SAVE;req.id=0;save_result=SCOPE_STORE_FULL;list_result=SCOPE_STORE_IO;assert(board_storage_submit(NULL,&req));step_worker();board_storage_poll();assert(completed.result==SCOPE_STORE_FULL&&completed.length==0);
    save_result=SCOPE_STORE_OK;assert(board_storage_submit(NULL,&req));step_worker();board_storage_poll();assert(completed.result==SCOPE_STORE_OK&&completed.id==17);assert(last.ui.result==SCOPE_STORE_OK&&last.ui.blob&&last.ui.length==96);list_result=SCOPE_STORE_OK;
    req.action=SCOPE_TF_LIST;card_present=false;assert(board_storage_submit(NULL,&req));step_worker();board_storage_poll();assert(completed.result==SCOPE_STORE_NO_CARD);card_present=true;
    mount_result=FR_NO_FILESYSTEM;assert(board_storage_submit(NULL,&req));step_worker();board_storage_poll();assert(completed.result==SCOPE_STORE_NO_FS);mount_result=FR_OK;
    req.kind=(ScopeStoreKind)-1;assert(!board_storage_submit(NULL,&req));req.kind=SCOPE_STORE_PARAMETERS;req.action=(ScopeTfAction)-1;assert(!board_storage_submit(NULL,&req));req.action=SCOPE_TF_LIST;
    assert(map_result(FR_DENIED,true)==SCOPE_STORE_FULL&&map_result(FR_DENIED,false)==SCOPE_STORE_IO&&map_result(FR_WRITE_PROTECTED,true)==SCOPE_STORE_READ_ONLY);
    bool exists=true,isdir=true;uint32_t bytes=1;assert(op_stat(NULL,"x",&exists,&isdir,&bytes)==SCOPE_STORE_OK&&!exists&&!isdir&&!bytes);
    fat_result=FR_EXIST;stat_result=FR_OK;stat_exists=true;stat_directory=false;assert(op_mkdir(NULL,"x")==SCOPE_STORE_INVALID);stat_directory=true;assert(op_mkdir(NULL,"x")==SCOPE_STORE_OK);
    fat_result=FR_OK;assert(op_rename(NULL,"a","b")==SCOPE_STORE_IO&&rename_count==0);stat_exists=false;assert(op_rename(NULL,"a","b")==SCOPE_STORE_OK&&rename_count==1);
    assert(op_open(NULL,"x",true)==SCOPE_STORE_OK&&open_flags==(FA_CREATE_NEW|FA_WRITE));assert(op_open(NULL,"x",false)==SCOPE_STORE_BUSY);
    fat_result=FR_DISK_ERR;assert(op_close(NULL)==SCOPE_STORE_IO&&!file_open&&close_count==1);fat_result=FR_OK;assert(op_open(NULL,"x",false)==SCOPE_STORE_OK&&open_flags==FA_READ);assert(op_close(NULL)==SCOPE_STORE_OK);
    size_t transferred=0;uint8_t small[16]={0};assert(op_read(NULL,small,sizeof(small),&transferred)==SCOPE_STORE_OK&&transferred==8);assert(op_write(NULL,small,sizeof(small),&transferred)==SCOPE_STORE_OK&&transferred==8);
    assert(op_begin(NULL,"x")==SCOPE_STORE_OK);ScopeStoreEntry entry;bool end=false;assert(op_next(NULL,&entry,&end)==SCOPE_STORE_OK&&!entry.name[0]);fat_result=FR_DISK_ERR;assert(op_end(NULL)==SCOPE_STORE_IO&&!directory_open&&dirclose_count==1);fat_result=FR_OK;
    media_state=0;sd.SectorCount=100;sd.HCS=1;uint8_t block[512];assert(disk_write(0,block,0,1)==RES_WRPRT);write_allowed=true;current_handle=(void*)2;assert(disk_write(0,block,0,1)==RES_WRPRT);current_handle=(void*)1;assert(disk_write(0,block,99,2)==RES_PARERR);assert(disk_write(0,block,100,1)==RES_PARERR);assert(disk_write(0,block,99,1)==RES_OK&&write_count==1);write_allowed=false;
    assert(disk_read(0,block,100,1)==RES_PARERR);assert(disk_read(0,block,0,0)==RES_PARERR);assert(disk_read(0,block,0,1)==RES_OK&&read_count==1);assert(disk_read(1,block,0,1)==RES_NOTRDY);
    sd.HCS=0;sd.SectorCount=UINT32_MAX;assert(disk_read(0,block,UINT32_MAX/512U,2)==RES_PARERR);assert(disk_ioctl(0,99,block)==RES_PARERR);assert(disk_ioctl(0,CTRL_SYNC,NULL)==RES_OK);
    log_count=0;board_storage_status();assert(log_count==3);puts("board_storage_test PASS: queues/snapshot/owner/remount/pages/errors/FatFs/sector-bounds/status/UI-completion-rejection");return 0;
}
