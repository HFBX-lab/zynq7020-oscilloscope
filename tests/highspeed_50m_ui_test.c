#include "scope_ui.h"
#include "src/misc/lv_text_private.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static uint32_t buffer[1024*60],record[4096],framebuffer[1024*600];
static const double seconds[]={1e-7,2e-7,5e-7,1e-6,2e-6,5e-6,1e-5,2e-5,5e-5,1e-4,2e-4,5e-4,.001,.002,.005,.01,.02,.05,.1,.2,.5};
static const char *names[]={"100 ns","200 ns","500 ns","1 µs","2 µs","5 µs","10 µs","20 µs","50 µs","100 µs","200 µs","500 µs","1 ms","2 ms","5 ms","10 ms","20 ms","50 ms","100 ms","200 ms","500 ms"};
static const unsigned rates[]={50000000,50000000,50000000,50000000,50000000,50000000,6250000,6250000,6250000,625000,625000,625000,62500,62500,62500,6250,6250,6250,625,625,625};
static const unsigned indices[]={4,4,4,4,4,4,5,5,5,0,0,0,1,1,1,2,2,2,3,3,3};
static const unsigned pre_counts[]={11,21,51,101,201,501,126,251,626,126,251,626,126,251,626,126,251,626,126,251,626};
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *p){
 unsigned width=(unsigned)(a->x2-a->x1+1);
 for(int y=a->y1;y<=a->y2;y++)memcpy(framebuffer+y*1024+a->x1,p+(y-a->y1)*width*4,width*4);
 lv_display_flush_ready(d);
}
static void save(lv_display_t *d,const char *directory,const char *name){
 if(!directory)return;
 lv_refr_now(d);char path[512];snprintf(path,sizeof(path),"%s/%s.bgrx",directory,name);
 FILE *f=fopen(path,"wb");assert(f);assert(fwrite(framebuffer,1,sizeof(framebuffer),f)==sizeof(framebuffer));assert(!fclose(f));
}
static void adjust(ScopeUI *ui,unsigned target){
 unsigned steps=0;
 while(ui->timebase_index<(int)target&&steps++<25)lv_obj_send_event(ui->adjust_buttons[1],LV_EVENT_CLICKED,NULL);
 while(ui->timebase_index>(int)target&&steps++<50)lv_obj_send_event(ui->adjust_buttons[0],LV_EVENT_CLICKED,NULL);
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
static void make_record(unsigned after,bool rising){
 for(unsigned i=0;i<4096;i++)record[i]=512u|((i%1000<500?540u:585u)<<16);
 record[after-1]=512u|((rising?562u:564u)<<16);record[after]=512u|((rising?564u:562u)<<16);
}
static void reject(ScopeUI *ui,unsigned rate,unsigned id,const ScopeRecordInfo *info,const ScopeHardwareTrigger *edge){
 ScopeAdcMetrics m[2]={{0}};unsigned seen=ui->adc_last_seen_id,shown=ui->adc_id;ScopeRecordInfo held=ui->adc_info;const uint32_t *data=ui->adc_record;
 assert(!scope_ui_set_hardware_adc_record(ui,record,4096,rate,id,m,info,edge));
 assert(ui->adc_last_seen_id==seen&&ui->adc_id==shown&&ui->adc_record==data&&!memcmp(&held,&ui->adc_info,sizeof(held)));
}
static void old_profiles(void){
 const unsigned conversions[]={5000000,10000000,25000000};
 const unsigned base_rates[]={625000,62500,6250,625};
 for(unsigned c=0;c<3;c++){
  unsigned count=c?18:13;ScopeCapturePlan p;
  for(unsigned i=0;i<count;i++){
   assert(!scope_acquisition_plan_for(i,conversions[c],&p));
   unsigned idx;
   if(c==2)idx=i<4?4:i<6?5:i<9?0:i<12?1:i<15?2:3;
   else if(c==1&&i<5)idx=4;
   else{unsigned j=c?i-5:i;idx=j<4?0:j<7?1:j<10?2:3;}
   assert(p.rate_index==idx&&p.rate_hz==(idx==4?conversions[c]:idx==5?6250000:base_rates[idx]));
   assert(p.conversion_hz==conversions[c]&&p.count==4096&&p.filter_kind==(idx==4?0:3));
   assert(p.timeout_ms==(4096000u+p.rate_hz-1)/p.rate_hz+100);
  }
  assert(scope_acquisition_plan_for(count,conversions[c],&p)<0);
 }
}
int main(int argc,char **argv){
 const char *preview=argc>1?argv[1]:NULL;
 ScopeCapturePlan p;
 /* This assertion must fail against the accepted 25M implementation. */
 assert(scope_acquisition_plan_for(0,50000000,&p)==0);
 assert(SCOPE_TIMEBASE_COUNT==13&&SCOPE_HIGHSPEED_TIMEBASE_COUNT==18&&SCOPE_50M_TIMEBASE_COUNT==21);
 old_profiles();
 for(unsigned i=0;i<21;i++){
  assert(!scope_acquisition_plan_for(i,50000000,&p));
  assert(p.conversion_hz==50000000&&p.rate_hz==rates[i]&&p.rate_index==indices[i]&&p.count==4096);
  assert(p.filter_kind==(i<6?0:3)&&p.timeout_ms==(4096000u+rates[i]-1)/rates[i]+100);
  assert(10*seconds[i]*p.rate_hz<=3125.0000001);
 }
 assert(scope_acquisition_plan_for(21,50000000,&p)<0&&scope_acquisition_plan_for(0,40000000,&p)<0&&scope_acquisition_plan_for(0,50000000,NULL)<0);
 lv_init();lv_display_t *d=lv_display_create(1024,600);lv_display_set_color_format(d,LV_COLOR_FORMAT_XRGB8888);
 lv_display_set_buffers(d,buffer,NULL,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(d,flush);
 ScopeUI ui;scope_ui_create(&ui);lv_obj_update_layout(lv_screen_active());
 scope_ui_enable_adaptive_sampling(&ui,true);scope_ui_enable_hardware_trigger(&ui,true);scope_ui_select_trigger_source(&ui,1);
 scope_ui_set_dual_display(&ui,true);scope_ui_set_cursors(&ui,true);
 uint32_t epoch=ui.capture_epoch;assert(scope_ui_set_conversion_hz(&ui,50000000)&&ui.timebase_index==10&&ui.capture_epoch==epoch+1);
 epoch=ui.capture_epoch;assert(scope_ui_set_conversion_hz(&ui,50000000)&&ui.capture_epoch==epoch);
 assert(!scope_ui_set_conversion_hz(&ui,40000000)&&ui.capture_epoch==epoch);
 ScopeAdcMetrics metrics[2]={{0},{1,.6,50000,true}};unsigned id=1;
 lv_obj_send_event(ui.settings_button,LV_EVENT_CLICKED,NULL);
 for(unsigned lang=0;lang<2;lang++){
  for(unsigned i=0;i<21;i++){
   adjust(&ui,i);assert(!scope_ui_capture_plan(&ui,&p)&&p.rate_hz==rates[i]);
   for(unsigned rising=0;rising<2;rising++){
    scope_ui_configure_trigger(&ui,false,rising!=0,500);ScopeHardwareConfig cfg;assert(!scope_ui_hardware_config(&ui,&cfg));
    assert(cfg.pre_samples==pre_counts[i]);make_record(cfg.pre_samples,rising!=0);ScopeHardwareTrigger edge={true,false,cfg.pre_samples};
    ScopeRecordInfo info={ui.capture_epoch,p.rate_index,p.conversion_hz,p.filter_kind};
    ScopeRecordInfo bad=info;bad.filter_kind=i<6?3:0;reject(&ui,p.rate_hz,id+100,&bad,&edge);
    bad=info;bad.conversion_hz=25000000;reject(&ui,p.rate_hz,id+100,&bad,&edge);
    bad=info;bad.capture_epoch--;reject(&ui,p.rate_hz,id+100,&bad,&edge);
    bad=info;bad.rate_index=i<6?5:4;reject(&ui,p.rate_hz,id+100,&bad,&edge);
    reject(&ui,p.rate_hz+1,id+100,&info,&edge);
    ScopeHardwareTrigger bad_edge=edge;bad_edge.after_index++;reject(&ui,p.rate_hz,id+100,&info,&bad_edge);
    assert(scope_ui_set_hardware_adc_record(&ui,record,4096,p.rate_hz,id++,metrics,&info,&edge));
    assert(ui.adc_triggered&&ui.adc_rate==p.rate_hz&&ui.adc_info.filter_kind==p.filter_kind);
    assert(ui.samples[SCOPE_POINTS-1]!=LV_CHART_POINT_NONE&&ui.secondary_samples[SCOPE_POINTS-1]!=LV_CHART_POINT_NONE);
    assert(fabs(ui.adc_trigger_crossing-ui.adc_display_start-.2*10*seconds[i]*p.rate_hz)<1e-8);
    assert(fabs(ui.cursor_result.delta_s-5*seconds[i])<1e-10);
   }
   assert(strstr(lv_label_get_text(ui.time_label),names[i]));
   if(i<6){assert(strstr(lv_label_get_text(ui.footer_label),"50 MSa/s")&&strstr(lv_label_get_text(ui.footer_label),lang?"RAW":"直采"));}
   else if(i<9){assert(strstr(lv_label_get_text(ui.footer_label),"6.25 MSa/s")&&strstr(lv_label_get_text(ui.footer_label),"0–625 kHz"));}
   lv_obj_update_layout(lv_screen_active());labels(lv_screen_active(),false);
   assert(lv_obj_get_y(ui.settings_note)+lv_obj_get_height(ui.settings_note)<=lv_obj_get_y(ui.close_button));
   if(preview&&(i==0||i==6||i==10||i==20)){
    char name[64];snprintf(name,sizeof(name),"%s-%u-settings",lang?"en":"zh",i);save(d,preview,name);
    lv_obj_send_event(ui.close_button,LV_EVENT_CLICKED,NULL);
    lv_obj_update_layout(lv_screen_active());labels(lv_screen_active(),false);
    snprintf(name,sizeof(name),"%s-%u-main",lang?"en":"zh",i);save(d,preview,name);
    lv_obj_send_event(ui.settings_button,LV_EVENT_CLICKED,NULL);
   }
  }
  if(!lang)lv_obj_send_event(ui.language_button,LV_EVENT_CLICKED,NULL);
 }
 /* Every profile change preserves physical time, clamping unsupported fast labels. */
 for(unsigned i=0;i<21;i++){
  adjust(&ui,i);epoch=ui.capture_epoch;
  assert(scope_ui_set_conversion_hz(&ui,25000000)&&ui.timebase_index==(int)(i<3?0:i-3)&&ui.capture_epoch==epoch+1);
  assert(scope_ui_set_conversion_hz(&ui,50000000)&&ui.timebase_index==(int)(i<3?3:i));
  adjust(&ui,i);assert(scope_ui_set_conversion_hz(&ui,10000000)&&ui.timebase_index==(int)(i<3?0:i-3));
  assert(scope_ui_set_conversion_hz(&ui,50000000)&&ui.timebase_index==(int)(i<3?3:i));
  adjust(&ui,i);assert(scope_ui_set_conversion_hz(&ui,5000000)&&ui.timebase_index==(int)(i<8?0:i-8));
  assert(scope_ui_set_conversion_hz(&ui,50000000)&&ui.timebase_index==(int)(i<8?8:i));
 }
 lv_obj_send_event(ui.close_button,LV_EVENT_CLICKED,NULL);
 /* Freeze both native and intermediate records, and retain their actual metadata on conversion changes. */
 for(unsigned test=0;test<2;test++){
  adjust(&ui,test?6:0);scope_ui_configure_trigger(&ui,false,true,500);scope_ui_move_cursors(&ui,250,750);
  assert(scope_ui_arm_single(&ui));assert(!scope_ui_capture_plan(&ui,&p));ScopeHardwareConfig cfg;assert(!scope_ui_hardware_config(&ui,&cfg));
  make_record(cfg.pre_samples,true);ScopeHardwareTrigger edge={true,false,cfg.pre_samples};ScopeRecordInfo info={ui.capture_epoch,p.rate_index,50000000,p.filter_kind};
  assert(scope_ui_set_hardware_adc_record(&ui,record,4096,p.rate_hz,id++,metrics,&info,&edge)&&ui.single_complete&&!ui.model.running);
  unsigned shown=ui.adc_id;ScopeRecordInfo held=ui.adc_info;epoch=ui.capture_epoch;
  reject(&ui,p.rate_hz,id+100,&info,&edge);
  assert(scope_ui_move_cursors(&ui,0,test?1:20));assert(ui.adc_id==shown&&ui.capture_epoch==epoch&&!memcmp(&held,&ui.adc_info,sizeof(held)));
  if(!test){assert(strstr(lv_label_get_text(ui.vpp_label),"+0.020 µs"));assert(fabs(ui.cursor_result.delta_s-2e-8)<1e-16);save(d,preview,"en-20ns-frozen");}
  assert(scope_ui_select_source(&ui,1)&&ui.capture_epoch==epoch&&ui.single_complete&&ui.adc_id==shown);
  assert(scope_ui_set_conversion_hz(&ui,25000000)&&ui.capture_epoch==epoch+1&&!ui.single_complete&&!ui.single_armed);
  assert(ui.adc_rate==p.rate_hz&&ui.adc_id==shown&&!memcmp(&held,&ui.adc_info,sizeof(held)));
  assert(strstr(lv_label_get_text(ui.footer_label),test?"6.25 MSa/s":"50 MSa/s"));
  assert(strstr(lv_label_get_text(ui.footer_label),test?"PASSBAND":"RAW"));
  assert(scope_ui_set_conversion_hz(&ui,50000000));scope_ui_set_running(&ui,true);
 }
 /* Measurement source preserves a live record; trigger source invalidates stale epochs while retaining single ownership. */
 adjust(&ui,0);scope_ui_configure_trigger(&ui,true,true,500);assert(scope_ui_arm_single(&ui));
 epoch=ui.capture_epoch;assert(scope_ui_select_source(&ui,0)&&ui.capture_epoch==epoch&&ui.single_armed);
 assert(scope_ui_select_trigger_source(&ui,0)&&ui.capture_epoch==epoch+1&&ui.single_armed&&ui.adc_waiting);
 assert(!scope_ui_capture_plan(&ui,&p));ScopeHardwareConfig cfg;assert(!scope_ui_hardware_config(&ui,&cfg));
 make_record(cfg.pre_samples,true);ScopeHardwareTrigger edge={true,false,cfg.pre_samples};ScopeRecordInfo info={epoch,4,50000000,0};
 reject(&ui,50000000,id+100,&info,&edge);info.capture_epoch=ui.capture_epoch;
 reject(&ui,50000000,id+100,&info,&edge); /* CH1 is flat: CH2's edge cannot satisfy CH1 trigger. */
 assert(scope_ui_select_trigger_source(&ui,1)&&ui.single_armed);info.capture_epoch=ui.capture_epoch;
 assert(scope_ui_set_hardware_adc_record(&ui,record,4096,50000000,id++,metrics,&info,&edge)&&ui.single_complete);
 scope_ui_set_running(&ui,true);assert(ui.model.running&&!ui.single_complete);
 lv_deinit();puts("HIGHSPEED_50M_UI_PASS: 21 windows/six rates, legacy profiles, all mappings, strict metadata, both slopes, frozen RAW/CIC/single/source, 20ns cursors, bilingual fonts/layout");return 0;
}
