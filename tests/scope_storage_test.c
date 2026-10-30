#include "scope_storage.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <direct.h>
#include <io.h>
#include <fcntl.h>
#include <errno.h>
#include <windows.h>

typedef struct {
    char root[256];FILE *file;struct _finddata_t item;intptr_t search;
    int fail_write,short_write,fail_sync,fail_close,fail_rename,short_read;
    bool first;unsigned writes;
} Disk;
static void path(Disk *d,const char *p,char out[320]){assert(!strncmp(p,"0:/SP2SCOPE",11));snprintf(out,320,"%s%s",d->root,p+11);}
static ScopeStoreResult mkdir_op(void *v,const char *p){Disk*d=v;char b[320];path(d,p,b);return !_mkdir(b)||errno==EEXIST?SCOPE_STORE_OK:SCOPE_STORE_IO;}
static ScopeStoreResult stat_op(void*v,const char*p,bool*exists,bool*dir,uint32_t*size){Disk*d=v;char b[320];path(d,p,b);struct _stat st;if(_stat(b,&st)){if(errno!=ENOENT)return SCOPE_STORE_IO;*exists=false;*dir=false;*size=0;return SCOPE_STORE_OK;}*exists=true;*dir=(st.st_mode&_S_IFDIR)!=0;*size=(uint32_t)st.st_size;return SCOPE_STORE_OK;}
static ScopeStoreResult open_op(void*v,const char*p,bool create){Disk*d=v;assert(!d->file);char b[320];path(d,p,b);if(create){int f=_open(b,_O_WRONLY|_O_CREAT|_O_EXCL|_O_BINARY,_S_IREAD|_S_IWRITE);if(f<0)return SCOPE_STORE_IO;d->file=_fdopen(f,"wb");}else d->file=fopen(b,"rb");return d->file?SCOPE_STORE_OK:SCOPE_STORE_NOT_FOUND;}
static ScopeStoreResult read_op(void*v,void*b,size_t n,size_t*got){Disk*d=v;if(d->short_read&&n>1)n/=2;*got=fread(b,1,n,d->file);return ferror(d->file)?SCOPE_STORE_IO:SCOPE_STORE_OK;}
static ScopeStoreResult write_op(void*v,const void*b,size_t n,size_t*got){Disk*d=v;d->writes++;if(d->fail_write)return SCOPE_STORE_FULL;if(d->short_write)n/=2;*got=fwrite(b,1,n,d->file);return ferror(d->file)?SCOPE_STORE_IO:SCOPE_STORE_OK;}
static ScopeStoreResult sync_op(void*v){Disk*d=v;return d->fail_sync||fflush(d->file)?SCOPE_STORE_IO:SCOPE_STORE_OK;}
static ScopeStoreResult close_op(void*v){Disk*d=v;int bad=fclose(d->file);d->file=NULL;return bad||d->fail_close?SCOPE_STORE_IO:SCOPE_STORE_OK;}
static ScopeStoreResult rename_op(void*v,const char*a,const char*b){Disk*d=v;char aa[320],bb[320];path(d,a,aa);path(d,b,bb);return d->fail_rename||rename(aa,bb)?SCOPE_STORE_IO:SCOPE_STORE_OK;}
static ScopeStoreResult begin_op(void*v,const char*p){Disk*d=v;char b[320],glob[330];path(d,p,b);snprintf(glob,sizeof(glob),"%s/*",b);d->search=_findfirst(glob,&d->item);d->first=true;return d->search==-1?SCOPE_STORE_NOT_FOUND:SCOPE_STORE_OK;}
static ScopeStoreResult next_op(void*v,ScopeStoreEntry*e,bool*end){Disk*d=v;if(!d->first&&_findnext(d->search,&d->item)){*end=true;return SCOPE_STORE_OK;}d->first=false;*end=false;memset(e,0,sizeof(*e));snprintf(e->name,sizeof(e->name),"%.12s",d->item.name);e->bytes=(uint32_t)d->item.size;e->directory=(d->item.attrib&_A_SUBDIR)!=0;return SCOPE_STORE_OK;}
static ScopeStoreResult end_op(void*v){Disk*d=v;_findclose(d->search);d->search=-1;return SCOPE_STORE_OK;}
static const ScopeStorageOps ops={mkdir_op,stat_op,open_op,read_op,write_op,sync_op,close_op,rename_op,begin_op,next_op,end_op};
static ScopeStorage storage;static Disk disk;static uint8_t blob[16512],out[16512];static uint32_t words[4096];
static size_t settings(void){ScopeSettings s;assert(!scope_settings_defaults(50000000,&s));size_t n;assert(!scope_settings_encode(&s,blob,sizeof(blob),&n));return n;}
static size_t wave(void){ScopeRecordMeta m={0};m.count=4096;m.rate_hz=6250000;m.conversion_hz=50000000;m.filter_kind=3;m.timebase_ns=10000;m.record_id=12;m.capture_epoch=17;m.flags=SCOPE_RECORD_RISING|SCOPE_RECORD_DUAL|SCOPE_RECORD_GRID;m.trigger_channel=1;m.trigger_level_mv=500;m.scale_index[0]=2;m.scale_index[1]=3;m.cursor[0]=250;m.cursor[1]=750;for(unsigned i=0;i<4096;i++)words[i]=(i&1023u)|(((i*3u)&1023u)<<16);size_t n;assert(!scope_record_encode(&m,words,blob,sizeof(blob),&n));return n;}
static void unchanged(const uint8_t*p,size_t n){for(size_t i=0;i<n;i++)assert(p[i]==0xa5);}
static void raw(const char*name,const uint8_t*b,size_t n){char p[320];snprintf(p,sizeof(p),"%s/%s",disk.root,name);FILE*f=fopen(p,"wb");assert(f&&fwrite(b,1,n,f)==n&&!fclose(f));}
int main(void){SetErrorMode(SEM_NOGPFAULTERRORBOX);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);snprintf(disk.root,sizeof(disk.root),"artifacts/storage-core-tests/run-%lu-%lu",(unsigned long)GetCurrentProcessId(),(unsigned long)GetTickCount());_mkdir("artifacts");_mkdir("artifacts/storage-core-tests");assert(!_mkdir(disk.root));disk.search=-1;storage.ops=&ops;storage.context=&disk;
 char name[13];assert(scope_storage_name(SCOPE_STORE_WAVE,1,name)&&!strcmp(name,"W0000001.SCW"));assert(!scope_storage_name(SCOPE_STORE_WAVE,0,name));assert(!scope_storage_name(SCOPE_STORE_WAVE,10000000,name));assert(!scope_storage_name((ScopeStoreKind)2,1,name));
 size_t n=settings();unsigned id=333;assert(scope_storage_save(&storage,SCOPE_STORE_PARAMETERS,blob,n,&id)==SCOPE_STORE_OK&&id==1);memset(out,0xa5,sizeof(out));assert(scope_storage_load(&storage,SCOPE_STORE_PARAMETERS,id,out,n)==SCOPE_STORE_OK&&!memcmp(blob,out,n));
 raw("P0000002.TMP",blob,n);assert(scope_storage_save(&storage,SCOPE_STORE_PARAMETERS,blob,n,&id)==SCOPE_STORE_OK&&id==3);raw("P0000004.SCP",blob,n);assert(scope_storage_save(&storage,SCOPE_STORE_PARAMETERS,blob,n,&id)==SCOPE_STORE_OK&&id==5);
 unsigned writes=disk.writes;blob[20]^=1;id=333;assert(scope_storage_save(&storage,SCOPE_STORE_PARAMETERS,blob,n,&id)==SCOPE_STORE_INVALID&&id==333&&disk.writes==writes);blob[20]^=1;
 for(unsigned fault=0;fault<5;fault++){disk.fail_write=fault==0;disk.short_write=fault==1;disk.fail_sync=fault==2;disk.fail_close=fault==3;disk.fail_rename=fault==4;id=333;assert(scope_storage_save(&storage,SCOPE_STORE_PARAMETERS,blob,n,&id)!=SCOPE_STORE_OK&&id==333&&!disk.file);disk.fail_write=disk.short_write=disk.fail_sync=disk.fail_close=disk.fail_rename=0;}
 memset(out,0xa5,sizeof(out));assert(scope_storage_load(&storage,SCOPE_STORE_PARAMETERS,1,out,n-1)==SCOPE_STORE_INVALID);unchanged(out,sizeof(out));disk.short_read=1;assert(scope_storage_load(&storage,SCOPE_STORE_PARAMETERS,1,out,n)==SCOPE_STORE_IO);unchanged(out,sizeof(out));disk.short_read=0;
 raw("P0000020.SCP",blob,n-1);assert(scope_storage_load(&storage,SCOPE_STORE_PARAMETERS,20,out,n)==SCOPE_STORE_INVALID);unchanged(out,sizeof(out));blob[n]=0;raw("P0000021.SCP",blob,n+1);assert(scope_storage_load(&storage,SCOPE_STORE_PARAMETERS,21,out,n)==SCOPE_STORE_INVALID);unchanged(out,sizeof(out));blob[20]^=1;raw("P0000022.SCP",blob,n);assert(scope_storage_load(&storage,SCOPE_STORE_PARAMETERS,22,out,n)==SCOPE_STORE_INVALID);unchanged(out,sizeof(out));blob[20]^=1;
 n=wave();assert(scope_storage_save(&storage,SCOPE_STORE_WAVE,blob,n,&id)==SCOPE_STORE_OK&&id==1);assert(scope_storage_load(&storage,SCOPE_STORE_WAVE,1,out,n)==SCOPE_STORE_OK&&!memcmp(out,blob,n));for(unsigned i=0;i<8;i++)assert(scope_storage_save(&storage,SCOPE_STORE_WAVE,blob,n,&id)==SCOPE_STORE_OK);
 ScopeStorePage page;memset(&page,0xa5,sizeof(page));assert(scope_storage_list(&storage,SCOPE_STORE_WAVE,0,&page)==SCOPE_STORE_OK&&page.count==6&&page.total==9&&page.more);for(unsigned i=0;i<6;i++)assert(page.files[i].id==i+1&&page.files[i].bytes==16512);assert(scope_storage_list(&storage,SCOPE_STORE_WAVE,6,&page)==SCOPE_STORE_OK&&page.count==3&&page.total==9&&!page.more);for(unsigned i=0;i<3;i++)assert(page.files[i].id==i+7);
 n=settings();raw("P0000100.SCP",blob,n);assert(scope_storage_save(&storage,SCOPE_STORE_PARAMETERS,blob,n,&id)==SCOPE_STORE_OK&&id==101);raw("P0000200.TMP",blob,n);assert(scope_storage_save(&storage,SCOPE_STORE_PARAMETERS,blob,n,&id)==SCOPE_STORE_OK&&id==201);raw("P9999999.SCP",blob,n);id=333;assert(scope_storage_save(&storage,SCOPE_STORE_PARAMETERS,blob,n,&id)==SCOPE_STORE_LIMIT&&id==333);
 printf("STORAGE_CORE_PASS real-files=%s faults=write/short/sync/close/rename/length/crc ownership=atomic monotonic=verified\n",disk.root);return 0;}
