#include "scope_storage.h"
#include <stdio.h>
#include <string.h>

static size_t bytes_for(ScopeStoreKind k){return k==SCOPE_STORE_WAVE?SCOPE_RECORD_BLOB_BYTES:k==SCOPE_STORE_PARAMETERS?SCOPE_SETTINGS_BLOB_BYTES:0;}
static bool valid(const ScopeStorage*s){
    if(!s||!s->ops)return false;
    const ScopeStorageOps*o=s->ops;
    return o->mkdir&&o->stat&&o->open&&o->read&&o->write&&o->sync&&o->close&&o->rename&&o->begin&&o->next&&o->end;
}
static bool overlap(const void*a,size_t an,const void*b,size_t bn){
    uintptr_t aa=(uintptr_t)a,bb=(uintptr_t)b;
    if(an>UINTPTR_MAX-aa||bn>UINTPTR_MAX-bb)return true;
    return aa<bb+bn&&bb<aa+an;
}
bool scope_storage_name(ScopeStoreKind k,unsigned id,char name[13]){
    if(!name||!bytes_for(k)||!id||id>SCOPE_STORAGE_MAX_ID)return false;
    snprintf(name,13,"%c%07u.%s",k==SCOPE_STORE_WAVE?'W':'P',id,k==SCOPE_STORE_WAVE?"SCW":"SCP");return true;
}
static void make_path(const char*name,char path[32]){snprintf(path,32,"%s/%s",SCOPE_STORAGE_DIRECTORY,name);}
static bool validate_blob(ScopeStorage*s,ScopeStoreKind k,const uint8_t*b,size_t n){
    if(!b||n!=bytes_for(k))return false;
    if(k==SCOPE_STORE_WAVE){ScopeRecordMeta m;return !scope_record_decode(b,n,&m,s->words,SCOPE_RECORD_WORDS);}
    ScopeSettings settings;return !scope_settings_decode(b,n,&settings);
}
static ScopeStoreResult stat_path(ScopeStorage*s,const char*p,bool*exists){
    bool directory=false;uint32_t bytes=0;return s->ops->stat(s->context,p,exists,&directory,&bytes);
}
static ScopeStoreResult read_checked(ScopeStorage*s,ScopeStoreKind k,const char*p){
    bool exists=false,directory=false;uint32_t bytes=0;
    ScopeStoreResult r=s->ops->stat(s->context,p,&exists,&directory,&bytes);
    if(r!=SCOPE_STORE_OK)return r;
    if(!exists)return SCOPE_STORE_NOT_FOUND;
    if(directory||bytes!=bytes_for(k))return SCOPE_STORE_INVALID;
    r=s->ops->open(s->context,p,false);if(r!=SCOPE_STORE_OK)return r;
    size_t got=0;const size_t expected=bytes_for(k);
    r=s->ops->read(s->context,s->buffer,expected,&got);
    if(r==SCOPE_STORE_OK&&got!=expected)r=SCOPE_STORE_IO;
    if(r==SCOPE_STORE_OK){uint8_t extra;got=0;r=s->ops->read(s->context,&extra,1,&got);if(r==SCOPE_STORE_OK&&got)r=SCOPE_STORE_INVALID;}
    ScopeStoreResult close=s->ops->close(s->context);if(r==SCOPE_STORE_OK)r=close;
    if(r==SCOPE_STORE_OK&&!validate_blob(s,k,s->buffer,expected))r=SCOPE_STORE_INVALID;
    return r;
}
ScopeStoreResult scope_storage_load(ScopeStorage*s,ScopeStoreKind k,unsigned id,uint8_t*b,size_t capacity){
    char name[13],path[32];size_t n=bytes_for(k);
    if(!valid(s)||!b||!n||capacity<n||overlap(s,sizeof(*s),b,capacity)||!scope_storage_name(k,id,name))return SCOPE_STORE_INVALID;
    make_path(name,path);ScopeStoreResult r=read_checked(s,k,path);
    if(r==SCOPE_STORE_OK)memcpy(b,s->buffer,n);
    return r;
}
static ScopeStoreResult ensure_directory(ScopeStorage*s){
    bool exists=false,directory=false;uint32_t bytes=0;
    ScopeStoreResult r=s->ops->stat(s->context,SCOPE_STORAGE_DIRECTORY,&exists,&directory,&bytes);
    if(r!=SCOPE_STORE_OK)return r;
    if(exists)return directory?SCOPE_STORE_OK:SCOPE_STORE_INVALID;
    r=s->ops->mkdir(s->context,SCOPE_STORAGE_DIRECTORY);if(r!=SCOPE_STORE_OK)return r;
    r=s->ops->stat(s->context,SCOPE_STORAGE_DIRECTORY,&exists,&directory,&bytes);
    if(r==SCOPE_STORE_OK&&(!exists||!directory))r=SCOPE_STORE_INVALID;
    return r;
}
static unsigned parse_id(ScopeStoreKind k,const char name[13]);
static ScopeStoreResult next_id(ScopeStorage*s,ScopeStoreKind k,unsigned*out){
    ScopeStoreResult r=s->ops->begin(s->context,SCOPE_STORAGE_DIRECTORY);if(r!=SCOPE_STORE_OK)return r;
    unsigned maximum=0;
    for(;;){ScopeStoreEntry e={0};bool end=false;r=s->ops->next(s->context,&e,&end);if(r!=SCOPE_STORE_OK||end)break;
        if(e.directory||!memchr(e.name,0,13))continue;
        if(strlen(e.name)==12&&!strcmp(e.name+9,"TMP"))memcpy(e.name+9,k==SCOPE_STORE_WAVE?"SCW":"SCP",4);
        unsigned id=parse_id(k,e.name);if(id>maximum)maximum=id;
    }
    ScopeStoreResult close=s->ops->end(s->context);if(r==SCOPE_STORE_OK)r=close;
    if(r==SCOPE_STORE_OK){if(maximum==SCOPE_STORAGE_MAX_ID)return SCOPE_STORE_LIMIT;*out=maximum+1;}return r;
}
ScopeStoreResult scope_storage_save(ScopeStorage*s,ScopeStoreKind k,const uint8_t*b,size_t n,unsigned*out_id){
    if(!valid(s)||!b||!out_id||!bytes_for(k)||overlap(s,sizeof(*s),b,n)||overlap(b,n,out_id,sizeof(*out_id))||
       overlap(s,sizeof(*s),out_id,sizeof(*out_id))||!validate_blob(s,k,b,n))return SCOPE_STORE_INVALID;
    ScopeStoreResult r=ensure_directory(s);if(r!=SCOPE_STORE_OK)return r;
    unsigned id;char name[13],final[32],temporary[32];
    r=next_id(s,k,&id);if(r!=SCOPE_STORE_OK)return r;
    for(;id<=SCOPE_STORAGE_MAX_ID;id++){
        bool exists=false;scope_storage_name(k,id,name);make_path(name,final);
        r=stat_path(s,final,&exists);if(r!=SCOPE_STORE_OK)return r;if(exists)continue;
        memcpy(name+9,"TMP",4);make_path(name,temporary);
        r=stat_path(s,temporary,&exists);if(r!=SCOPE_STORE_OK)return r;if(!exists)break;
    }
    if(id>SCOPE_STORAGE_MAX_ID)return SCOPE_STORE_LIMIT;
    r=s->ops->open(s->context,temporary,true);if(r!=SCOPE_STORE_OK)return r;
    size_t written=0;r=s->ops->write(s->context,b,n,&written);
    if(r==SCOPE_STORE_OK&&written!=n)r=SCOPE_STORE_FULL;
    if(r==SCOPE_STORE_OK)r=s->ops->sync(s->context);
    ScopeStoreResult close=s->ops->close(s->context);if(r==SCOPE_STORE_OK)r=close;
    if(r!=SCOPE_STORE_OK)return r;
    r=read_checked(s,k,temporary);if(r!=SCOPE_STORE_OK)return r;
    if(memcmp(b,s->buffer,n))return SCOPE_STORE_INVALID;
    bool exists=false;r=stat_path(s,final,&exists);if(r!=SCOPE_STORE_OK)return r;
    if(exists)return SCOPE_STORE_IO;
    r=s->ops->rename(s->context,temporary,final);
    if(r==SCOPE_STORE_OK)*out_id=id;
    return r;
}
static unsigned parse_id(ScopeStoreKind k,const char name[13]){
    if(!memchr(name,0,13)||strlen(name)!=12||name[0]!=(k==SCOPE_STORE_WAVE?'W':'P')||
       name[8]!='.'||strcmp(name+9,k==SCOPE_STORE_WAVE?"SCW":"SCP"))return 0;
    unsigned id=0;for(unsigned i=1;i<8;i++){if(name[i]<'0'||name[i]>'9')return 0;id=id*10+(unsigned)(name[i]-'0');}return id;
}
ScopeStoreResult scope_storage_list(ScopeStorage*s,ScopeStoreKind k,unsigned after,ScopeStorePage*out){
    if(!valid(s)||!bytes_for(k)||!out||after>SCOPE_STORAGE_MAX_ID||overlap(s,sizeof(*s),out,sizeof(*out)))return SCOPE_STORE_INVALID;
    ScopeStorePage p={0};bool exists=false,directory=false;uint32_t bytes=0;
    ScopeStoreResult r=s->ops->stat(s->context,SCOPE_STORAGE_DIRECTORY,&exists,&directory,&bytes);
    if(r!=SCOPE_STORE_OK)return r;
    if(!exists){*out=p;return SCOPE_STORE_OK;}if(!directory)return SCOPE_STORE_INVALID;
    r=s->ops->begin(s->context,SCOPE_STORAGE_DIRECTORY);if(r!=SCOPE_STORE_OK)return r;
    unsigned eligible=0;
    for(;;){ScopeStoreEntry e={0};bool end=false;r=s->ops->next(s->context,&e,&end);if(r!=SCOPE_STORE_OK||end)break;
        unsigned id=e.directory?0:parse_id(k,e.name);if(!id)continue;
        if(p.total==UINT32_MAX){r=SCOPE_STORE_LIMIT;break;}p.total++;
        if(id<=after)continue;
        eligible++;unsigned at=0;while(at<p.count&&p.files[at].id<id)at++;
        if(at>=SCOPE_STORAGE_PAGE_SIZE)continue;
        unsigned last=p.count<SCOPE_STORAGE_PAGE_SIZE?p.count:SCOPE_STORAGE_PAGE_SIZE-1;
        for(unsigned j=last;j>at;j--)p.files[j]=p.files[j-1];
        p.files[at]=(ScopeStoreFile){id,e.bytes};if(p.count<SCOPE_STORAGE_PAGE_SIZE)p.count++;
    }
    ScopeStoreResult close=s->ops->end(s->context);if(r==SCOPE_STORE_OK)r=close;
    if(r==SCOPE_STORE_OK){p.more=eligible>p.count;*out=p;}return r;
}
