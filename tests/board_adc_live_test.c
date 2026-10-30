#include "board_adc_live.h"
#include <windows.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static uint32_t regs[0x8000/4],now_ms;
static unsigned arms,resets,configs,deliveries,failures,enable_calls,hardware_enable_calls,conversion_calls;
static unsigned write_order,reset_order,config_order,arm_order;
static unsigned trigger_writes,trigger_order;
static unsigned hold_reset,hold_config,reject_record;
static unsigned analyzed_rate;
static const unsigned rates[]={625000,62500,6250,625,50000000,6250000};
static ScopeUI ui;
static ScopeHardwareConfig requested_trigger;
static ScopeHardwareTrigger delivered_trigger;
static ScopeAdcMetrics delivered_metrics[2];
static char status_line[1024];
uint32_t board_millis(void){return now_ms;}
void board_delay(unsigned ms){now_ms+=ms;}
void board_log(const char *fmt,...){
 char line[1024];va_list args;va_start(args,fmt);int n=vsnprintf(line,sizeof line,fmt,args);va_end(args);
 assert(n>=0&&(unsigned)n<sizeof line);if(strstr(fmt,"ADC_LIVE_FAIL"))failures++;
 if(strstr(fmt,"ADC_LIVE_STATUS"))memcpy(status_line,line,(unsigned)n+1);
}
void Xil_DCacheFlushRange(uintptr_t p,unsigned n){(void)p;assert(n==16384);}
void lv_obj_add_state(void *p,unsigned s){(void)p;(void)s;}
void lv_label_set_text(void *p,const char *s){(void)p;(void)s;}
const char *scope_ui_acquisition_error_text(const ScopeUI *p){(void)p;return "error";}
uint32_t screen_crc32(const uint8_t *p,unsigned n){(void)p;(void)n;return 0;}
bool scope_ui_set_conversion_hz(ScopeUI *p,unsigned conversion){
 assert(!p->adaptive_sampling&&!p->hardware_trigger);assert(conversion==50000000);conversion_calls++;
 if(p->conversion_hz!=conversion){
  assert(p->conversion_hz==5000000||p->conversion_hz==10000000||p->conversion_hz==25000000);
  p->timebase_index+=p->conversion_hz==5000000?8:3;
  p->conversion_hz=conversion;p->capture_epoch++;
 }
 return true;
}
int scope_ui_capture_plan(const ScopeUI *p,ScopeCapturePlan *out){
 return scope_acquisition_plan_for((unsigned)p->timebase_index,p->conversion_hz,out);
}
void scope_ui_enable_adaptive_sampling(ScopeUI *p,bool enabled){
 assert(p->conversion_hz==50000000&&conversion_calls==1);
 if(p->adaptive_sampling!=enabled)p->capture_epoch++;
 p->adaptive_sampling=enabled;enable_calls++;
}
void scope_ui_enable_hardware_trigger(ScopeUI *p,bool enabled){
 assert(p->adaptive_sampling);if(p->hardware_trigger!=enabled)p->capture_epoch++;
 p->hardware_trigger=enabled;hardware_enable_calls++;
}
int scope_ui_hardware_config(const ScopeUI *p,ScopeHardwareConfig *out){
 assert(p->hardware_trigger);*out=requested_trigger;return 0;
}
bool scope_ui_set_adc_record(ScopeUI *p,const uint32_t *data,unsigned count,unsigned rate,unsigned id,const ScopeAdcMetrics m[2]){
 (void)p;(void)data;(void)count;(void)rate;(void)id;(void)m;assert(!"legacy record API called");return false;
}
bool scope_ui_set_adc_record_info(ScopeUI *p,const uint32_t *data,unsigned count,unsigned rate,unsigned id,const ScopeAdcMetrics m[2],const ScopeRecordInfo *info){
 (void)p;(void)data;(void)count;(void)rate;(void)id;(void)m;(void)info;assert(!"software-trigger record API called");return false;
}
bool scope_ui_set_hardware_adc_record(ScopeUI *p,const uint32_t *data,unsigned count,unsigned rate,unsigned id,const ScopeAdcMetrics m[2],const ScopeRecordInfo *info,const ScopeHardwareTrigger *trigger){
 assert(p->hardware_trigger);assert(trigger->matched!=trigger->auto_forced);
 assert(trigger->after_index==(trigger->matched?requested_trigger.pre_samples:0));delivered_trigger=*trigger;
 memcpy(delivered_metrics,m,sizeof delivered_metrics);assert(count==4096);assert(p->adaptive_sampling);assert(info->capture_epoch==p->capture_epoch);
 assert(info->conversion_hz==50000000&&info->filter_kind==(info->rate_index==4?0u:3u));assert(rate==rates[info->rate_index]);
 analyzed_rate=rate;deliveries++;if(reject_record)return false;
 p->adc_record=data;p->adc_rate=rate;p->adc_id=id;p->adc_info=*info;return true;
}
uint32_t Xil_In32(uintptr_t address){assert(address>=0x43c10000u&&address<0x43c18000u);return regs[(address-0x43c10000u)/4];}
void Xil_Out32(uintptr_t address,uint32_t value){
 unsigned off=(unsigned)(address-0x43c10000u);write_order++;
 if(off==8&&value==2){resets++;reset_order=write_order;regs[3]=hold_reset?16:0;return;}
 if(off==0x30){assert(!(regs[3]&17));assert(!(regs[0x34/4]&1));assert(value<6);configs++;config_order=write_order;regs[off/4]=value;
  regs[0x34/4]=hold_config?(regs[0x34/4]|1):(value<<8);
  if(!hold_config){regs[0x24/4]=rates[value];regs[0x3c/4]=value==4?0:3;}return;}
 if(off>=0x40&&off<=0x4c){
  assert(!(regs[3]&17));assert(!(regs[0x34/4]&1));assert(off%4==0);regs[off/4]=value;
  trigger_writes++;trigger_order=write_order;return;
 }
 if(off==8&&value==1){
  assert(!(regs[3]&17));assert(!(regs[0x34/4]&1));assert(trigger_writes>=4);
  assert(regs[0x40/4]==requested_trigger.control&&regs[0x44/4]==requested_trigger.level_q8);
  assert(regs[0x48/4]==requested_trigger.pre_samples&&regs[0x4c/4]==requested_trigger.hysteresis_codes);
  arms++;arm_order=write_order;regs[3]=1;regs[0x54/4]=0;regs[0x58/4]=0;regs[0x64/4]=regs[0x68/4]=0;return;
 }
 assert(!"unexpected MMIO write");
}
static void fresh(unsigned timebase){
 memset(regs,0,sizeof regs);memset(&ui,0,sizeof ui);now_ms=100;
 arms=resets=configs=deliveries=failures=enable_calls=hardware_enable_calls=conversion_calls=0;write_order=reset_order=config_order=arm_order=0;
 trigger_writes=trigger_order=0;requested_trigger=(ScopeHardwareConfig){7,144038,64,2};
 delivered_trigger=(ScopeHardwareTrigger){false,false,0};
 hold_reset=hold_config=reject_record=analyzed_rate=0;status_line[0]='\0';
 ui.model.running=true;ui.capture_epoch=1;ui.timebase_index=(int)timebase;ui.conversion_hz=5000000;
 regs[0]=0x53414443;regs[1]=0x70000;regs[0x10/4]=4096;regs[0x14/4]=16384;
 regs[0x24/4]=625000;regs[0x28/4]=1;regs[0x2c/4]=50000000;regs[0x3c/4]=3;
 regs[0x1c/4]=10000000;for(unsigned i=0;i<4096;i++)regs[0x4000/4+i]=512u|(512u<<16);
}
static void poll_many(unsigned n){while(n--)board_adc_live_poll(&ui);}
static void fresh_raw(unsigned timebase){fresh(1);ui.conversion_hz=50000000;ui.timebase_index=(int)timebase;}
static void reach_arm(void){for(unsigned i=0;i<12&&!arms;i++)board_adc_live_poll(&ui);assert(arms==1&&failures==0);}
static void finish_record(void){
 regs[3]=0x82;regs[0x18/4]++;regs[0x54/4]=1;regs[0x58/4]=requested_trigger.pre_samples;
 regs[0x64/4]=21;regs[0x68/4]=8192;board_adc_live_poll(&ui);
}
static void test_initialization(void){
 fresh(1);regs[1]=0x20000;assert(board_adc_live_init()==-1);
 fresh(1);regs[1]=0x30000;assert(board_adc_live_init()==-1);
 fresh(1);regs[1]=0x40000;regs[0x2c/4]=5000000;assert(board_adc_live_init()==-1);
 fresh(1);regs[1]=0x50000;regs[0x2c/4]=10000000;assert(board_adc_live_init()==-1);
 fresh(1);regs[1]=0x50000;assert(board_adc_live_init()==-1);
 fresh(1);regs[0x2c/4]=5000000;assert(board_adc_live_init()==-1);
 fresh(1);regs[0x2c/4]=10000000;assert(board_adc_live_init()==-1);
 fresh(1);regs[1]=0x60000;assert(board_adc_live_init()==-1);
 fresh(1);regs[0x2c/4]=25000000;assert(board_adc_live_init()==-1);
 fresh(1);assert(board_adc_live_init()==0);reach_arm();
 assert(conversion_calls==1&&ui.timebase_index==9&&enable_calls==1&&hardware_enable_calls==1&&configs==0&&reset_order<trigger_order&&trigger_order<arm_order&&trigger_writes==4);
 fresh(2);assert(board_adc_live_init()==0);reach_arm();assert(ui.timebase_index==10&&ui.adc_record==NULL);
 fresh(1);regs[0x3c/4]=0;assert(board_adc_live_init()==-1);
 fresh(1);regs[0x24/4]=625;assert(board_adc_live_init()==-1);
}
static void test_all_rates_and_deadlines(void){
 const unsigned timebases[]={1,4,7,10};
 for(unsigned i=0;i<4;i++){
  fresh(timebases[i]);assert(board_adc_live_init()==0);reach_arm();
  assert(configs==(i!=0));if(i)assert(reset_order<config_order&&config_order<arm_order);
  assert(config_order<trigger_order&&trigger_order<arm_order);
  ScopeCapturePlan plan;assert(scope_acquisition_plan(timebases[i],&plan)==0);
  now_ms+=plan.timeout_ms-1;board_adc_live_poll(&ui);assert(failures==0&&ui.model.running);
  finish_record();assert(deliveries==1&&ui.adc_rate==rates[i]&&analyzed_rate==rates[i]);
  assert(ui.adc_info.capture_epoch==ui.capture_epoch&&enable_calls==1);
  char expected[80];snprintf(expected,sizeof expected," rate=%u ",rates[i]);
  assert(strstr(status_line,expected));assert(strstr(status_line,"filter_kind=3"));
  board_adc_live_poll(&ui);now_ms+=plan.timeout_ms+1;board_adc_live_poll(&ui);
  assert(failures==1&&!ui.model.running);
 }
}
static void test_config_wait_and_timeout(void){
 fresh(10);hold_config=1;assert(board_adc_live_init()==0);poll_many(5);assert(configs==1&&arms==0);
 now_ms+=99;poll_many(1);assert(failures==0&&arms==0);
 regs[0x34/4]=3u<<8;regs[0x24/4]=625;poll_many(2);assert(arms==1&&failures==0);
 fresh(10);hold_config=1;assert(board_adc_live_init()==0);poll_many(5);now_ms+=101;poll_many(1);assert(failures==1&&arms==0);
 fresh(10);hold_config=1;assert(board_adc_live_init()==0);poll_many(5);regs[0x34/4]=3u<<8;regs[0x24/4]=6250;
 poll_many(2);assert(failures==1&&arms==0);
}
static void test_cancel_and_frozen_buffer(void){
 fresh(1);assert(board_adc_live_init()==0);reach_arm();finish_record();assert(deliveries==1);
 const uint32_t *frozen=ui.adc_record;uint32_t original=frozen[0];board_adc_live_poll(&ui);
 regs[3]=0x82;regs[0x18/4]++;ui.model.running=false;ui.capture_epoch++;
 board_adc_live_poll(&ui);assert(deliveries==1&&resets==2&&ui.adc_record==frozen);poll_many(2);
 now_ms+=10000;poll_many(2);assert(failures==0&&ui.adc_record==frozen);
 ui.model.running=true;ui.capture_epoch++;ui.timebase_index=18;unsigned before=arms;poll_many(8);
 assert(arms==before+1&&configs==1);finish_record();assert(deliveries==2&&ui.adc_record!=frozen&&frozen[0]==original);
 frozen=ui.adc_record;reject_record=1;board_adc_live_poll(&ui);finish_record();
 assert(ui.adc_record==frozen&&frozen[0]==original);assert(failures==0);
}
static void test_tick_wrap_and_config_cancel(void){
 fresh(10);assert(board_adc_live_init()==0);now_ms=UINT32_MAX-100;reach_arm();now_ms+=6500;
 board_adc_live_poll(&ui);assert(failures==0);finish_record();assert(deliveries==1);
 fresh(10);hold_config=1;assert(board_adc_live_init()==0);poll_many(5);assert(configs==1&&arms==0);
 ui.timebase_index=9;ui.capture_epoch++;board_adc_live_poll(&ui);assert(resets==2);
 poll_many(2);assert(arms==0&&configs==1&&failures==0);
 regs[0x34/4]=3u<<8;regs[0x24/4]=625;hold_config=0;poll_many(8);
 assert(configs==2&&arms==1&&regs[0x24/4]==625000&&failures==0);
 fresh(10);hold_reset=1;assert(board_adc_live_init()==0);now_ms=UINT32_MAX-50;poll_many(1);
 now_ms+=99;poll_many(1);assert(failures==0);regs[3]=0;hold_reset=0;hold_config=1;poll_many(3);
 assert(configs==1&&arms==0);now_ms+=99;poll_many(1);assert(failures==0);
 regs[0x34/4]=3u<<8;regs[0x24/4]=625;poll_many(1);assert(arms==1);
}
static void test_stale_id_and_reset_timeout(void){
 fresh(1);assert(board_adc_live_init()==0);reach_arm();regs[3]=0x82;regs[0x18/4]=7;poll_many(1);
 assert(failures==1&&deliveries==0);
 fresh(1);hold_reset=1;assert(board_adc_live_init()==0);poll_many(2);now_ms+=101;poll_many(1);assert(failures==1&&arms==0);
}
static void test_trigger_config_and_metadata(void){
 for(unsigned channel=0;channel<2;channel++)for(unsigned rise=0;rise<2;rise++)for(unsigned wait=0;wait<2;wait++){
  fresh(1);requested_trigger.control=1u|(channel<<1)|(rise<<2)|(wait<<3);
  assert(board_adc_live_init()==0);reach_arm();finish_record();assert(deliveries==1&&delivered_trigger.matched);
  assert(strstr(status_line," hw_matched=1 ")&&strstr(status_line," hw_auto=0 "));
  assert(strstr(status_line," hw_start=21 ")&&strstr(status_line," hw_seen=8192"));
  board_adc_live_poll(&ui);assert(arms==2&&trigger_writes==4);
  board_adc_live_status();assert(strstr(status_line," hw_matched=1 ")&&strstr(status_line," hw_start=21 "));
  ui.capture_epoch++;requested_trigger.pre_samples=626;poll_many(4);assert(trigger_writes==8);
 }
 fresh(10);assert(board_adc_live_init()==0);reach_arm();regs[3]=0x82;regs[0x18/4]++;
 regs[0x54/4]=2;regs[0x58/4]=0;regs[0x64/4]=0;regs[0x68/4]=4096;
 board_adc_live_poll(&ui);assert(deliveries==1&&delivered_trigger.auto_forced&&!delivered_trigger.matched);
 assert(strstr(status_line," hw_matched=0 ")&&strstr(status_line," hw_auto=1 "));
}
static void test_progress_wait_and_stall(void){
 const unsigned timebases[]={1,4,7,10};
 for(unsigned i=0;i<4;i++){
  fresh(timebases[i]);requested_trigger.control|=8;assert(board_adc_live_init()==0);reach_arm();
  ScopeCapturePlan plan;assert(scope_acquisition_plan(timebases[i],&plan)==0);regs[0x54/4]=256;
  for(unsigned n=0;n<20;n++){
   now_ms+=plan.timeout_ms-1;regs[0x5c/4]++;board_adc_live_poll(&ui);
   assert(failures==0&&ui.model.running&&deliveries==0);
  }
  board_adc_live_status();assert(strstr(status_line," hw_waiting=1 "));
  finish_record();assert(deliveries==1&&failures==0);
  board_adc_live_poll(&ui);now_ms+=plan.timeout_ms;board_adc_live_poll(&ui);assert(failures==1);
 }
 /* AUTO can match late and then collect post samples; the progress rule
    applies there too, even when the live waiting flag is already clear. */
 fresh(1);assert(board_adc_live_init()==0);reach_arm();
 for(unsigned i=0;i<30;i++){now_ms+=100;regs[0x5c/4]++;board_adc_live_poll(&ui);assert(failures==0);}
 finish_record();assert(deliveries==1);
 fresh(1);regs[0x5c/4]=UINT32_MAX;assert(board_adc_live_init()==0);now_ms=UINT32_MAX-40;reach_arm();
 now_ms+=106;regs[0x5c/4]=0;board_adc_live_poll(&ui);assert(failures==0);
 now_ms+=106;board_adc_live_poll(&ui);assert(failures==0);
 now_ms++;board_adc_live_poll(&ui);assert(failures==1);
}
static void test_bad_trigger_metadata(void){
 for(unsigned bad=0;bad<7;bad++){
  fresh(1);assert(board_adc_live_init()==0);reach_arm();regs[3]=0x82;regs[0x18/4]++;
  regs[0x54/4]=1;regs[0x58/4]=requested_trigger.pre_samples;regs[0x64/4]=0;regs[0x68/4]=4096;
  if(bad==0)regs[0x54/4]=0;
  if(bad==1)regs[0x54/4]=3;
  if(bad==2)regs[0x58/4]++;
  if(bad==3)regs[0x64/4]=4096;
  if(bad==4)regs[0x68/4]=4095;
  if(bad==5){regs[0x54/4]=2;regs[0x58/4]=1;}
  if(bad==6){ui.capture_epoch++;requested_trigger.control|=8;poll_many(4);regs[3]=0x82;regs[0x18/4]++;regs[0x54/4]=2;regs[0x58/4]=0;regs[0x68/4]=4096;}
  board_adc_live_poll(&ui);assert(failures==1&&deliveries==0);
 }
}
static void test_bad_trigger_config(void){
 for(unsigned bad=0;bad<7;bad++){
  fresh(1);
  if(bad==0)requested_trigger.control=0;
  if(bad==1)requested_trigger.control=17;
  if(bad==2)requested_trigger.level_q8=1023u*256u+1;
  if(bad==3)requested_trigger.pre_samples=0;
  if(bad==4)requested_trigger.pre_samples=4095;
  if(bad==5)requested_trigger.hysteresis_codes=0;
  if(bad==6)requested_trigger.hysteresis_codes=1024;
  assert(board_adc_live_init()==0);poll_many(4);assert(failures==1&&arms==0);
 }
}
static void test_raw_profiles_and_metadata(void){
 for(unsigned timebase=0;timebase<6;timebase++){
  fresh_raw(timebase);assert(board_adc_live_init()==0);reach_arm();
  assert(configs==1&&regs[0x30/4]==4&&regs[0x34/4]==(4u<<8)&&regs[0x24/4]==50000000&&regs[0x3c/4]==0);
  ScopeCapturePlan plan;assert(scope_ui_capture_plan(&ui,&plan)==0&&plan.timeout_ms==101);
  assert(plan.rate_index==4&&plan.filter_kind==0&&plan.conversion_hz==50000000);
  now_ms+=100;board_adc_live_poll(&ui);assert(failures==0);finish_record();
  assert(deliveries==1&&ui.adc_rate==50000000&&ui.adc_info.filter_kind==0&&ui.adc_info.rate_index==4);
  assert(strstr(status_line," rate_index=4 ")&&strstr(status_line," filter_kind=0 "));
  assert(strstr(status_line," conversion_hz=50000000 ")&&strstr(status_line," record_filter=0 "));
  assert(strstr(status_line," display_filter=0 ")&&strstr(status_line,"display_conversion_hz=50000000"));
  board_adc_live_poll(&ui);now_ms+=101;board_adc_live_poll(&ui);assert(failures==1);
 }
 fresh_raw(0);regs[0x34/4]=4u<<8;regs[0x24/4]=50000000;regs[0x3c/4]=0;
 assert(board_adc_live_init()==0);reach_arm();assert(configs==0);finish_record();assert(deliveries==1);
}
static void test_raw_filter_handshake_rejections(void){
 for(unsigned code=4;code<8;code++){
  fresh_raw(0);regs[0x34/4]=code<<8;regs[0x24/4]=50000000;regs[0x3c/4]=3;
  assert(board_adc_live_init()==-1);
 }
 fresh_raw(0);hold_config=1;assert(board_adc_live_init()==0);poll_many(5);
 assert(configs==1&&arms==0&&regs[0x30/4]==4);now_ms+=99;poll_many(1);assert(failures==0);
 regs[0x34/4]=4u<<8;regs[0x24/4]=50000000;regs[0x3c/4]=0;poll_many(1);assert(arms==1);
 fresh_raw(0);hold_config=1;assert(board_adc_live_init()==0);poll_many(5);
 regs[0x34/4]=4u<<8;regs[0x24/4]=50000000;regs[0x3c/4]=3;poll_many(1);assert(failures==1&&arms==0);
 fresh_raw(0);assert(board_adc_live_init()==0);reach_arm();regs[0x3c/4]=3;finish_record();assert(failures==1&&deliveries==0);
 fresh(1);assert(board_adc_live_init()==0);reach_arm();regs[0x3c/4]=0;finish_record();assert(failures==1&&deliveries==0);
 fresh(1);assert(board_adc_live_init()==0);poll_many(2);regs[0x34/4]=6u<<8;
 poll_many(1);assert(failures==1&&arms==0&&configs==0);
 fresh_raw(0);assert(board_adc_live_init()==0);poll_many(2);regs[0x3c/4]=0;
 poll_many(1);assert(failures==1&&arms==0&&configs==0);
}
static void test_raw_cic_frozen_transitions(void){
 fresh(1);assert(board_adc_live_init()==0);reach_arm();finish_record();
 const uint32_t *cic=ui.adc_record;uint32_t original=cic[0];unsigned cic_id=ui.adc_id;
 ui.model.running=false;ui.timebase_index=0;ui.capture_epoch++;poll_many(3);
 assert(ui.adc_record==cic&&ui.adc_info.filter_kind==3&&ui.adc_rate==625000);
 ui.model.running=true;ui.capture_epoch++;unsigned old_arms=arms;poll_many(5);
 assert(arms==old_arms+1&&regs[0x3c/4]==0&&ui.adc_record==cic&&ui.adc_id==cic_id&&cic[0]==original);
 board_adc_live_status();assert(strstr(status_line," record_filter=3 ")&&strstr(status_line," display_filter=3 ")&&strstr(status_line," filter_kind=0 "));
 finish_record();assert(ui.adc_record!=cic&&ui.adc_info.filter_kind==0&&ui.adc_rate==50000000&&cic[0]==original);
 const uint32_t *raw=ui.adc_record;unsigned raw_id=ui.adc_id;original=raw[0];
 ui.timebase_index=12;ui.capture_epoch++;poll_many(5);
 assert(regs[0x24/4]==62500&&regs[0x3c/4]==3&&ui.adc_record==raw&&ui.adc_id==raw_id&&ui.adc_info.filter_kind==0);
 board_adc_live_status();assert(strstr(status_line," record_filter=0 ")&&strstr(status_line," display_filter=0 ")&&strstr(status_line," filter_kind=3 "));
 finish_record();assert(ui.adc_record!=raw&&ui.adc_info.filter_kind==3&&ui.adc_rate==62500&&raw[0]==original);
}
static void test_raw_progress_and_invalid_ui_mode(void){
 fresh_raw(3);requested_trigger.control|=8;assert(board_adc_live_init()==0);reach_arm();
 for(unsigned i=0;i<50;i++){now_ms+=100;regs[0x5c/4]++;board_adc_live_poll(&ui);assert(failures==0&&deliveries==0);}
 finish_record();assert(deliveries==1);
 fresh_raw(21);assert(board_adc_live_init()==0);poll_many(4);assert(failures==1&&arms==0);
 fresh(1);assert(board_adc_live_init()==0);reach_arm();ui.conversion_hz=5000000;ui.capture_epoch++;
 board_adc_live_poll(&ui);assert(failures==1);
}
static void test_50m_all_profile_rates(void){
 static const unsigned expected[]={4,4,4,4,4,4,5,5,5,0,0,0,1,1,1,2,2,2,3,3,3};
 for(unsigned timebase=0;timebase<21;timebase++){
  fresh_raw(timebase);assert(board_adc_live_init()==0);reach_arm();
  unsigned code=expected[timebase];ScopeCapturePlan plan;assert(scope_ui_capture_plan(&ui,&plan)==0);
  assert(plan.rate_index==code&&plan.rate_hz==rates[code]&&plan.conversion_hz==50000000);
  assert(configs==(code!=0)&&regs[0x34/4]==(code<<8)&&regs[0x24/4]==rates[code]);
  assert(plan.filter_kind==(code==4?0u:3u)&&plan.timeout_ms==(4096000u+rates[code]-1u)/rates[code]+100u);
  for(unsigned n=0;n<4;n++){now_ms+=plan.timeout_ms-1;regs[0x5c/4]++;poll_many(1);assert(failures==0&&deliveries==0);}
  finish_record();assert(deliveries==1&&ui.adc_rate==rates[code]&&ui.adc_info.rate_index==code);
  assert(ui.adc_info.filter_kind==plan.filter_kind&&ui.adc_info.conversion_hz==50000000);
 }
 /* Older high-speed profiles gain three finer labels, preserving physical time. */
 fresh_raw(3);ui.conversion_hz=10000000;assert(board_adc_live_init()==0);reach_arm();
 assert(ui.timebase_index==6&&regs[0x30/4]==5);finish_record();assert(ui.adc_rate==6250000);
 for(unsigned old=0;old<2;old++){
  fresh_raw(7);ui.conversion_hz=old?25000000:10000000;assert(board_adc_live_init()==0);reach_arm();
  assert(ui.timebase_index==10&&ui.capture_epoch==4&&configs==0);finish_record();
  assert(ui.adc_rate==625000&&ui.adc_info.conversion_hz==50000000);
 }
}
static void test_intermediate_handshake_and_wrong_readback(void){
 fresh_raw(6);regs[0x34/4]=5u<<8;regs[0x24/4]=6250000;assert(board_adc_live_init()==0);
 reach_arm();assert(configs==0);finish_record();assert(deliveries==1&&ui.adc_info.rate_index==5&&ui.adc_info.filter_kind==3);
 for(unsigned bad=0;bad<5;bad++){
  fresh_raw(6);hold_config=1;assert(board_adc_live_init()==0);poll_many(5);assert(configs==1&&regs[0x30/4]==5&&arms==0);
  regs[0x34/4]=5u<<8;regs[0x24/4]=6250000;regs[0x3c/4]=3;
  if(bad==1)regs[0x24/4]=50000000;
  if(bad==2)regs[0x3c/4]=0;
  if(bad==3)regs[0x2c/4]=10000000;
  if(bad==4)regs[0x34/4]=6u<<8;
  if(bad==4)now_ms+=101;
  poll_many(1);
  if(bad)assert(failures==1&&arms==0);else{assert(failures==0&&arms==1);finish_record();assert(deliveries==1);}
 }
 fresh_raw(6);assert(board_adc_live_init()==0);reach_arm();regs[0x24/4]=50000000;finish_record();assert(failures==1&&deliveries==0);
 fresh_raw(6);assert(board_adc_live_init()==0);reach_arm();regs[0x3c/4]=0;finish_record();assert(failures==1&&deliveries==0);
}
static void test_fast_six_rate_switch_cancels_pending_records(void){
 static const unsigned indices[]={0,6,9,8,5,12,15,18,9,0,6,9};
 fresh(1);assert(board_adc_live_init()==0);reach_arm();finish_record();
 for(unsigned n=0;n<sizeof indices/sizeof indices[0];n++){
  const uint32_t *held=ui.adc_record;ScopeRecordInfo info=ui.adc_info;unsigned held_id=ui.adc_id,held_rate=ui.adc_rate;
  uint32_t first=held[0];unsigned before=deliveries;
  board_adc_live_poll(&ui); // Begin an old pending capture, then cancel it.
  ui.timebase_index=(int)indices[n];ui.capture_epoch++;board_adc_live_poll(&ui);
  assert(deliveries==before&&ui.adc_record==held&&ui.adc_id==held_id&&held[0]==first);
  assert(ui.adc_info.capture_epoch==info.capture_epoch&&ui.adc_info.filter_kind==info.filter_kind&&ui.adc_rate==held_rate);
  poll_many(5);assert(failures==0&&ui.adc_record==held&&held[0]==first);
  ScopeCapturePlan plan;assert(scope_ui_capture_plan(&ui,&plan)==0);
  finish_record();assert(deliveries==before+1&&ui.adc_record!=held&&ui.adc_rate==plan.rate_hz);
  assert(ui.adc_info.capture_epoch==ui.capture_epoch&&ui.adc_info.rate_index==plan.rate_index&&ui.adc_info.filter_kind==plan.filter_kind);
 }
}
static void test_all_six_rate_readback_rejections(void){
 static const unsigned indices[]={9,12,15,18,0,6};
 for(unsigned code=0;code<6;code++)for(unsigned bad=0;bad<4;bad++){
  fresh_raw(indices[code]);regs[0x34/4]=code<<8;regs[0x24/4]=rates[code];regs[0x3c/4]=code==4?0:3;
  if(bad==0)regs[0x24/4]++;
  if(bad==1)regs[0x3c/4]=code==4?3:0;
  if(bad==2)regs[0x2c/4]=25000000;
  if(bad==3)regs[0x34/4]=6u<<8;
  assert(board_adc_live_init()==-1);
  fresh_raw(indices[code]);assert(board_adc_live_init()==0);reach_arm();
  if(bad==0)regs[0x24/4]++;
  if(bad==1)regs[0x3c/4]=code==4?3:0;
  if(bad==2)regs[0x2c/4]=25000000;
  if(bad==3)regs[0x34/4]=6u<<8;
  finish_record();assert(failures==1&&deliveries==0);
  fresh_raw(indices[code]);hold_config=1;
  /* The initial applied config must differ so even code 0 needs an ACK. */
  if(code==0){regs[0x34/4]=4u<<8;regs[0x24/4]=50000000;regs[0x3c/4]=0;}
  assert(board_adc_live_init()==0);poll_many(5);assert(configs==1&&arms==0);
  regs[0x34/4]=code<<8;regs[0x24/4]=rates[code];regs[0x3c/4]=code==4?0:3;
  if(bad==0)regs[0x24/4]++;
  if(bad==1)regs[0x3c/4]=code==4?3:0;
  if(bad==2)regs[0x2c/4]=25000000;
  if(bad==3)regs[0x34/4]=6u<<8;
  now_ms+=101;poll_many(1);assert(failures==1&&arms==0);
 }
}
static void test_raw_intermediate_cic_frozen_metadata(void){
 static const unsigned indices[]={0,6,9,6,0};
 fresh_raw(9);assert(board_adc_live_init()==0);reach_arm();finish_record();
 for(unsigned n=0;n<sizeof indices/sizeof indices[0];n++){
  const uint32_t *held=ui.adc_record;ScopeRecordInfo saved=ui.adc_info;
  unsigned id=ui.adc_id,rate=ui.adc_rate,old_configs=configs;uint32_t first=held[0];
  ui.model.running=false;ui.timebase_index=(int)indices[n];ui.capture_epoch++;poll_many(3);
  assert(ui.adc_record==held&&ui.adc_id==id&&ui.adc_rate==rate&&held[0]==first&&configs==old_configs);
  assert(ui.adc_info.capture_epoch==saved.capture_epoch&&ui.adc_info.filter_kind==saved.filter_kind&&ui.adc_info.conversion_hz==saved.conversion_hz);
  ui.model.running=true;ui.capture_epoch++;poll_many(5);assert(failures==0&&ui.adc_record==held);
  ScopeCapturePlan plan;assert(scope_ui_capture_plan(&ui,&plan)==0);
  assert(regs[0x34/4]==(plan.rate_index<<8)&&regs[0x24/4]==plan.rate_hz&&regs[0x3c/4]==plan.filter_kind);
  finish_record();assert(ui.adc_record!=held&&held[0]==first&&ui.adc_rate==plan.rate_hz);
  assert(ui.adc_info.capture_epoch==ui.capture_epoch&&ui.adc_info.filter_kind==plan.filter_kind&&ui.adc_info.conversion_hz==50000000);
 }
}
static void test_raw_20ns_analysis(void){
 fresh_raw(0);assert(board_adc_live_init()==0);reach_arm();
 /* Four samples per cycle means 80 ns at 50 MSa/s; channel 2 is offset
    by one sample (20 ns), with an exact edge crossing between samples. */
 for(unsigned i=0;i<4096;i++){
  unsigned a=(i%4)<2?400:624,b=((i+1)%4)<2?400:624;
  regs[0x4000/4+i]=a|(b<<16);
 }
 finish_record();assert(deliveries==1&&ui.adc_rate==50000000);
 for(unsigned c=0;c<2;c++)assert(delivered_metrics[c].frequency_valid&&delivered_metrics[c].frequency_hz==12500000.0);
 assert(ui.adc_record[1]==(400u|(624u<<16))&&ui.adc_record[2]==(624u|(624u<<16)));
}
static void test_replay_like_pause_preserves_both_live_slots(void){
 static uint32_t replay[4096],copy[8192];
 fresh_raw(5);assert(board_adc_live_init()==0);reach_arm();finish_record();
 const uint32_t *live=ui.adc_record;ScopeRecordInfo live_info=ui.adc_info;
 unsigned shown=ui.adc_id,delivered=deliveries;
 board_adc_live_poll(&ui);unsigned armed=arms;
 /* A completed capture races entry into replay. Epoch cancellation must win
    before the poller reads it, even though the displayed pointer is now RAM. */
 regs[3]=0x82;regs[0x18/4]++;
 memcpy(copy,(const void*)(uintptr_t)0x13000000u,sizeof(copy));
 for(unsigned i=0;i<4096;i++)replay[i]=500u|(600u<<16);
 ui.adc_record=replay;ui.adc_rate=6250000;
 ui.adc_info=(ScopeRecordInfo){7,5,25000000,3};
 ui.timebase_index=6;ui.model.running=false;ui.capture_epoch++;
 poll_many(4);now_ms+=20000;poll_many(10);
 assert(failures==0&&arms==armed&&deliveries==delivered&&ui.adc_record==replay);
 assert(ui.adc_rate==6250000&&ui.adc_info.conversion_hz==25000000&&ui.conversion_hz==50000000);
 assert(!memcmp(copy,(const void*)(uintptr_t)0x13000000u,sizeof(copy)));
 ui.adc_record=live;ui.adc_rate=50000000;ui.adc_id=shown;ui.adc_info=live_info;
 ui.timebase_index=5;ui.model.running=true;ui.capture_epoch++;poll_many(5);
 assert(failures==0&&arms==armed+1&&ui.adc_record==live);
 finish_record();assert(deliveries==delivered+1&&ui.adc_record!=live&&ui.adc_rate==50000000);
 assert(!memcmp(live,copy+((uintptr_t)live==0x13000000u?0:4096),4096*4));
 for(unsigned i=0;i<4096;i++)assert(replay[i]==(500u|(600u<<16)));
}
int main(void){
 void *buffer=VirtualAlloc((void *)(uintptr_t)0x13000000u,32768,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
 assert(buffer==(void *)(uintptr_t)0x13000000u);
 test_initialization();test_all_rates_and_deadlines();test_config_wait_and_timeout();
 test_cancel_and_frozen_buffer();test_tick_wrap_and_config_cancel();test_stale_id_and_reset_timeout();
 test_trigger_config_and_metadata();test_progress_wait_and_stall();test_bad_trigger_metadata();test_bad_trigger_config();
 test_raw_profiles_and_metadata();test_raw_filter_handshake_rejections();test_raw_cic_frozen_transitions();test_raw_progress_and_invalid_ui_mode();
 test_50m_all_profile_rates();test_intermediate_handshake_and_wrong_readback();test_fast_six_rate_switch_cancels_pending_records();
 test_all_six_rate_readback_rejections();test_raw_intermediate_cic_frozen_metadata();test_raw_20ns_analysis();
 test_replay_like_pause_preserves_both_live_slots();
 VirtualFree(buffer,0,MEM_RELEASE);puts("board ADC poller: ABI7/50MHz/21 timebases/6 rates/RAW+CIC3/config/progress+stall/frozen-metadata/wrap PASS");return 0;
}
