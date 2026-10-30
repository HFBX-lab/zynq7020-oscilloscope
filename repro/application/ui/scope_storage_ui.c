#include "scope_storage_ui.h"
#include "fonts/scope_fonts.h"
#include <stdio.h>
#include <string.h>

static void enable(lv_obj_t *o,bool yes){if(yes)lv_obj_remove_state(o,LV_STATE_DISABLED);else lv_obj_add_state(o,LV_STATE_DISABLED);}
static void text(lv_obj_t *o,const char *s){lv_label_set_text(lv_obj_get_child(o,0),s);}
static lv_obj_t *label(lv_obj_t*p,int x,int y,int width,const char*s,const lv_font_t*f){lv_obj_t*o=lv_label_create(p);lv_obj_set_pos(o,x,y);lv_obj_set_width(o,width);lv_obj_set_style_text_font(o,f,0);lv_obj_set_style_text_color(o,lv_color_hex(0xe7edf7),0);lv_label_set_text(o,s);return o;}
static void event(lv_event_t *e);
static lv_obj_t *button(ScopeStorageUI*p,lv_obj_t*parent,int x,int y,int w,int h){lv_obj_t*o=lv_button_create(parent);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);lv_obj_set_style_bg_color(o,lv_color_hex(0x304259),0);lv_obj_set_style_radius(o,6,0);lv_obj_set_style_text_font(o,&scope_font_16,0);lv_obj_t*l=lv_label_create(o);lv_obj_set_width(l,w-16);lv_label_set_long_mode(l,LV_LABEL_LONG_DOT);lv_obj_center(l);lv_obj_add_event_cb(o,event,LV_EVENT_CLICKED,p);return o;}
void scope_storage_ui_localize(ScopeStorageUI*p){
    if(!p||!p->ui||!p->overlay)return;
    bool zh=p->ui->language==SCOPE_ZH_CN;char b[192];
    lv_label_set_text(p->title,p->kind==SCOPE_STORE_WAVE?(zh?"TF 波形记录":"TF WAVEFORM FILES"):(zh?"TF 参数管理":"TF SETTINGS FILES"));
    const char *status=p->busy?(zh?"正在读写，请勿拔卡。":"Working. Keep the card inserted."):
        p->result==SCOPE_STORE_OK?(zh?"操作完成，文件掉电保留。":"Complete. Files survive power loss."):
        p->result==SCOPE_STORE_NO_CARD?(zh?"没有卡，请检查后刷新。":"No card. Check and refresh."):
        p->result==SCOPE_STORE_NO_FS?(zh?"文件系统无法读取。":"Cannot mount the filesystem."):
        p->result==SCOPE_STORE_FULL?(zh?"卡空间不足，保存失败。":"Card full. Save failed."):
        p->result==SCOPE_STORE_READ_ONLY?(zh?"卡只读，保存失败。":"Read-only card. Save failed."):
        p->result==SCOPE_STORE_BUSY?(zh?"采集状态已改变，请重试。":"Acquisition state changed. Retry."):
        p->result==SCOPE_STORE_INVALID?(zh?"文件或当前记录无效。":"Invalid file or current record."):
        p->result==SCOPE_STORE_LIMIT?(zh?"文件编号已用完。":"File numbers exhausted."):
        p->result==SCOPE_STORE_NOT_FOUND?(zh?"文件不存在，请刷新。":"File missing. Refresh."):
        (zh?"卡读写失败，请刷新。":"Card I/O failed. Refresh.");
    if(p->last_id){char name[13];scope_storage_name(p->kind,p->last_id,name);snprintf(b,sizeof(b),"%s\n%s  %s",status,p->media,name);}
    else snprintf(b,sizeof(b),"%s\n%s",status,p->media);
    lv_label_set_text(p->note,b);
    text(p->save,zh?"保存当前":"SAVE CURRENT");text(p->load,zh?"载入文件":"LOAD FILE");text(p->refresh,zh?"刷新":"REFRESH");
    text(p->previous,zh?"上一页":"PREVIOUS");text(p->next,zh?"下一页":"NEXT");text(p->close,zh?"关闭":"CLOSE");
    for(unsigned i=0;i<6;i++){
        if(i<p->files.count){char name[13];scope_storage_name(p->kind,p->files.files[i].id,name);snprintf(b,sizeof(b),"%s%s    %u B",p->selection==p->files.files[i].id?"> ":"",name,(unsigned)p->files.files[i].bytes);text(p->rows[i],b);lv_obj_remove_flag(p->rows[i],LV_OBJ_FLAG_HIDDEN);enable(p->rows[i],!p->busy);}
        else lv_obj_add_flag(p->rows[i],LV_OBJ_FLAG_HIDDEN);
    }
    enable(p->save,!p->busy&&!p->ui->replay_active&&!scope_ui_autoset_busy(p->ui));enable(p->load,!p->busy&&p->selection!=0);
    enable(p->refresh,!p->busy);enable(p->previous,!p->busy&&p->page>0);enable(p->next,!p->busy&&p->files.more);enable(p->close,!p->busy);
}
bool scope_storage_ui_request(ScopeStorageUI*p,ScopeTfAction action,unsigned id,unsigned page){
    if(!p||!p->submit||p->busy||action>SCOPE_TF_LOAD||scope_ui_autoset_busy(p->ui))return false;
    size_t n=0;
    if(action==SCOPE_TF_SAVE){
        n=p->kind==SCOPE_STORE_WAVE?SCOPE_RECORD_BLOB_BYTES:SCOPE_SETTINGS_BLOB_BYTES;
        bool ok=p->kind==SCOPE_STORE_WAVE?scope_ui_export_record(p->ui,p->snapshot,sizeof(p->snapshot)):scope_ui_export_settings(p->ui,p->snapshot,sizeof(p->snapshot));
        if(!ok){p->result=SCOPE_STORE_INVALID;scope_storage_ui_localize(p);return false;}
    }
    if(action==SCOPE_TF_LOAD&&(!id||id>SCOPE_STORAGE_MAX_ID))return false;
    if(++p->sequence==0)++p->sequence;
    ScopeTfRequest r={action,p->kind,p->sequence,page,id,p->ui->capture_epoch,p->snapshot,n};
    if(!p->submit(p->context,&r)){p->result=SCOPE_STORE_BUSY;scope_storage_ui_localize(p);return false;}
    p->busy=true;p->action=action;p->epoch=r.epoch;p->pending_page=page;p->pending_id=id;p->last_id=0;scope_storage_ui_localize(p);return true;
}
bool scope_storage_ui_complete(ScopeStorageUI*p,const ScopeTfResponse*r){
    if(!p||!r||!p->busy||p->sequence!=r->sequence)return false;
    p->result=r->result;p->last_id=r->id;memcpy(p->media,r->media,sizeof(p->media));p->media[sizeof(p->media)-1]=0;
    if(p->result==SCOPE_STORE_OK&&p->action==SCOPE_TF_LOAD){
        bool ok=false;
        if(r->id!=p->pending_id)p->result=SCOPE_STORE_INVALID;
        else if(p->epoch!=p->ui->capture_epoch)p->result=SCOPE_STORE_BUSY;
        else if(p->kind==SCOPE_STORE_WAVE){char name[13];if(scope_storage_name(p->kind,r->id,name))ok=scope_ui_import_record(p->ui,r->blob,r->length,name);}
        else ok=scope_ui_import_settings(p->ui,r->blob,r->length);
        if(!ok&&p->result==SCOPE_STORE_OK)p->result=SCOPE_STORE_INVALID;
    }
    if(p->result==SCOPE_STORE_OK){p->files=r->page;p->selection=0;p->page=p->pending_page;}
    p->busy=false;scope_storage_ui_localize(p);return true;
}
bool scope_storage_ui_open(ScopeStorageUI*p,ScopeStoreKind kind){
    if(!p||p->busy||(kind!=SCOPE_STORE_WAVE&&kind!=SCOPE_STORE_PARAMETERS)||scope_ui_autoset_busy(p->ui))return false;
    p->kind=kind;p->selection=0;p->last_id=0;p->page=0;memset(&p->files,0,sizeof(p->files));
    lv_obj_remove_flag(p->overlay,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(p->overlay);
    scope_storage_ui_localize(p);return scope_storage_ui_request(p,SCOPE_TF_LIST,0,0);
}
static void open_entry(void*v,unsigned kind){(void)scope_storage_ui_open(v,(ScopeStoreKind)kind);}
static void event(lv_event_t*e){
    ScopeStorageUI*p=lv_event_get_user_data(e);lv_obj_t*o=lv_event_get_target(e);if(p->busy)return;
    if(o==p->close){lv_obj_add_flag(p->overlay,LV_OBJ_FLAG_HIDDEN);return;}
    for(unsigned i=0;i<p->files.count;i++)if(o==p->rows[i]){p->selection=p->files.files[i].id;scope_storage_ui_localize(p);return;}
    if(o==p->save)(void)scope_storage_ui_request(p,SCOPE_TF_SAVE,0,0);
    else if(o==p->load)(void)scope_storage_ui_request(p,SCOPE_TF_LOAD,p->selection,p->page);
    else if(o==p->refresh)(void)scope_storage_ui_request(p,SCOPE_TF_LIST,0,0);
    else if(o==p->previous&&p->page)(void)scope_storage_ui_request(p,SCOPE_TF_LIST,0,p->page-1);
    else if(o==p->next&&p->files.more)(void)scope_storage_ui_request(p,SCOPE_TF_LIST,0,p->page+1);
}
void scope_storage_ui_create(ScopeStorageUI*p,ScopeUI*ui,ScopeTfSubmit submit,void*context){
    memset(p,0,sizeof(*p));p->ui=ui;p->submit=submit;p->context=context;p->result=SCOPE_STORE_OK;
    p->overlay=lv_obj_create(lv_screen_active());lv_obj_remove_style_all(p->overlay);lv_obj_set_size(p->overlay,1024,600);lv_obj_set_style_bg_color(p->overlay,lv_color_black(),0);lv_obj_set_style_bg_opa(p->overlay,180,0);lv_obj_add_flag(p->overlay,LV_OBJ_FLAG_CLICKABLE);lv_obj_remove_flag(p->overlay,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t*box=lv_obj_create(p->overlay);lv_obj_set_pos(box,112,30);lv_obj_set_size(box,800,540);lv_obj_set_style_pad_all(box,0,0);lv_obj_set_style_border_width(box,0,0);lv_obj_set_style_bg_color(box,lv_color_hex(0x1a2638),0);lv_obj_remove_flag(box,LV_OBJ_FLAG_SCROLLABLE);
    p->title=label(box,24,16,620,"TF",&scope_font_24);p->note=label(box,24,54,750,"",&scope_font_14);
    for(unsigned i=0;i<6;i++)p->rows[i]=button(p,box,24,112+(int)i*46,750,40);
    p->save=button(p,box,24,400,234,42);p->load=button(p,box,282,400,234,42);p->refresh=button(p,box,540,400,234,42);
    p->previous=button(p,box,24,464,200,42);p->next=button(p,box,248,464,200,42);p->close=button(p,box,594,464,180,42);
    ui->storage_open=open_entry;ui->storage_context=p;lv_obj_set_width(ui->memory_settings_button,332);lv_obj_set_width(ui->parameter_default_button,332);
    lv_obj_remove_flag(ui->tf_record_button,LV_OBJ_FLAG_HIDDEN);lv_obj_remove_flag(ui->tf_parameter_button,LV_OBJ_FLAG_HIDDEN);
    scope_storage_ui_localize(p);lv_obj_add_flag(p->overlay,LV_OBJ_FLAG_HIDDEN);
}
