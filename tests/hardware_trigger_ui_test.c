#include "scope_ui.h"
#include "scope_trigger.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static uint32_t buffer[1024*60],record[4096],original[4096];
static const unsigned precount[]={64,126,251,626,126,251,626,126,251,626,126,251,626};
static const double spans[]={312.5,625,1250,3125,625,1250,3125,625,1250,3125,625,1250,3125};
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *p){(void)a;(void)p;lv_display_flush_ready(d);}
static void adjust(ScopeUI *ui,unsigned target){
 while(ui->timebase_index<(int)target)lv_obj_send_event(ui->adjust_buttons[1],LV_EVENT_CLICKED,NULL);
 while(ui->timebase_index>(int)target)lv_obj_send_event(ui->adjust_buttons[0],LV_EVENT_CLICKED,NULL);
}
static ScopeRecordInfo info_for(const ScopeUI *ui){
 ScopeCapturePlan p;assert(!scope_acquisition_plan((unsigned)ui->timebase_index,&p));
 return (ScopeRecordInfo){ui->capture_epoch,p.rate_index,p.conversion_hz,SCOPE_FILTER_CIC3};
}
static void make_record(unsigned channel,bool rising,unsigned after){
 for(unsigned i=0;i<4096;i++)record[i]=512u|(512u<<16);
 /* Several previous cycles are present. They must not replace the hardware
    edge, even when a software search could find an earlier eligible edge. */
 unsigned shift=channel*16;
 for(unsigned i=0;i<4096;i++){
  unsigned c=i%64<32?540:585;
  record[i]=(record[i]&~(1023u<<shift))|(c<<shift);
 }
 unsigned before=rising?562:564,next=rising?564:562;
 record[after-1]=(record[after-1]&~(1023u<<shift))|(before<<shift);
 record[after]=(record[after]&~(1023u<<shift))|(next<<shift);
 memcpy(original,record,sizeof(record));
}
static void reject_without_mutation(ScopeUI *ui,unsigned rate,unsigned id,const ScopeRecordInfo *info,const ScopeHardwareTrigger *trigger){
 unsigned seen=ui->adc_last_seen_id,shown=ui->adc_id;const uint32_t *held=ui->adc_record;
 ScopeRecordInfo held_info=ui->adc_info;ScopeAdcMetrics m[2]={{0}};
 assert(!scope_ui_set_hardware_adc_record(ui,record,4096,rate,id,m,info,trigger));
 assert(ui->adc_last_seen_id==seen&&ui->adc_id==shown&&ui->adc_record==held&&!memcmp(&held_info,&ui->adc_info,sizeof(held_info)));
}
int main(void){
 lv_init();lv_display_t *d=lv_display_create(1024,600);lv_display_set_color_format(d,LV_COLOR_FORMAT_XRGB8888);
 lv_display_set_buffers(d,buffer,NULL,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(d,flush);
 ScopeUI ui;scope_ui_create(&ui);lv_obj_update_layout(lv_screen_active());
 ScopeHardwareConfig cfg;assert(scope_ui_hardware_config(&ui,&cfg)<0);
 scope_ui_enable_adaptive_sampling(&ui,true);scope_ui_enable_hardware_trigger(&ui,true);
 assert(ui.hardware_trigger&&scope_ui_hardware_config(&ui,&cfg)==0);
 assert(scope_ui_hardware_config(NULL,&cfg)<0&&scope_ui_hardware_config(&ui,NULL)<0);
 ScopeAdcMetrics metrics[2]={{1,.6,1000,true},{1,.6,1000,true}};unsigned id=1;
 scope_ui_set_dual_display(&ui,true);
 for(unsigned time=0;time<13;time++)for(unsigned channel=0;channel<2;channel++)for(unsigned rise=0;rise<2;rise++){
  adjust(&ui,time);scope_ui_select_trigger_source(&ui,channel);scope_ui_configure_trigger(&ui,false,rise!=0,500);
  assert(scope_ui_hardware_config(&ui,&cfg)==0);
  assert(cfg.control==(1u|(channel<<1)|(rise?4u:0u))&&cfg.pre_samples==precount[time]&&cfg.hysteresis_codes==10);
  assert(cfg.level_q8==144038u); /* round(562.65*256), shared PS/PL threshold. */
  ScopeRecordInfo info=info_for(&ui);ScopeCapturePlan plan;assert(!scope_acquisition_plan(time,&plan));
  make_record(channel,rise!=0,cfg.pre_samples);
  ScopeHardwareTrigger trigger={true,false,cfg.pre_samples};
  assert(scope_ui_set_hardware_adc_record(&ui,record,4096,plan.rate_hz,id++,metrics,&info,&trigger));
  double before=rise?562:564,after=rise?564:562;
  double cross=cfg.pre_samples-1+(cfg.level_q8/256.0-before)/(after-before);
  assert(ui.adc_triggered&&!ui.adc_waiting&&fabs(ui.adc_trigger_crossing-cross)<1e-12);
  assert(fabs(ui.adc_display_start-(cross-.2*spans[time]))<1e-12);
  assert(ui.samples[SCOPE_POINTS-1]!=LV_CHART_POINT_NONE&&ui.secondary_samples[SCOPE_POINTS-1]!=LV_CHART_POINT_NONE);
  assert(!memcmp(original,record,sizeof(record)));
  uint32_t epoch=ui.capture_epoch;double start=ui.adc_display_start;
  scope_ui_select_source(&ui,1-ui.adc_channel);scope_ui_configure_channel(&ui,1,3,500);scope_ui_set_cursors(&ui,true);
  assert(ui.capture_epoch==epoch&&ui.adc_display_start==start&&ui.adc_trigger_crossing==cross);
 }
 /* AUTO is explicit metadata, never a software trigger search. */
 ScopeRecordInfo info=info_for(&ui);ScopeCapturePlan plan;assert(!scope_acquisition_plan((unsigned)ui.timebase_index,&plan));
 ScopeHardwareTrigger forced={false,true,0};
 assert(scope_ui_set_hardware_adc_record(&ui,record,4096,plan.rate_hz,id++,metrics,&info,&forced));
 assert(!ui.adc_triggered&&ui.adc_display_start==0&&ui.adc_trigger_crossing==0);
 unsigned malformed_id=id+100;
 ScopeHardwareTrigger bad={false,false,0};reject_without_mutation(&ui,plan.rate_hz,malformed_id,&info,&bad);
 bad=(ScopeHardwareTrigger){true,true,cfg.pre_samples};reject_without_mutation(&ui,plan.rate_hz,malformed_id,&info,&bad);
 bad=(ScopeHardwareTrigger){true,false,cfg.pre_samples+1};reject_without_mutation(&ui,plan.rate_hz,malformed_id,&info,&bad);
 bad=(ScopeHardwareTrigger){false,true,1};reject_without_mutation(&ui,plan.rate_hz,malformed_id,&info,&bad);
 reject_without_mutation(&ui,plan.rate_hz,malformed_id,&info,NULL);
 ScopeHardwareTrigger matched={true,false,cfg.pre_samples};
 record[cfg.pre_samples]|=0x400u<<(ui.trigger_channel*16);reject_without_mutation(&ui,plan.rate_hz,malformed_id,&info,&matched);
 memcpy(record,original,sizeof(record));record[4095]|=0x80000000u;reject_without_mutation(&ui,plan.rate_hz,malformed_id,&info,&matched);
 memcpy(record,original,sizeof(record));
 ScopeRecordInfo stale=info;stale.capture_epoch--;reject_without_mutation(&ui,plan.rate_hz,malformed_id,&stale,&matched);
 stale=info;stale.rate_index=0;reject_without_mutation(&ui,plan.rate_hz,malformed_id,&stale,&matched);
 reject_without_mutation(&ui,plan.rate_hz*10,malformed_id,&info,&matched);
 assert(!scope_ui_set_adc_record_info(&ui,record,4096,plan.rate_hz,malformed_id,metrics,&info));
 assert(scope_ui_configure_trigger(&ui,true,true,500));info=info_for(&ui);
 reject_without_mutation(&ui,plan.rate_hz,malformed_id,&info,&forced);
 assert(scope_ui_arm_single(&ui));info=info_for(&ui);assert(scope_ui_hardware_config(&ui,&cfg)==0&&(cfg.control&8));
 reject_without_mutation(&ui,plan.rate_hz,malformed_id,&info,&forced);
 assert(ui.single_armed&&ui.model.running);
 make_record(ui.trigger_channel,true,cfg.pre_samples);matched.after_index=cfg.pre_samples;
 assert(scope_ui_set_hardware_adc_record(&ui,record,4096,plan.rate_hz,id++,metrics,&info,&matched));
 assert(ui.single_complete&&!ui.single_armed&&!ui.model.running);
 unsigned frozen_id=ui.adc_id;uint32_t epoch=ui.capture_epoch;double frozen_start=ui.adc_display_start;
 scope_ui_select_source(&ui,1-ui.adc_channel);scope_ui_set_cursors(&ui,false);scope_ui_set_dual_display(&ui,false);
 assert(ui.single_complete&&ui.adc_id==frozen_id&&ui.capture_epoch==epoch&&ui.adc_display_start==frozen_start);
 reject_without_mutation(&ui,plan.rate_hz,malformed_id,&info,&matched);
 scope_ui_set_running(&ui,true);scope_ui_configure_trigger(&ui,false,true,-5000);assert(!scope_ui_hardware_config(&ui,&cfg)&&cfg.level_q8==0);
 scope_ui_configure_trigger(&ui,false,false,5000);assert(!scope_ui_hardware_config(&ui,&cfg)&&cfg.level_q8==261888);
 ui.timebase_index=13;assert(scope_ui_hardware_config(&ui,&cfg)<0);ui.timebase_index=12;
 scope_ui_enable_hardware_trigger(&ui,false);assert(!ui.hardware_trigger&&scope_ui_hardware_config(&ui,&cfg)<0);
 lv_deinit();puts("HARDWARE_TRIGGER_UI_PASS: 13 rates/windows, both sources/slopes, exact named edge, AUTO/NORMAL/single metadata, malformed/frozen protection");return 0;
}
