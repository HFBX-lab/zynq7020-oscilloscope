#include "board_adc_live.h"
#include "record_stats.h"
#include "frame_source.h"
#include "board.h"
#include "xparameters.h"
#include "xil_io.h"
#include "xil_cache.h"
#define BASE 0x43c10000u
#define WORDS 4096u
#define CONVERSION_HZ 50000000u
#define CONTROL_TIMEOUT_MS 100u
static unsigned available,pending,last_id,old_id;
static uint32_t epoch_seen,reset_started;
static unsigned reset_waiting,config_waiting,epoch_valid,trigger_configured;
static ScopeUI *current_ui;
static uint32_t started,config_started,last_report,last_address,last_rate,last_filter;
static ScopeCapturePlan capture_plan;
static ScopeRecordInfo capture_info;
static ScopeHardwareConfig capture_trigger;
static ScopeHardwareTrigger last_trigger;
static uint32_t last_progress,last_ring_start,last_samples_seen;
static AdcLiveReport report;
static uint32_t rd(unsigned off){return Xil_In32(BASE+off);}
static void wr(unsigned off,uint32_t value){Xil_Out32(BASE+off,value);__asm__ volatile("dsb sy":::"memory");}
static unsigned valid_applied_config(void){
 static const unsigned rates[6]={625000u,62500u,6250u,625u,50000000u,6250000u};
 unsigned config=rd(0x34),index=(config>>8)&7u;
 return !(config&1u)&&index<6u&&rd(0x24)==rates[index]&&rd(0x2c)==CONVERSION_HZ&&rd(0x3c)==(index==4u?0u:3u);
}
int board_adc_live_init(void){
 available=pending=last_id=0;last_address=last_rate=last_filter=0;epoch_seen=0;
 reset_waiting=config_waiting=epoch_valid=trigger_configured=0;current_ui=NULL;report=(AdcLiveReport){0};
 last_trigger=(ScopeHardwareTrigger){false,false,0};last_progress=last_ring_start=last_samples_seen=0;
 if(rd(0)!=0x53414443u||rd(4)!=0x70000u||rd(0x10)!=WORDS||rd(0x14)!=WORDS*4||
    rd(0x2c)!=CONVERSION_HZ||(rd(0xc)&64u))goto fail;
 uint32_t now=board_millis();
 while((rd(0xc)&17u)||!(rd(0x28)&1u)||(rd(0x34)&1u)){
  if((uint32_t)(board_millis()-now)>=CONTROL_TIMEOUT_MS)goto fail;
  board_delay(1);
 }
 if(!valid_applied_config())goto fail;
 available=1;last_report=board_millis();
 board_log("ADC_LIVE_READY: EXTERNAL BANK33/J6 conversion_hz=%u record_hz=%u words=4096 ABI=00070000 filter_kind=%u volts=NOMINAL_UNCALIBRATED\r\n",
  CONVERSION_HZ,(unsigned)rd(0x24),(unsigned)rd(0x3c));return 0;
fail:board_log("ADC_LIVE_FAIL: initialization status=%08x ready=%u\r\n",(unsigned)rd(0xc),(unsigned)rd(0x28));return -1;
}
void board_adc_live_status(void){
 uint32_t display_address=current_ui?(uint32_t)(uintptr_t)current_ui->adc_record:0;
 uint32_t display_crc=display_address?screen_crc32((const uint8_t*)(uintptr_t)display_address,WORDS*4):0;
 /* hw_waiting/progress are live. Other hw_* fields describe the completed
    raw id above; ARM clears PL frozen flags, so never log those as last-id metadata. */
 board_log("ADC_LIVE_STATUS available=%u pending=%u id=%u address=%08x bytes=16384 rate=%u crc32=%08x status=%08x display_id=%u display_address=%08x display_crc=%08x requested_rate=%u applied_rate=%u rate_index=%u configbusy=%u filter_kind=%u capture_epoch=%u display_rate=%u display_epoch=%u watchdog=%u hw_waiting=%u hw_progress=%u hw_matched=%u hw_after=%u hw_auto=%u hw_start=%u hw_seen=%u conversion_hz=%u record_filter=%u display_filter=%u display_conversion_hz=%u\r\n",
  available,pending,last_id,(unsigned)last_address,(unsigned)(last_id?last_rate:rd(0x24)),(unsigned)report.crc32,(unsigned)rd(0xc),
  current_ui?current_ui->adc_id:0,(unsigned)display_address,(unsigned)display_crc,
  epoch_valid?capture_plan.rate_hz:0,(unsigned)rd(0x24),(unsigned)((rd(0x34)>>8)&7u),(unsigned)(rd(0x34)&1u),
  (unsigned)rd(0x3c),(unsigned)epoch_seen,current_ui?current_ui->adc_rate:0,
  current_ui?(unsigned)current_ui->adc_info.capture_epoch:0,(unsigned)rd(0x1c),
  (unsigned)((rd(0x54)>>8)&1u),(unsigned)rd(0x5c),last_trigger.matched?1u:0u,
  last_trigger.after_index,last_trigger.auto_forced?1u:0u,(unsigned)last_ring_start,(unsigned)last_samples_seen,
  (unsigned)rd(0x2c),(unsigned)last_filter,current_ui?current_ui->adc_info.filter_kind:0,
  current_ui?current_ui->adc_info.conversion_hz:0);
 if(last_id)for(unsigned c=0;c<2;c++){
  const AdcLiveChannel *p=&report.channel[c];
  board_log("ADC_LIVE_CHANNEL ch=%u min=%u max=%u mean=%.3f otr=%u vpp_nom=%.6f rms_total_nom=%.6f frequency_valid=%u frequency_hz=%.3f\r\n",
   c+1,p->min_code,p->max_code,p->mean_code,p->otr_count,p->vpp_v,p->rms_v,p->frequency_valid,p->frequency_hz);
 }
}
void board_adc_live_poll(ScopeUI *ui){
 if(!ui)return;
 if(current_ui!=ui){
  if(!scope_ui_set_conversion_hz(ui,CONVERSION_HZ))goto fail;
  scope_ui_enable_adaptive_sampling(ui,true);scope_ui_enable_hardware_trigger(ui,true);epoch_valid=0;
 }
 current_ui=ui;
 if(!available)return;
 uint32_t now=board_millis(),status=rd(0xc);
 if(!epoch_valid||epoch_seen!=ui->capture_epoch){
  if(scope_ui_capture_plan(ui,&capture_plan)||
     capture_plan.count!=WORDS||capture_plan.conversion_hz!=CONVERSION_HZ||
     scope_ui_hardware_config(ui,&capture_trigger)||
     !(capture_trigger.control&1u)||(capture_trigger.control&~15u)||
     capture_trigger.level_q8>1023u*256u||capture_trigger.pre_samples<1u||capture_trigger.pre_samples>WORDS-2u||
     capture_trigger.hysteresis_codes<1u||capture_trigger.hysteresis_codes>1023u)goto fail;
  /* Even a pause must cancel the previous capture. Keep the UI-owned DDR
     record intact while reset and rate acknowledgement complete. */
  wr(8,2);pending=config_waiting=trigger_configured=0;reset_waiting=1;reset_started=now;
  epoch_seen=ui->capture_epoch;epoch_valid=1;
  capture_info=(ScopeRecordInfo){epoch_seen,capture_plan.rate_index,capture_plan.conversion_hz,capture_plan.filter_kind};
  board_log("ADC_ACQ_REARM epoch=%u previous_raw_id=%u requested_rate=%u rate_index=%u timeout_ms=%u\r\n",
   (unsigned)epoch_seen,last_id,capture_plan.rate_hz,capture_plan.rate_index,capture_plan.timeout_ms);return;
 }
 if(reset_waiting){
  /* Soft reset preserves an outstanding configuration handshake. Do not
     submit a replacement request until that handshake also becomes idle. */
  if((status&17u)||(rd(0x34)&1u)){
   if((uint32_t)(now-reset_started)>=CONTROL_TIMEOUT_MS)goto fail;
   return;
  }
  reset_waiting=0;return;
 }
 if(!ui->model.running)return;
 if(config_waiting){
  unsigned config=rd(0x34);
  if((config&1u)||((config>>8)&7u)!=capture_plan.rate_index){
   if((uint32_t)(now-config_started)>=CONTROL_TIMEOUT_MS)goto fail;
   return;
  }
  if(!valid_applied_config()||rd(0x24)!=capture_plan.rate_hz||rd(0x3c)!=capture_plan.filter_kind)goto fail;
  config_waiting=0;
 }
 if(pending){
  if(status&(4u|8u|32u))goto fail;
  if((status&0x93u)==0x82u){
   unsigned id=rd(0x18);if(id!=old_id+1u)goto fail;
   unsigned flags=rd(0x54)&3u,after=rd(0x58),ring_start=rd(0x64),samples_seen=rd(0x68);
   if((flags!=1u&&flags!=2u)||ring_start>=WORDS||samples_seen<WORDS||
      (flags==1u&&after!=capture_trigger.pre_samples)||
      (flags==2u&&(after!=0u||(capture_trigger.control&8u))))goto fail;
   ScopeHardwareTrigger trigger={flags==1u,flags==2u,after};
   /* NORMAL may reject arbitrarily many records. Always protect the slot
      held by the UI rather than blindly alternating into it. */
   uint32_t address=(uintptr_t)ui->adc_record==0x13000000u?0x13004000u:0x13000000u;
   uint32_t *copy=(uint32_t*)(uintptr_t)address;
   for(unsigned i=0;i<WORDS;i++)copy[i]=rd(0x4000+4*i);
   if(rd(0x18)!=id||rd(0xc)!=status||(rd(0x54)&3u)!=flags||rd(0x58)!=after||rd(0x64)!=ring_start||rd(0x68)!=samples_seen||
      !valid_applied_config()||rd(0x24)!=capture_plan.rate_hz||rd(0x3c)!=capture_plan.filter_kind||
      adc_live_analyze(copy,WORDS,capture_plan.rate_hz,&report))goto fail;
   Xil_DCacheFlushRange(address,WORDS*4);__asm__ volatile("dsb sy":::"memory");
   ScopeAdcMetrics m[2];for(unsigned c=0;c<2;c++)m[c]=(ScopeAdcMetrics){report.channel[c].vpp_v,report.channel[c].rms_v,report.channel[c].frequency_hz,report.channel[c].frequency_valid!=0};
   (void)scope_ui_set_hardware_adc_record(ui,copy,WORDS,capture_plan.rate_hz,id,m,&capture_info,&trigger);
   last_address=address;last_id=id;last_rate=capture_plan.rate_hz;last_filter=capture_plan.filter_kind;pending=0;
   last_trigger=trigger;last_ring_start=ring_start;last_samples_seen=samples_seen;
   if(last_id<=2||(uint32_t)(now-last_report)>=1000){last_report=now;board_adc_live_status();}
  }else{
   uint32_t progress=rd(0x5c);
   /* NORMAL/single may wait indefinitely and AUTO may match a late edge.
      Only lack of valid-sample progress is a fault. != also handles wrap. */
   if(progress!=last_progress){last_progress=progress;started=now;}
   else if((uint32_t)(now-started)>=capture_plan.timeout_ms)goto fail;
  }
 }else{
  if(status&0x3du)goto fail;
  if(!valid_applied_config())goto fail;
  if(((rd(0x34)>>8)&7u)!=capture_plan.rate_index){
   wr(0x30,capture_plan.rate_index);config_waiting=1;config_started=now;return;
  }
  if(!valid_applied_config()||rd(0x24)!=capture_plan.rate_hz||rd(0x3c)!=capture_plan.filter_kind)goto fail;
  if(!trigger_configured){
   wr(0x40,capture_trigger.control);wr(0x44,capture_trigger.level_q8);
   wr(0x48,capture_trigger.pre_samples);wr(0x4c,capture_trigger.hysteresis_codes);
   if(rd(0x40)!=capture_trigger.control||rd(0x44)!=capture_trigger.level_q8||
      rd(0x48)!=capture_trigger.pre_samples||rd(0x4c)!=capture_trigger.hysteresis_codes)goto fail;
   trigger_configured=1;
  }
  old_id=rd(0x18);last_progress=rd(0x5c);wr(8,1);pending=1;started=now;
 }
 return;
fail:
 available=0;ui->model.running=false;
 lv_obj_add_state(ui->run_button,LV_STATE_DISABLED);
 lv_label_set_text(ui->state_label,scope_ui_acquisition_error_text(ui));
 board_log("ADC_LIVE_FAIL: runtime id=%u status=%08x\r\n",last_id,(unsigned)rd(0xc));
}
