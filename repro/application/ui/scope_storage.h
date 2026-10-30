#ifndef SCOPE_STORAGE_H
#define SCOPE_STORAGE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "scope_record.h"
#include "scope_settings.h"

#define SCOPE_STORAGE_PAGE_SIZE 6u
#define SCOPE_STORAGE_MAX_ID 9999999u
#define SCOPE_STORAGE_DIRECTORY "0:/SP2SCOPE"
typedef enum {SCOPE_STORE_OK=0,SCOPE_STORE_IO,SCOPE_STORE_NO_CARD,
    SCOPE_STORE_NO_FS,SCOPE_STORE_FULL,SCOPE_STORE_INVALID,SCOPE_STORE_LIMIT,
    SCOPE_STORE_NOT_FOUND,SCOPE_STORE_READ_ONLY,SCOPE_STORE_BUSY} ScopeStoreResult;
typedef enum {SCOPE_STORE_WAVE=0,SCOPE_STORE_PARAMETERS=1} ScopeStoreKind;
typedef struct {char name[13];uint32_t bytes;bool directory;} ScopeStoreEntry;
/* One owner serializes all calls. open(create=true) must exclusively create;
 * rename must refuse an existing destination. close releases even on error.
 * enumerate always releases directory state in end, including after errors. */
typedef struct {
    ScopeStoreResult (*mkdir)(void *,const char *);
    ScopeStoreResult (*stat)(void *,const char *,bool *,bool *,uint32_t *);
    ScopeStoreResult (*open)(void *,const char *,bool create);
    ScopeStoreResult (*read)(void *,void *,size_t,size_t *);
    ScopeStoreResult (*write)(void *,const void *,size_t,size_t *);
    ScopeStoreResult (*sync)(void *);
    ScopeStoreResult (*close)(void *);
    ScopeStoreResult (*rename)(void *,const char *,const char *);
    ScopeStoreResult (*begin)(void *,const char *);
    ScopeStoreResult (*next)(void *,ScopeStoreEntry *,bool *);
    ScopeStoreResult (*end)(void *);
} ScopeStorageOps;
typedef struct {
    const ScopeStorageOps *ops;void *context;
    uint8_t buffer[SCOPE_RECORD_BLOB_BYTES];
    uint32_t words[SCOPE_RECORD_WORDS];
} ScopeStorage;
typedef struct {unsigned id;uint32_t bytes;} ScopeStoreFile;
typedef struct {
    ScopeStoreFile files[SCOPE_STORAGE_PAGE_SIZE];unsigned count,total;bool more;
} ScopeStorePage;
bool scope_storage_name(ScopeStoreKind kind,unsigned id,char name[13]);
ScopeStoreResult scope_storage_save(ScopeStorage *,ScopeStoreKind,const uint8_t *,size_t,unsigned *);
/* Exact length/codec validation before copying. Failure preserves caller blob
 * and output id/page. Workspace is scratch; caller buffers must not alias it. */
ScopeStoreResult scope_storage_load(ScopeStorage *,ScopeStoreKind,unsigned,uint8_t *,size_t);
ScopeStoreResult scope_storage_list(ScopeStorage *,ScopeStoreKind,unsigned after_id,ScopeStorePage *);
#endif
