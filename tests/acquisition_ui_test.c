#include "scope_ui.h"
#include "src/misc/lv_text_private.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static uint32_t framebuffer[1024*600];
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *p){
 unsigned width=(unsigned)(a->x2-a->x1+1);
 for(int y=a->y1;y<=a->y2;y++)memcpy(framebuffer+y*1024+a->x1,p+(y-a->y1)*width*4,width*4);
 lv_display_flush_ready(d);
}
static void save(lv_display_t *d,const char *directory,const char *name){
 if(!directory)return;lv_refr_now(d);char path[512];snprintf(path,sizeof(path),"%s/%s.bgrx",directory,name);
 FILE *f=fopen(path,"wb");assert(f);assert(fwrite(framebuffer,1,sizeof(framebuffer),f)==sizeof(framebuffer));assert(fclose(f)==0);
}
static void fonts(lv_obj_t *o){
 if(lv_obj_check_type(o,&lv_label_class)){
  const char *s=lv_label_get_text(o);uint32_t i=0;
  while(s[i]){uint32_t cp=lv_text_encoded_next(s,&i);if(cp=='\n')continue;lv_font_glyph_dsc_t g={0};
   assert(lv_font_get_glyph_dsc(lv_obj_get_style_text_font(o,LV_PART_MAIN),&g,cp,0)&&!g.is_placeholder);}
 }
 for(unsigned i=0;i<lv_obj_get_child_count(o);i++)fonts(lv_obj_get_child(o,i));
}
static uint32_t record[4096],phase_record[4096],flat[4096];
int main(int argc,char **argv){
 const char *preview=argc>1?argv[1]:NULL;
 static uint32_t buffer[1024*60];
 lv_init();lv_display_t *d=lv_display_create(1024,600);
 lv_display_set_color_format(d,LV_COLOR_FORMAT_XRGB8888);
 lv_display_set_buffers(d,buffer,NULL,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(d,flush);
 for(unsigned i=0;i<4096;i++){
  unsigned c=(unsigned)lround(563+52*sin(6.283185307179586*i/625));
  unsigned shifted=(unsigned)lround(563+52*sin(6.283185307179586*(i+157)/625));
  record[i]=c|(c<<16);phase_record[i]=shifted|(shifted<<16);flat[i]=512u|(512u<<16);
 }
 ScopeUI ui;scope_ui_create(&ui);ScopeAdcMetrics m[2]={{1,.6,1000,true},{1,.6,1000,true}};
 assert(scope_ui_set_adc_record(&ui,record,4096,625000,1,m));
 assert(ui.trigger_rising&&ui.trigger_level_mv==500&&!ui.trigger_normal);
 assert(ui.adc_triggered&&!ui.adc_waiting);
 assert(fabs(ui.adc_trigger_crossing-ui.adc_display_start-250)<1e-6);
 assert(scope_ui_set_adc_record(&ui,phase_record,4096,625000,2,m));
 assert(fabs(ui.adc_trigger_crossing-ui.adc_display_start-250)<1e-6);
 fonts(lv_screen_active());save(d,preview,"trigger-running");
 assert(scope_ui_configure_trigger(&ui,true,true,4500));
 int32_t held[SCOPE_POINTS];memcpy(held,ui.samples,sizeof(held));
 assert(!scope_ui_set_adc_record(&ui,flat,4096,625000,3,m));
 assert(ui.adc_id==2&&ui.adc_last_seen_id==3&&ui.adc_waiting);
 assert(memcmp(held,ui.samples,sizeof(held))==0);
 assert(scope_ui_configure_trigger(&ui,true,true,500));
 const uint32_t *normal_held=ui.adc_record;
 lv_obj_send_event(ui.adjust_buttons[1],LV_EVENT_CLICKED,NULL);
 assert(ui.adc_id==2&&ui.adc_record==normal_held&&ui.adc_waiting&&ui.adc_triggered);
 assert(fabs(ui.adc_trigger_crossing-ui.adc_display_start-625)<1e-6);
 lv_obj_send_event(ui.adjust_buttons[0],LV_EVENT_CLICKED,NULL);
 assert(scope_ui_configure_trigger(&ui,true,true,4500));
 assert(scope_ui_arm_single(&ui));uint32_t epoch=ui.capture_epoch;
 assert(ui.model.running&&ui.single_armed&&!ui.single_complete);
 assert(!scope_ui_set_adc_record(&ui,record,4096,625000,4,m));
 assert(ui.single_armed&&ui.adc_id==2);
 assert(scope_ui_configure_trigger(&ui,true,true,500));
 assert(ui.capture_epoch>epoch&&ui.single_armed_after_id==4);
 assert(!scope_ui_set_adc_record(&ui,record,4096,625000,4,m));
 assert(scope_ui_set_adc_record(&ui,record,4096,625000,5,m));
 assert(!ui.model.running&&!ui.single_armed&&ui.single_complete&&ui.adc_id==5);
 uint32_t single_id=ui.adc_id;
 lv_obj_send_event(ui.adjust_buttons[1],LV_EVENT_CLICKED,NULL);assert(ui.timebase_index==3);
 assert(ui.single_complete&&ui.adc_id==single_id&&ui.adc_triggered);
 assert(fabs(ui.adc_trigger_crossing-ui.adc_display_start-625)<1e-6);
 lv_obj_send_event(ui.adjust_buttons[1],LV_EVENT_CLICKED,NULL);assert(ui.timebase_index==4);
 assert(ui.single_complete&&ui.adc_id==single_id&&!ui.adc_triggered&&ui.adc_display_start==0);
 assert(ui.samples[SCOPE_POINTS-1]==LV_CHART_POINT_NONE);
 lv_obj_send_event(ui.adjust_buttons[0],LV_EVENT_CLICKED,NULL);
 lv_obj_send_event(ui.adjust_buttons[0],LV_EVENT_CLICKED,NULL);
 assert(ui.timebase_index==2&&ui.adc_triggered&&fabs(ui.adc_trigger_crossing-ui.adc_display_start-250)<1e-6);
 save(d,preview,"single-complete");
 assert(!scope_ui_set_adc_record(&ui,phase_record,4096,625000,6,m)&&ui.adc_id==5);
 scope_ui_set_running(&ui,true);assert(!ui.single_complete&&ui.model.running);
 assert(scope_ui_configure_trigger(&ui,false,false,500));
 assert(scope_ui_set_adc_record(&ui,record,4096,625000,7,m)&&ui.adc_triggered);
 assert(scope_ui_select_source(&ui,1)&&ui.adc_channel==1);
 assert(scope_ui_set_adc_record(&ui,record,4096,625000,8,m));
 assert(scope_ui_select_source(&ui,2));assert(!scope_ui_arm_single(&ui));
 assert(!scope_ui_select_source(&ui,3));assert(!scope_ui_configure_trigger(&ui,false,true,5001));
 assert(scope_ui_select_source(&ui,0));ui.timebase_index=6;
 assert(!scope_ui_arm_single(&ui)); /* Visible50ms cannot fit a6.55ms record. */
 assert(scope_ui_set_adc_record(&ui,record,4096,625000,9,m));
 assert(!ui.adc_triggered&&ui.samples[SCOPE_POINTS-1]==LV_CHART_POINT_NONE);
 ui.timebase_index=2;assert(scope_ui_configure_trigger(&ui,false,true,500));
 scope_ui_set_running(&ui,false);lv_obj_send_event(ui.single_button,LV_EVENT_CLICKED,NULL);assert(ui.single_armed);
 assert(scope_ui_set_adc_record(&ui,record,4096,625000,10,m)&&ui.single_complete);
 lv_obj_send_event(ui.settings_button,LV_EVENT_CLICKED,NULL);fonts(lv_screen_active());save(d,preview,"trigger-settings-zh");
 lv_obj_send_event(ui.language_button,LV_EVENT_CLICKED,NULL);assert(ui.language==SCOPE_EN);fonts(lv_screen_active());save(d,preview,"trigger-settings-en");
 lv_obj_send_event(ui.trigger_mode_button,LV_EVENT_CLICKED,NULL);assert(ui.trigger_normal);
 lv_obj_send_event(ui.trigger_button,LV_EVENT_CLICKED,NULL);assert(!ui.trigger_rising);
 scope_ui_set_running(&ui,false);ui.timebase_index=3;assert(scope_ui_arm_single(&ui));
 uint32_t prior_epoch=ui.capture_epoch;
 lv_obj_send_event(ui.adjust_buttons[1],LV_EVENT_CLICKED,NULL);
 assert(ui.timebase_index==4&&ui.capture_epoch>prior_epoch&&!ui.single_armed&&!ui.model.running);
 lv_deinit();puts("ACQUISITION_UI_TEST_PASS: edge alignment, NORMAL hold, single epoch/ID gate/freeze, source and window guards");return 0;
}
