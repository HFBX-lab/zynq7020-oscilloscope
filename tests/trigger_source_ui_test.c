#include "scope_ui.h"
#include "src/misc/lv_text_private.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static uint32_t frame[1024*600],buffer[1024*60],record[4096],original[4096];
static lv_indev_data_t finger;
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *p){unsigned w=(unsigned)(a->x2-a->x1+1);for(int y=a->y1;y<=a->y2;y++)memcpy(frame+y*1024+a->x1,p+(y-a->y1)*w*4,w*4);lv_display_flush_ready(d);}
static void pointer(lv_indev_t *d,lv_indev_data_t *out){(void)d;*out=finger;}
static void point(int x,int y,bool down){finger.point=(lv_point_t){x,y};finger.state=down?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;for(int i=0;i<6;i++){lv_tick_inc(10);lv_timer_handler();}}
static void click(lv_obj_t *o){assert(o);lv_obj_update_layout(lv_screen_active());lv_area_t a;lv_obj_get_coords(o,&a);point((a.x1+a.x2)/2,(a.y1+a.y2)/2,true);point((a.x1+a.x2)/2,(a.y1+a.y2)/2,false);}
static void labels(lv_obj_t *o,bool hidden){
 hidden=hidden||lv_obj_has_flag(o,LV_OBJ_FLAG_HIDDEN);
 if(lv_obj_check_type(o,&lv_label_class)){
  const char *s=lv_label_get_text(o);uint32_t i=0;while(s[i]){uint32_t c=lv_text_encoded_next(s,&i);if(c=='\n')continue;lv_font_glyph_dsc_t g={0};assert(lv_font_get_glyph_dsc(lv_obj_get_style_text_font(o,LV_PART_MAIN),&g,c,0)&&!g.is_placeholder);}
  if(!hidden){lv_obj_t *p=lv_obj_get_parent(o);assert(lv_obj_get_x(o)>=0&&lv_obj_get_y(o)>=0);assert(lv_obj_get_x(o)+lv_obj_get_width(o)<=lv_obj_get_width(p));assert(lv_obj_get_y(o)+lv_obj_get_height(o)<=lv_obj_get_height(p));}
 }
 for(unsigned i=0;i<lv_obj_get_child_count(o);i++)labels(lv_obj_get_child(o,i),hidden);
}
static void save(lv_display_t *d,const char *dir,const char *name){if(!dir)return;lv_refr_now(d);char path[512];snprintf(path,sizeof(path),"%s/%s.bgrx",dir,name);FILE *f=fopen(path,"wb");assert(f);assert(fwrite(frame,1,sizeof(frame),f)==sizeof(frame));assert(!fclose(f));}
int main(int argc,char **argv){
 const char *dir=argc>1?argv[1]:NULL;lv_init();lv_display_t *d=lv_display_create(1024,600);lv_display_set_color_format(d,LV_COLOR_FORMAT_XRGB8888);lv_display_set_buffers(d,buffer,NULL,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(d,flush);
 lv_indev_t *input=lv_indev_create();lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(input,pointer);
 for(unsigned i=0;i<4096;i++)record[i]=512u|((unsigned)lround(563+60*sin(6.283185307179586*(i+100)/625))<<16);
 memcpy(original,record,sizeof(record));ScopeUI ui;scope_ui_create(&ui);ScopeAdcMetrics m[2]={{0,.0049,0,false},{1.2,.7,1000,true}};
 assert(scope_ui_set_adc_record(&ui,record,4096,625000,1,m)&&!ui.adc_triggered);
 assert(ui.trigger_source_button&&"Independent trigger source control missing");
 click(ui.trigger_source_button);assert(ui.trigger_channel==1&&ui.adc_channel==0);
 assert(scope_ui_configure_trigger(&ui,true,true,500));assert(scope_ui_set_adc_record(&ui,record,4096,625000,2,m));
 assert(ui.adc_triggered&&fabs(ui.adc_trigger_crossing-ui.adc_display_start-250)<1e-6);
 assert(!strcmp(lv_label_get_text(ui.readout_label),"--"));for(unsigned i=0;i<SCOPE_POINTS;i++)assert(ui.samples[i]==10);
 scope_ui_set_dual_display(&ui,true);assert(scope_ui_arm_single(&ui));uint32_t epoch=ui.capture_epoch;
 click(ui.wave_buttons[1]);assert(ui.adc_channel==1&&ui.trigger_channel==1&&ui.single_armed&&ui.capture_epoch==epoch);
 assert(scope_ui_set_adc_record(&ui,record,4096,625000,3,m)&&ui.single_complete&&!ui.model.running);
 double start=ui.adc_display_start,cross=ui.adc_trigger_crossing;
 click(ui.wave_buttons[0]);assert(ui.adc_channel==0&&ui.single_complete&&ui.adc_id==3&&ui.capture_epoch==epoch&&ui.adc_triggered);
 assert(ui.adc_display_start==start&&ui.adc_trigger_crossing==cross&&!strcmp(lv_label_get_text(ui.readout_label),"--"));
 assert(!memcmp(original,record,sizeof(record)));lv_obj_update_layout(lv_screen_active());labels(lv_screen_active(),false);save(d,dir,"CH1-measure-CH2-trigger-zh");
 click(ui.trigger_source_button);assert(ui.trigger_channel==0&&ui.capture_epoch==epoch+1&&!ui.single_complete&&!ui.adc_triggered&&ui.adc_id==3);
 scope_ui_set_running(&ui,true);assert(!scope_ui_set_adc_record(&ui,record,4096,625000,4,m)&&ui.adc_waiting);
 epoch=ui.capture_epoch;click(ui.wave_buttons[1]);assert(ui.adc_channel==1&&ui.trigger_channel==0&&ui.capture_epoch==epoch&&ui.adc_waiting);
 assert(scope_ui_arm_single(&ui));assert(!scope_ui_set_adc_record(&ui,record,4096,625000,5,m)&&ui.single_armed);
 epoch=ui.capture_epoch;click(ui.trigger_source_button);assert(ui.trigger_channel==1&&ui.capture_epoch==epoch+1&&ui.single_armed&&ui.single_armed_after_id==5);
 assert(!scope_ui_set_adc_record(&ui,record,4096,625000,5,m));assert(scope_ui_set_adc_record(&ui,record,4096,625000,6,m)&&ui.single_complete);
 click(ui.wave_buttons[0]);assert(ui.single_complete&&ui.adc_id==6);
 click(ui.settings_button);click(ui.language_button);click(ui.close_button);lv_obj_update_layout(lv_screen_active());labels(lv_screen_active(),false);save(d,dir,"CH1-measure-CH2-trigger-en");
 assert(scope_ui_select_source(&ui,2)&&ui.trigger_channel==1);assert(lv_obj_has_state(ui.trigger_source_button,LV_STATE_DISABLED));click(ui.trigger_source_button);assert(ui.trigger_channel==1);
 assert(scope_ui_select_source(&ui,0)&&ui.trigger_channel==1);scope_ui_set_running(&ui,true);assert(scope_ui_set_adc_record(&ui,record,4096,625000,7,m)&&ui.adc_triggered);
 lv_deinit();puts("TRIGGER_SOURCE_UI_TEST_PASS: CH2 triggers CH1 readout, measurement preserves armed/frozen state, trigger epoch/new-ID gates, DEMO isolation, pointer/fonts/layout");
}
