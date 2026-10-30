#include "scope_ui.h"
#include "src/misc/lv_text_private.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static uint32_t buffer[1024*60],record[4096];
static const unsigned rates[]={25000000,25000000,25000000,25000000,6250000,6250000,625000,625000,625000,62500,62500,62500,6250,6250,6250,625,625,625};
static const unsigned rate_indices[]={4,4,4,4,5,5,0,0,0,1,1,1,2,2,2,3,3,3};
static const unsigned pre_counts[]={51,101,251,501,251,626,126,251,626,126,251,626,126,251,626,126,251,626};
static const double seconds[]={.000001,.000002,.000005,.00001,.00002,.00005,.0001,.0002,.0005,.001,.002,.005,.01,.02,.05,.1,.2,.5};
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
  if(!hidden){lv_obj_t *parent=lv_obj_get_parent(obj);assert(lv_obj_get_x(obj)>=0&&lv_obj_get_y(obj)>=0);
   assert(lv_obj_get_x(obj)+lv_obj_get_width(obj)<=lv_obj_get_width(parent));assert(lv_obj_get_y(obj)+lv_obj_get_height(obj)<=lv_obj_get_height(parent));}
 }
 for(unsigned i=0;i<lv_obj_get_child_count(obj);i++)labels(lv_obj_get_child(obj,i),hidden);
}
static void make_record(unsigned after,bool rising){
 for(unsigned i=0;i<4096;i++)record[i]=512u|((i%1000<500?540u:585u)<<16);
 record[after-1]=512u|((rising?562u:564u)<<16);record[after]=512u|((rising?564u:562u)<<16);
}
static void reject(ScopeUI *ui,unsigned rate,unsigned id,const ScopeRecordInfo *info,const ScopeHardwareTrigger *edge){
 ScopeAdcMetrics m[2]={{0}};unsigned seen=ui->adc_last_seen_id,shown=ui->adc_id;ScopeRecordInfo held=ui->adc_info;const uint32_t *data=ui->adc_record;
 assert(!scope_ui_set_hardware_adc_record(ui,record,4096,rate,id,m,info,edge));
 assert(ui->adc_last_seen_id==seen&&ui->adc_id==shown&&ui->adc_record==data&&!memcmp(&held,&ui->adc_info,sizeof(held)));
}
int main(void){
 ScopeCapturePlan p;
 for(unsigned i=0;i<18;i++){
  assert(scope_acquisition_plan_for(i,25000000,&p)==0);
  assert(p.conversion_hz==25000000&&p.rate_hz==rates[i]&&p.rate_index==rate_indices[i]&&p.count==4096);
  assert(p.filter_kind==(i<4?SCOPE_FILTER_RAW:SCOPE_FILTER_CIC3));
  assert(p.timeout_ms==(4096000u+rates[i]-1)/rates[i]+100);
  assert(10*seconds[i]*p.rate_hz<=3125.0000001);
 }
 assert(scope_acquisition_plan_for(18,25000000,&p)<0&&scope_acquisition_plan_for(0,40000000,&p)<0);
 lv_init();lv_display_t *d=lv_display_create(1024,600);lv_display_set_color_format(d,LV_COLOR_FORMAT_XRGB8888);
 lv_display_set_buffers(d,buffer,NULL,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(d,flush);
 ScopeUI ui;scope_ui_create(&ui);lv_obj_update_layout(lv_screen_active());
 scope_ui_enable_adaptive_sampling(&ui,true);scope_ui_enable_hardware_trigger(&ui,true);scope_ui_select_trigger_source(&ui,1);
 scope_ui_set_dual_display(&ui,true);scope_ui_set_cursors(&ui,true);
 uint32_t epoch=ui.capture_epoch;assert(scope_ui_set_conversion_hz(&ui,25000000)&&ui.timebase_index==7&&ui.capture_epoch==epoch+1);
 epoch=ui.capture_epoch;assert(scope_ui_set_conversion_hz(&ui,25000000)&&ui.capture_epoch==epoch);
 ScopeAdcMetrics metrics[2]={{0},{1,.6,100000,true}};unsigned id=1;
 lv_obj_send_event(ui.settings_button,LV_EVENT_CLICKED,NULL);
 for(unsigned i=0;i<18;i++){
  adjust(&ui,i);assert(!scope_ui_capture_plan(&ui,&p)&&p.rate_hz==rates[i]);
  for(unsigned rising=0;rising<2;rising++){
   scope_ui_configure_trigger(&ui,false,rising!=0,500);ScopeHardwareConfig cfg;assert(!scope_ui_hardware_config(&ui,&cfg));
   if(cfg.pre_samples!=pre_counts[i])fprintf(stderr,"pre mismatch index=%u got=%u expected=%u\n",i,cfg.pre_samples,pre_counts[i]);
   assert(cfg.pre_samples==pre_counts[i]);
   make_record(cfg.pre_samples,rising!=0);ScopeHardwareTrigger edge={true,false,cfg.pre_samples};
   ScopeRecordInfo info={ui.capture_epoch,p.rate_index,p.conversion_hz,p.filter_kind};
   ScopeRecordInfo bad=info;bad.filter_kind=i<4?3:0;reject(&ui,p.rate_hz,id+100,&bad,&edge);
   bad=info;bad.conversion_hz=10000000;reject(&ui,p.rate_hz,id+100,&bad,&edge);
   if(i==4||i==5){bad=info;bad.rate_index=4;reject(&ui,p.rate_hz,id+100,&bad,&edge);reject(&ui,10000000,id+100,&info,&edge);}
   assert(scope_ui_set_hardware_adc_record(&ui,record,4096,p.rate_hz,id++,metrics,&info,&edge));
   assert(ui.adc_triggered&&ui.adc_rate==p.rate_hz&&ui.adc_info.filter_kind==p.filter_kind);
   assert(ui.samples[SCOPE_POINTS-1]!=LV_CHART_POINT_NONE&&ui.secondary_samples[SCOPE_POINTS-1]!=LV_CHART_POINT_NONE);
   assert(fabs(ui.adc_trigger_crossing-ui.adc_display_start-.2*10*seconds[i]*p.rate_hz)<1e-8);
   assert(fabs(ui.cursor_result.delta_s-5*seconds[i])<1e-10);
  }
  if(i<4){assert(strstr(lv_label_get_text(ui.footer_label),"25 MSa/s")&&strstr(lv_label_get_text(ui.footer_label),"直采"));assert(!strstr(lv_label_get_text(ui.footer_label),"数字通带"));}
  else if(i<6){assert(strstr(lv_label_get_text(ui.footer_label),"6.25 MSa/s")&&strstr(lv_label_get_text(ui.footer_label),"0–625 kHz"));}
  lv_obj_update_layout(lv_screen_active());labels(lv_screen_active(),false);
  assert(lv_obj_get_y(ui.settings_note)+lv_obj_get_height(ui.settings_note)<=lv_obj_get_y(ui.close_button));
 }
 /* Switching between high-speed profiles preserves each physical timebase;
    crossing the legacy boundary adds/removes exactly the five fast entries. */
 for(unsigned i=0;i<18;i++){
  adjust(&ui,i);epoch=ui.capture_epoch;assert(scope_ui_set_conversion_hz(&ui,10000000)&&ui.timebase_index==(int)i&&ui.capture_epoch==epoch+1);
  assert(scope_ui_set_conversion_hz(&ui,25000000)&&ui.timebase_index==(int)i);
  assert(scope_ui_set_conversion_hz(&ui,5000000)&&ui.timebase_index==(int)(i<5?0:i-5));
  assert(scope_ui_set_conversion_hz(&ui,25000000)&&ui.timebase_index==(int)(i<5?5:i));
 }
 lv_obj_send_event(ui.close_button,LV_EVENT_CLICKED,NULL);adjust(&ui,4);scope_ui_configure_trigger(&ui,false,true,500);
 assert(scope_ui_arm_single(&ui));assert(!scope_ui_capture_plan(&ui,&p));ScopeHardwareConfig cfg;assert(!scope_ui_hardware_config(&ui,&cfg));
 make_record(cfg.pre_samples,true);ScopeHardwareTrigger edge={true,false,cfg.pre_samples};ScopeRecordInfo info={ui.capture_epoch,5,25000000,3};
 assert(scope_ui_set_hardware_adc_record(&ui,record,4096,6250000,id++,metrics,&info,&edge)&&ui.single_complete);
 unsigned shown=ui.adc_id;ScopeRecordInfo held=ui.adc_info;
 epoch=ui.capture_epoch;assert(scope_ui_set_conversion_hz(&ui,10000000)&&ui.timebase_index==4&&ui.capture_epoch==epoch+1&&!ui.single_complete&&!ui.adc_triggered);
 assert(ui.adc_rate==6250000&&ui.adc_id==shown&&!memcmp(&held,&ui.adc_info,sizeof(held)));
 assert(strstr(lv_label_get_text(ui.footer_label),"数字通带")&&!strstr(lv_label_get_text(ui.footer_label),"直采"));
 assert(scope_ui_arm_single(&ui));ScopeRecordInfo stale={ui.capture_epoch,5,25000000,3};reject(&ui,6250000,id+100,&stale,&edge);
 assert(scope_ui_set_conversion_hz(&ui,25000000)&&ui.timebase_index==4&&!ui.single_armed);
 scope_ui_set_running(&ui,true);adjust(&ui,0);assert(!scope_ui_capture_plan(&ui,&p));assert(!scope_ui_hardware_config(&ui,&cfg));
 make_record(cfg.pre_samples,true);edge.after_index=cfg.pre_samples;info=(ScopeRecordInfo){ui.capture_epoch,4,25000000,0};
 assert(scope_ui_set_hardware_adc_record(&ui,record,4096,25000000,id++,metrics,&info,&edge));
 assert(scope_ui_move_cursors(&ui,0,10)&&strstr(lv_label_get_text(ui.vpp_label),"+0.100 µs"));
 held=ui.adc_info;shown=ui.adc_id;assert(scope_ui_set_conversion_hz(&ui,10000000));
 assert(ui.adc_rate==25000000&&ui.adc_id==shown&&!memcmp(&held,&ui.adc_info,sizeof(held))&&strstr(lv_label_get_text(ui.footer_label),"25 MSa/s"));
 assert(scope_ui_set_conversion_hz(&ui,25000000));lv_obj_send_event(ui.settings_button,LV_EVENT_CLICKED,NULL);lv_obj_send_event(ui.language_button,LV_EVENT_CLICKED,NULL);
 assert(strstr(lv_label_get_text(ui.footer_label),"RAW")&&!strstr(lv_label_get_text(ui.footer_label),"PASSBAND"));
 lv_obj_update_layout(lv_screen_active());labels(lv_screen_active(),false);
 lv_deinit();puts("HIGHSPEED_25M_UI_PASS: 18 windows/six rates, RAW4/CIC5, legacy conversions, frozen metadata, rejection, both slopes, cursor units, fonts/layout");return 0;
}
