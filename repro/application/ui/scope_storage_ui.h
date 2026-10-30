#ifndef SCOPE_STORAGE_UI_H
#define SCOPE_STORAGE_UI_H
#include "scope_ui.h"
#include "scope_storage.h"
typedef enum {SCOPE_TF_LIST=0,SCOPE_TF_SAVE,SCOPE_TF_LOAD} ScopeTfAction;
typedef struct {
    ScopeTfAction action;ScopeStoreKind kind;unsigned sequence,page,id,epoch;
    const uint8_t *blob;size_t length;
} ScopeTfRequest;
typedef bool (*ScopeTfSubmit)(void *,const ScopeTfRequest *);
typedef struct {
    unsigned sequence,id;ScopeStoreResult result;ScopeStorePage page;
    char media[64];const uint8_t *blob;size_t length;
} ScopeTfResponse;
typedef struct {
    ScopeUI *ui;ScopeTfSubmit submit;void *context;
    lv_obj_t *overlay,*title,*note,*rows[6],*save,*load,*refresh,*previous,*next,*close;
    ScopeStorePage files;ScopeStoreKind kind;ScopeTfAction action;
    unsigned sequence,page,pending_page,pending_id,selection,epoch,last_id;bool busy;
    ScopeStoreResult result;char media[64];
    uint8_t snapshot[SCOPE_RECORD_BLOB_BYTES];
} ScopeStorageUI;
void scope_storage_ui_create(ScopeStorageUI *,ScopeUI *,ScopeTfSubmit,void *);
bool scope_storage_ui_open(ScopeStorageUI *,ScopeStoreKind);
bool scope_storage_ui_request(ScopeStorageUI *,ScopeTfAction,unsigned id,unsigned page);
bool scope_storage_ui_complete(ScopeStorageUI *,const ScopeTfResponse *);
void scope_storage_ui_localize(ScopeStorageUI *);
#endif
