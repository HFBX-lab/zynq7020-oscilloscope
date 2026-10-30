#include "scope_ui.h"
#include "src/misc/lv_text_private.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static uint32_t buffer[1024*60],record[4096];
static const double fast_time[]={.000001,.000002,.000005,.00001,.00002};
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *p){(void)a;(void)p;lv_display_flush_ready(d);}
static void adjust(ScopeUI *ui,unsigned target){
 while(ui->timebase_index<(int)target)lv_obj_send_event(ui->adjust_buttons[1],LV_EVENT_CLICKED,NULL);
 while(ui->timebase_index>(int)target)lv_obj_send_event(ui->adjust_buttons[0],LV_EVENT_CLICKED,NULL);
 assert(ui->timebase_index==(int)target);
}
static void labels(lv_obj_t *obj,bool hidden){
 hidden=hidden||lv_obj_has_flag(obj,LV_OBJ_FLAG_HIDDEN);
 if(lv_obj_check_type(obj,&lv_label_class)){
  const char *s=lv_label_get_text(obj);uint32_t i=0;
  while(s[i]){uint32_t c=lv_text_encoded_next(s,&i);if(c=='\n')continue;lv_font_glyph_dsc_t g={0};
   assert(lv_font_get_glyph_dsc(lv_obj_get_style_text_font(obj,LV_PART_MAIN),&g,c,0)&&!g.is_placeholder);}
  if(!hidden){lv_obj_t *p=lv_obj_get_parent(obj);assert(lv_obj_get_x(obj)>=0&&lv_obj_get_y(obj)>=0);
   assert(lv_obj_get_x(obj)+lv_obj_get_width(obj)<=lv_obj_get_width(p));assert(lv_obj_get_y(obj)+lv_obj_get_height(obj)<=lv_obj_get_height(p));}
 }
 for(unsigned i=0;i<lv_obj_get_child_count(obj);i++)labels(lv_obj_get_child(obj,i),hidden);
}
static void make_edge(unsigned after){
 for(unsigned i=0;i<4096;i++)record[i]=512u|((i%1000<500?540u:585u)<<16);
 record[after-1]=512u|(562u<<16);record[after]=512u|(564u<<16);
}
int main(void){
 ScopeCapturePlan p,old;
 for(unsigned i=0;i<18;i++){
  assert(scope_acquisition_plan_for(i,10000000,&p)==0&&p.conversion_hz==10000000&&p.count==4096);
  if(i<5)assert(p.rate_index==4&&p.rate_hz==10000000&&p.filter_kind==0&&p.timeout_ms==101);
  else{assert(!scope_acquisition_plan(i-5,&old));assert(p.rate_index==old.rate_index&&p.rate_hz==old.rate_hz&&p.filter_kind==3&&p.timeout_ms==old.timeout_ms);}
 }
 assert(scope_acquisition_plan_for(18,10000000,&p)<0&&scope_acquisition_plan_for(13,5000000,&p)<0);
 assert(scope_acquisition_plan_for(0,12500000,&p)<0&&scope_acquisition_plan_for(0,10000000,NULL)<0);
 lv_init();lv_display_t *d=lv_display_create(1024,600);lv_display_set_color_format(d,LV_COLOR_FORMAT_XRGB8888);
 lv_display_set_buffers(d,buffer,NULL,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(d,flush);
 ScopeUI ui;scope_ui_create(&ui);lv_obj_update_layout(lv_screen_active());assert(ui.conversion_hz==5000000&&ui.timebase_index==2);
 scope_ui_enable_adaptive_sampling(&ui,true);scope_ui_enable_hardware_trigger(&ui,true);scope_ui_select_trigger_source(&ui,1);
 uint32_t epoch=ui.capture_epoch;assert(scope_ui_set_conversion_hz(&ui,10000000)&&ui.timebase_index==7&&ui.capture_epoch==epoch+1);
 assert(!scope_ui_capture_plan(&ui,&p)&&p.rate_hz==625000&&p.filter_kind==3);
 epoch=ui.capture_epoch;assert(scope_ui_set_conversion_hz(&ui,10000000)&&ui.capture_epoch==epoch);
 assert(!scope_ui_set_conversion_hz(&ui,40000000)&&ui.capture_epoch==epoch&&ui.conversion_hz==10000000);
 assert(!scope_ui_set_conversion_hz(NULL,10000000)&&scope_ui_capture_plan(NULL,&p)<0&&scope_ui_capture_plan(&ui,NULL)<0);
 ScopeAdcMetrics m[2]={{0},{1,.6,10000,true}};unsigned id=1;
 scope_ui_set_dual_display(&ui,true);scope_ui_set_cursors(&ui,true);
 lv_obj_send_event(ui.settings_button,LV_EVENT_CLICKED,NULL);
 for(unsigned i=0;i<18;i++){
  adjust(&ui,i);assert(!scope_ui_capture_plan(&ui,&p));ScopeHardwareConfig cfg;assert(!scope_ui_hardware_config(&ui,&cfg));
  make_edge(cfg.pre_samples);ScopeRecordInfo info={ui.capture_epoch,p.rate_index,p.conversion_hz,p.filter_kind};
  ScopeHardwareTrigger edge={true,false,cfg.pre_samples};
  ScopeRecordInfo bad=info;bad.filter_kind=i<5?3:0;unsigned seen=ui.adc_last_seen_id;
  assert(!scope_ui_set_hardware_adc_record(&ui,record,4096,p.rate_hz,id+100,m,&bad,&edge)&&ui.adc_last_seen_id==seen);
  bad=info;bad.conversion_hz=5000000;assert(!scope_ui_set_hardware_adc_record(&ui,record,4096,p.rate_hz,id+100,m,&bad,&edge));
  assert(scope_ui_set_hardware_adc_record(&ui,record,4096,p.rate_hz,id++,m,&info,&edge));
  assert(ui.adc_info.filter_kind==p.filter_kind&&ui.adc_rate==p.rate_hz&&ui.adc_info.conversion_hz==10000000);
  assert(ui.samples[SCOPE_POINTS-1]!=LV_CHART_POINT_NONE&&ui.secondary_samples[SCOPE_POINTS-1]!=LV_CHART_POINT_NONE);
  if(i<5){
   assert(fabs(ui.cursor_result.delta_s-5*fast_time[i])<1e-12);
   assert(fabs(ui.adc_trigger_crossing-ui.adc_display_start-.2*10*fast_time[i]*p.rate_hz)<1e-9);
   assert(strstr(lv_label_get_text(ui.footer_label),"10 MSa/s")&&strstr(lv_label_get_text(ui.footer_label),"直采"));
   assert(!strstr(lv_label_get_text(ui.footer_label),"数字通带"));
   assert(strstr(lv_label_get_text(ui.settings_note),"无数字抗混叠滤波")&&strstr(lv_label_get_text(ui.settings_note),"电压读数尚未校准"));
  }else assert(strstr(lv_label_get_text(ui.footer_label),"数字通带"));
  lv_obj_update_layout(lv_screen_active());labels(lv_screen_active(),false);
  assert(lv_obj_get_y(ui.settings_note)+lv_obj_get_height(ui.settings_note)<=lv_obj_get_y(ui.close_button));
 }
 lv_obj_send_event(ui.close_button,LV_EVENT_CLICKED,NULL);
 /* A frozen RAW record keeps its metadata when the requested profile changes. */
 adjust(&ui,0);assert(!scope_ui_capture_plan(&ui,&p));assert(scope_ui_arm_single(&ui));ScopeHardwareConfig cfg;assert(!scope_ui_hardware_config(&ui,&cfg));
 make_edge(cfg.pre_samples);ScopeHardwareTrigger edge={true,false,cfg.pre_samples};ScopeRecordInfo info={ui.capture_epoch,4,10000000,0};
 assert(scope_ui_set_hardware_adc_record(&ui,record,4096,10000000,id++,m,&info,&edge)&&ui.single_complete);
 unsigned held_id=ui.adc_id;ScopeRecordInfo held=ui.adc_info;
 assert(scope_ui_set_conversion_hz(&ui,5000000)&&ui.timebase_index==0&&!ui.single_complete&&!ui.adc_triggered);
 assert(ui.adc_id==held_id&&ui.adc_rate==10000000&&!memcmp(&ui.adc_info,&held,sizeof(held)));
 assert(strstr(lv_label_get_text(ui.footer_label),"直采")&&!strstr(lv_label_get_text(ui.footer_label),"数字通带"));
 assert(ui.samples[SCOPE_POINTS-1]==LV_CHART_POINT_NONE&&scope_ui_arm_single(&ui));
 assert(scope_ui_set_conversion_hz(&ui,10000000)&&ui.timebase_index==5&&!ui.single_armed);
 adjust(&ui,0);scope_ui_set_running(&ui,true);assert(!scope_ui_capture_plan(&ui,&p));assert(!scope_ui_hardware_config(&ui,&cfg));
 info=(ScopeRecordInfo){ui.capture_epoch,4,10000000,0};make_edge(cfg.pre_samples);edge.after_index=cfg.pre_samples;
 assert(scope_ui_set_hardware_adc_record(&ui,record,4096,10000000,id++,m,&info,&edge));
 assert(scope_ui_move_cursors(&ui,0,10));
 assert(strstr(lv_label_get_text(ui.vpp_label),"+0.100 µs"));
 assert(strstr(lv_label_get_text(ui.metric_titles[0]),"µs"));
 lv_obj_send_event(ui.settings_button,LV_EVENT_CLICKED,NULL);lv_obj_send_event(ui.language_button,LV_EVENT_CLICKED,NULL);
 assert(strstr(lv_label_get_text(ui.footer_label),"RAW")&&!strstr(lv_label_get_text(ui.footer_label),"PASSBAND"));
 assert(strstr(lv_label_get_text(ui.settings_note),"no digital antialias filter"));
 lv_obj_update_layout(lv_screen_active());labels(lv_screen_active(),false);
 assert(lv_obj_get_y(ui.settings_note)+lv_obj_get_height(ui.settings_note)<=lv_obj_get_y(ui.close_button));
 lv_deinit();puts("HIGHSPEED_UI_PASS: 18 windows, RAW/CIC profile rejection, index/epoch conversion, frozen metadata, trigger/cursor/dual, bilingual fonts/layout");return 0;
}
