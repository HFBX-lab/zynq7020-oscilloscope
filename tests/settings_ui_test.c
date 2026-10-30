#include "scope_ui.h"
#include "src/misc/lv_text_private.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t buffer[1024*60],frame[1024*600],record[4096];
static lv_indev_data_t finger;
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *p){
    unsigned width=(unsigned)(a->x2-a->x1+1);
    for(int y=a->y1;y<=a->y2;y++)memcpy(frame+y*1024+a->x1,p+(y-a->y1)*width*4,width*4);
    lv_display_flush_ready(d);
}
static void pointer(lv_indev_t *d,lv_indev_data_t *out){(void)d;*out=finger;}
static void point(int x,int y,bool down){
    finger.point=(lv_point_t){x,y};finger.state=down?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;
    for(unsigned i=0;i<6;i++){lv_tick_inc(10);lv_timer_handler();}
}
static void click(lv_obj_t *object){
    assert(object);lv_obj_update_layout(lv_screen_active());lv_area_t a;lv_obj_get_coords(object,&a);
    point((a.x1+a.x2)/2,(a.y1+a.y2)/2,true);point((a.x1+a.x2)/2,(a.y1+a.y2)/2,false);
}
static void labels(lv_obj_t *object,bool hidden){
    hidden=hidden||lv_obj_has_flag(object,LV_OBJ_FLAG_HIDDEN);
    if(lv_obj_check_type(object,&lv_label_class)){
        const char *text=lv_label_get_text(object);uint32_t at=0;
        while(text[at]){uint32_t c=lv_text_encoded_next(text,&at);if(c=='\n')continue;
            lv_font_glyph_dsc_t g={0};
            if(!lv_font_get_glyph_dsc(lv_obj_get_style_text_font(object,LV_PART_MAIN),&g,c,0)||g.is_placeholder){
                fprintf(stderr,"MISSING GLYPH U+%04x: %s\n",(unsigned)c,text);assert(false);
            }
        }
        if(!hidden){lv_obj_t *parent=lv_obj_get_parent(object);
            if(lv_obj_get_x(object)<0||lv_obj_get_y(object)<0)fprintf(stderr,"NEG LABEL: %s x%d y%d w%d parent%d\n",text,(int)lv_obj_get_x(object),(int)lv_obj_get_y(object),(int)lv_obj_get_width(object),(int)lv_obj_get_width(parent));
            assert(lv_obj_get_x(object)>=0&&lv_obj_get_y(object)>=0);
            assert(lv_obj_get_x(object)+lv_obj_get_width(object)<=lv_obj_get_width(parent));
            assert(lv_obj_get_y(object)+lv_obj_get_height(object)<=lv_obj_get_height(parent));
        }
    }
    for(unsigned i=0;i<lv_obj_get_child_count(object);i++)labels(lv_obj_get_child(object,i),hidden);
}
static void save_frame(lv_display_t *d,const char *directory,const char *name){
    lv_obj_update_layout(lv_screen_active());labels(lv_screen_active(),false);
    if(!directory)return;
    lv_refr_now(d);char path[512];snprintf(path,sizeof(path),"%s/%s.bgrx",directory,name);
    FILE *file=fopen(path,"wb");assert(file);assert(fwrite(frame,1,sizeof(frame),file)==sizeof(frame));assert(!fclose(file));
}
static bool feed(ScopeUI *ui,unsigned id){
    ScopeCapturePlan plan;ScopeHardwareConfig cfg;
    assert(!scope_ui_capture_plan(ui,&plan)&&!scope_ui_hardware_config(ui,&cfg));
    for(unsigned i=0;i<4096;i++)record[i]=512u|((unsigned)lround(563+50*sin(6.283185307179586*i*50000/plan.rate_hz))<<16);
    record[cfg.pre_samples-1]=512u|(562u<<16);record[cfg.pre_samples]=512u|(564u<<16);
    ScopeRecordInfo info={ui->capture_epoch,plan.rate_index,plan.conversion_hz,plan.filter_kind};
    ScopeHardwareTrigger edge={true,false,cfg.pre_samples};
    ScopeAdcMetrics metrics[2]={{0,0,0,false},{1,.6,50000,true}};
    return scope_ui_set_hardware_adc_record(ui,record,4096,plan.rate_hz,id,metrics,&info,&edge);
}
typedef struct {
    const uint32_t *pointer;
    unsigned count,rate,id,seen,conversion;
    ScopeRecordInfo info;
    ScopeAdcMetrics metrics[2];
    ScopeMeasurement measurement[2];
    uint32_t raw_crc,wave_crc,replay_crc;
    ScopeRecordMeta wave_meta;
    ScopeLiveViewBackup backup;
    bool wave_valid,adaptive,hardware,running;
} DataSnapshot;
static DataSnapshot snapshot(const ScopeUI *ui){
    DataSnapshot s={0};s.pointer=ui->adc_record;s.count=ui->adc_count;s.rate=ui->adc_rate;s.id=ui->adc_id;
    s.seen=ui->adc_last_seen_id;s.conversion=ui->conversion_hz;s.info=ui->adc_info;
    memcpy(s.metrics,ui->adc_metrics,sizeof(s.metrics));memcpy(s.measurement,ui->measurement,sizeof(s.measurement));
    s.raw_crc=scope_record_crc32(ui->adc_record,ui->adc_count*sizeof(uint32_t));
    s.wave_crc=scope_record_crc32(ui->memory_blob,sizeof(ui->memory_blob));s.replay_crc=scope_record_crc32(ui->replay_words,sizeof(ui->replay_words));
    s.wave_meta=ui->memory_meta;s.backup=ui->live_view_backup;s.wave_valid=ui->memory_valid;
    s.adaptive=ui->adaptive_sampling;s.hardware=ui->hardware_trigger;s.running=ui->model.running;return s;
}
static void unchanged_data(const ScopeUI *ui,const DataSnapshot *s){
    DataSnapshot now=snapshot(ui);assert(!memcmp(&now,s,sizeof(now)));
}
static void settings_equal(const ScopeUI *ui,const ScopeSettings *s,int index){
    assert(ui->timebase_index==index&&ui->adc_channel==s->selected_channel&&ui->trigger_channel==s->trigger_channel);
    assert(ui->trigger_level_mv==s->trigger_level_mv&&ui->language==(ScopeLanguage)s->language);
    assert(ui->trigger_normal==!!(s->flags&SCOPE_SETTINGS_NORMAL)&&ui->trigger_rising==!!(s->flags&SCOPE_SETTINGS_RISING));
    assert(ui->dual_display==!!(s->flags&SCOPE_SETTINGS_DUAL)&&ui->grid_on==!!(s->flags&SCOPE_SETTINGS_GRID));
    assert(ui->cursors_on==!!(s->flags&SCOPE_SETTINGS_CURSORS));
    for(unsigned c=0;c<2;c++)assert(ui->channel_scale_index[c]==(int)s->scale_index[c]&&
        ui->channel_position_mdiv[c]==s->position_mdiv[c]&&ui->cursor_positions[c]==s->cursor[c]);
    assert(ui->scale_index==ui->channel_scale_index[ui->adc_channel]);
}
static void all_rejected(ScopeUI *ui){
    unsigned epoch=ui->capture_epoch;DataSnapshot held=snapshot(ui);ScopeSettings cached=ui->settings_cache;
    uint32_t crc=scope_record_crc32(ui->settings_blob,sizeof(ui->settings_blob));bool valid=ui->settings_valid;
    int time=ui->timebase_index;unsigned source=ui->adc_channel;ScopeLanguage language=ui->language;
    assert(!scope_ui_settings_save(ui)&&!scope_ui_settings_load(ui)&&!scope_ui_settings_defaults(ui));
    assert(ui->capture_epoch==epoch&&ui->timebase_index==time&&ui->adc_channel==source&&ui->language==language);
    assert(ui->settings_valid==valid&&!memcmp(&cached,&ui->settings_cache,sizeof(cached)));
    assert(crc==scope_record_crc32(ui->settings_blob,sizeof(ui->settings_blob)));unchanged_data(ui,&held);
}
int main(int argc,char **argv){
    const char *directory=argc>1?argv[1]:NULL;lv_init();lv_display_t *display=lv_display_create(1024,600);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_XRGB8888);lv_display_set_buffers(display,buffer,NULL,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display,flush);lv_indev_t *input=lv_indev_create();lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(input,pointer);
    ScopeUI ui;scope_ui_create(&ui);lv_obj_update_layout(lv_screen_active());
    assert(!scope_ui_settings_save(NULL)&&!scope_ui_settings_load(NULL)&&!scope_ui_settings_defaults(NULL)&&!scope_ui_show_parameters(NULL,true));
    all_rejected(&ui);assert(!scope_ui_show_parameters(&ui,true));
    scope_ui_enable_adaptive_sampling(&ui,true);scope_ui_enable_hardware_trigger(&ui,true);assert(scope_ui_set_conversion_hz(&ui,50000000));
    ui.timebase_index=6;assert(scope_ui_select_source(&ui,1)&&scope_ui_select_trigger_source(&ui,1));assert(feed(&ui,1));
    /* A missing slot rejects load but never prevents an explicit default. */
    assert(!scope_ui_settings_load(&ui));
    DataSnapshot held=snapshot(&ui);unsigned epoch=ui.capture_epoch;
    assert(scope_ui_settings_defaults(&ui)&&!ui.settings_valid&&ui.capture_epoch==epoch+1);unchanged_data(&ui,&held);
    ScopeSettings defaults;assert(!scope_settings_defaults(50000000,&defaults));settings_equal(&ui,&defaults,10);
    assert(scope_ui_select_source(&ui,1)&&scope_ui_select_trigger_source(&ui,1));ui.timebase_index=6;
    assert(scope_ui_configure_channel(&ui,0,3,750)&&scope_ui_configure_channel(&ui,1,1,-500));
    scope_ui_set_dual_display(&ui,true);scope_ui_set_cursors(&ui,true);assert(scope_ui_move_cursors(&ui,100,900));
    assert(feed(&ui,2)&&scope_ui_memory_save(&ui));assert(scope_ui_configure_trigger(&ui,true,false,-250));ui.grid_on=false;
    epoch=ui.capture_epoch;held=snapshot(&ui);bool matched=ui.adc_triggered;double start=ui.adc_display_start,crossing=ui.adc_trigger_crossing;
    click(ui.settings_button);click(ui.parameter_button);click(ui.parameter_save_button);
    assert(ui.settings_valid&&ui.capture_epoch==epoch);unchanged_data(&ui,&held);
    click(ui.parameter_close_button);click(ui.close_button);
    assert(ui.adc_triggered==matched&&ui.adc_display_start==start&&ui.adc_trigger_crossing==crossing);
    ScopeSettings saved;assert(!scope_settings_decode(ui.settings_blob,sizeof(ui.settings_blob),&saved));
    assert(saved.origin_conversion_hz==50000000&&saved.timebase_ns==10000&&saved.language==SCOPE_ZH_CN);
    settings_equal(&ui,&saved,6);uint8_t saved_blob[96];memcpy(saved_blob,ui.settings_blob,sizeof(saved_blob));
    click(ui.settings_button);lv_obj_update_layout(lv_screen_active());
    assert(lv_obj_get_y(ui.settings_dialog)==55&&lv_obj_get_height(ui.settings_dialog)==490);
    assert(lv_obj_get_y(ui.parameter_button)==180&&lv_obj_get_y(ui.language_button)==64&&lv_obj_get_y(ui.grid_button)==122);
    assert(lv_obj_get_y(ui.trigger_mode_button)==238&&lv_obj_get_y(ui.dual_button)==296&&lv_obj_get_y(ui.close_button)==414);
    assert(lv_obj_has_flag(ui.bars_button,LV_OBJ_FLAG_HIDDEN));save_frame(display,directory,"settings-entry-zh");
    click(ui.parameter_button);assert(!lv_obj_has_flag(ui.parameter_panel,LV_OBJ_FLAG_HIDDEN));save_frame(display,directory,"settings-saved-zh");
    epoch=ui.capture_epoch;held=snapshot(&ui);click(ui.parameter_save_button);
    assert(ui.capture_epoch==epoch);unchanged_data(&ui,&held);assert(!memcmp(saved_blob,ui.settings_blob,sizeof(saved_blob)));
    click(ui.parameter_close_button);click(ui.language_button);assert(ui.language==SCOPE_EN);click(ui.parameter_button);
    save_frame(display,directory,"settings-panel-en");
    /* Restore preserves a paused raw record and its original rate/caches. */
    scope_ui_set_running(&ui,false);assert(scope_ui_configure_channel(&ui,1,5,1500));ui.timebase_index=20;
    epoch=ui.capture_epoch;held=snapshot(&ui);click(ui.parameter_load_button);
    assert(ui.capture_epoch==epoch+1&&!ui.model.running&&!ui.single_armed&&!ui.single_complete&&!ui.adc_triggered&&!ui.adc_waiting);
    assert(ui.language==SCOPE_ZH_CN&&!ui.adc_display_start&&!ui.adc_trigger_crossing);settings_equal(&ui,&saved,6);unchanged_data(&ui,&held);
    assert(!memcmp(saved_blob,ui.settings_blob,sizeof(saved_blob)));save_frame(display,directory,"settings-restored-zh");
    /* Default never clears either saved slot; load can undo the default. */
    epoch=ui.capture_epoch;held=snapshot(&ui);click(ui.parameter_default_button);
    assert(ui.capture_epoch==epoch+1&&!ui.model.running&&!ui.single_complete&&!ui.adc_triggered);settings_equal(&ui,&defaults,10);unchanged_data(&ui,&held);
    assert(!memcmp(saved_blob,ui.settings_blob,sizeof(saved_blob))&&ui.settings_valid);save_frame(display,directory,"settings-defaults-zh");
    click(ui.parameter_load_button);settings_equal(&ui,&saved,6);click(ui.parameter_close_button);click(ui.close_button);
    /* Saving a completed single or an armed single has no acquisition effect. */
    assert(scope_ui_configure_trigger(&ui,false,true,500));assert(scope_ui_arm_single(&ui)&&feed(&ui,3)&&ui.single_complete);
    held=snapshot(&ui);epoch=ui.capture_epoch;assert(scope_ui_settings_save(&ui)&&ui.single_complete&&!ui.single_armed&&ui.capture_epoch==epoch);unchanged_data(&ui,&held);
    epoch=ui.capture_epoch;held=snapshot(&ui);assert(scope_ui_settings_defaults(&ui)&&!ui.single_complete&&!ui.model.running&&ui.capture_epoch==epoch+1);unchanged_data(&ui,&held);
    assert(scope_ui_settings_load(&ui)&&ui.adc_channel==1);
    assert(scope_ui_arm_single(&ui));held=snapshot(&ui);epoch=ui.capture_epoch;unsigned after=ui.single_armed_after_id;
    assert(scope_ui_settings_save(&ui)&&ui.single_armed&&ui.capture_epoch==epoch&&ui.single_armed_after_id==after);unchanged_data(&ui,&held);
    assert(scope_ui_settings_defaults(&ui)&&!ui.single_armed&&!ui.single_complete&&ui.model.running&&ui.capture_epoch==epoch+1);
    assert(scope_ui_settings_load(&ui)&&ui.adc_channel==1);
    /* Corruption changes only the notice, never preferences or data. */
    held=snapshot(&ui);epoch=ui.capture_epoch;int time=ui.timebase_index;ScopeSettings cache=ui.settings_cache;
    ui.settings_blob[35]^=1;assert(!scope_ui_settings_load(&ui)&&ui.settings_notice&&ui.capture_epoch==epoch&&ui.timebase_index==time);
    assert(!memcmp(&cache,&ui.settings_cache,sizeof(cache)));unchanged_data(&ui,&held);ui.settings_blob[35]^=1;
    /* Default does not depend on a valid slot and retains even its damaged bytes. */
    ui.settings_blob[35]^=1;uint32_t bad_crc=scope_record_crc32(ui.settings_blob,sizeof(ui.settings_blob));
    held=snapshot(&ui);epoch=ui.capture_epoch;assert(scope_ui_settings_defaults(&ui)&&ui.capture_epoch==epoch+1);unchanged_data(&ui,&held);
    assert(ui.settings_valid&&bad_crc==scope_record_crc32(ui.settings_blob,sizeof(ui.settings_blob)));ui.settings_blob[35]^=1;
    assert(scope_ui_settings_load(&ui));
    /* A different origin maps physical time and never sets the hardware clock. */
    ScopeSettings external=ui.settings_cache;external.origin_conversion_hz=25000000;external.timebase_ns=10000;size_t written;
    assert(!scope_settings_encode(&external,ui.settings_blob,sizeof(ui.settings_blob),&written));epoch=ui.capture_epoch;held=snapshot(&ui);
    assert(scope_ui_settings_load(&ui)&&ui.timebase_index==6&&ui.conversion_hz==50000000&&ui.capture_epoch==epoch+1);unchanged_data(&ui,&held);
    external.origin_conversion_hz=50000000;external.timebase_ns=100;
    assert(!scope_settings_encode(&external,ui.settings_blob,sizeof(ui.settings_blob),&written));assert(scope_ui_set_conversion_hz(&ui,5000000));
    held=snapshot(&ui);epoch=ui.capture_epoch;time=ui.timebase_index;
    assert(!scope_ui_settings_load(&ui)&&ui.capture_epoch==epoch&&ui.timebase_index==time);unchanged_data(&ui,&held);
    /* Save uses the current platform even while the shown record is from 50M. */
    assert(ui.adc_info.conversion_hz==50000000&&scope_ui_settings_save(&ui)&&ui.settings_cache.origin_conversion_hz==5000000);
    assert(scope_ui_set_conversion_hz(&ui,50000000));ui.timebase_index=6;assert(feed(&ui,4));
    scope_ui_set_running(&ui,false);assert(scope_ui_memory_load(&ui));all_rejected(&ui);assert(!scope_ui_show_parameters(&ui,true));
    assert(scope_ui_memory_return_live(&ui));assert(scope_ui_select_source(&ui,2));all_rejected(&ui);assert(!scope_ui_show_parameters(&ui,true));
    assert(scope_ui_select_source(&ui,1));ui.bars_on=true;all_rejected(&ui);ui.bars_on=false;
    ui.adaptive_sampling=false;all_rejected(&ui);ui.adaptive_sampling=true;ui.hardware_trigger=false;all_rejected(&ui);ui.hardware_trigger=true;
    ui.timebase_index=6;assert(feed(&ui,5)&&scope_ui_autoset_start(&ui));all_rejected(&ui);assert(!scope_ui_show_parameters(&ui,true));scope_ui_autoset_cancel(&ui);
    /* New sessions start with both volatile slots empty. */
    lv_deinit();lv_init();display=lv_display_create(1024,600);lv_display_set_buffers(display,buffer,NULL,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(display,flush);
    scope_ui_create(&ui);assert(!ui.settings_valid&&!ui.memory_valid);lv_deinit();
    puts("SETTINGS_UI_TEST_PASS: pointer bilingual/layout/glyphs, independent slots, save atomicity, restore/default epoch and raw cache protection, physical mapping and guards");return 0;
}
