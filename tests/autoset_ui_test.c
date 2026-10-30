#include "scope_ui.h"
#include "src/misc/lv_text_private.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static uint32_t buffer[1024*60],frame[1024*600],old_record[4096],next_record[4096];
static lv_indev_data_t finger;
static double amplitude=50,center=563,signal_frequency=50000;
static double other_amplitude=0,other_center=512;
static bool other_clip;
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *p){
 unsigned w=(unsigned)(a->x2-a->x1+1);for(int y=a->y1;y<=a->y2;y++)memcpy(frame+y*1024+a->x1,p+(y-a->y1)*w*4,w*4);lv_display_flush_ready(d);
}
static void pointer(lv_indev_t *d,lv_indev_data_t *out){(void)d;*out=finger;}
static void point(int x,int y,bool down){finger.point=(lv_point_t){x,y};finger.state=down?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;for(int i=0;i<6;i++){lv_tick_inc(10);lv_timer_handler();}}
static void click(lv_obj_t *o){assert(o);lv_obj_update_layout(lv_screen_active());lv_area_t a;lv_obj_get_coords(o,&a);point((a.x1+a.x2)/2,(a.y1+a.y2)/2,true);point((a.x1+a.x2)/2,(a.y1+a.y2)/2,false);}
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
static void save(lv_display_t *d,const char *dir,const char *name){
 lv_obj_update_layout(lv_screen_active());labels(lv_screen_active(),false);
 if(!dir)return;
 lv_refr_now(d);char path[512];snprintf(path,sizeof(path),"%s/%s.bgrx",dir,name);FILE *f=fopen(path,"wb");assert(f);assert(fwrite(frame,1,sizeof(frame),f)==sizeof(frame));assert(!fclose(f));
}
static bool has_label(lv_obj_t *o,const char *text){
 if(lv_obj_check_type(o,&lv_label_class)&&!strcmp(lv_label_get_text(o),text))return true;
 for(unsigned i=0;i<lv_obj_get_child_count(o);i++)if(has_label(lv_obj_get_child(o,i),text))return true;
 return false;
}
static ScopeCapturePlan profile(const ScopeUI *ui){ScopeCapturePlan p;assert(!scope_ui_capture_plan(ui,&p));return p;}
static bool feed(ScopeUI *ui,uint32_t *record,unsigned id,double frequency,bool matched,bool clip){
 ScopeCapturePlan p=profile(ui);ScopeHardwareConfig cfg;assert(!scope_ui_hardware_config(ui,&cfg));
 unsigned selected=ui->trigger_channel;double level=cfg.level_q8/256.0;
 for(unsigned i=0;i<4096;i++){
  double phase=6.283185307179586*(double)i*signal_frequency/p.rate_hz;
  unsigned ch1=(unsigned)lround(other_center+other_amplitude*sin(phase)),ch2=(unsigned)lround(center+amplitude*sin(phase));
  record[i]=ch1|(ch2<<16);
 }
 if(matched){unsigned shift=selected*16;record[cfg.pre_samples-1]=(record[cfg.pre_samples-1]&~(1023u<<shift))|((unsigned)floor(level-2)<<shift);
  record[cfg.pre_samples]=(record[cfg.pre_samples]&~(1023u<<shift))|((unsigned)ceil(level+2)<<shift);}
 if(clip)record[4095]|=1024u<<(selected*16);
 if(other_clip)record[4095]|=1024u;
 ScopeRecordInfo info={ui->capture_epoch,p.rate_index,p.conversion_hz,p.filter_kind};ScopeHardwareTrigger t={matched,!matched,matched?cfg.pre_samples:0};
 ScopeAdcMetrics metrics[2]={{0,0,0,false},{1,.6,frequency,frequency>0}};
 return scope_ui_set_hardware_adc_record(ui,record,4096,p.rate_hz,id,metrics,&info,&t);
}
static void same_backup(const ScopeUI *ui,const ScopeAutosetBackup *b){
 assert(ui->timebase_index==b->timebase_index&&ui->scale_index==b->scale_index&&ui->trigger_channel==b->trigger_channel);
 assert(!memcmp(ui->channel_scale_index,b->channel_scale_index,sizeof(b->channel_scale_index)));
 assert(!memcmp(ui->channel_position_mdiv,b->channel_position_mdiv,sizeof(b->channel_position_mdiv)));
 assert(ui->trigger_normal==b->trigger_normal&&ui->trigger_rising==b->trigger_rising&&ui->trigger_level_mv==b->trigger_level_mv);
 assert(ui->model.running==b->running&&ui->single_complete==b->single_complete&&ui->adc_triggered==b->adc_triggered&&ui->adc_waiting==b->adc_waiting);
 assert(ui->adc_display_start==b->adc_display_start&&ui->adc_trigger_crossing==b->adc_trigger_crossing);
}
static void assert_old(const ScopeUI *ui,const ScopeMeasurement old[2],unsigned id){assert(ui->adc_record==old_record&&ui->adc_id==id&&!memcmp(old,ui->measurement,sizeof(ui->measurement)));}
int main(int argc,char **argv){
 const char *dir=argc>1?argv[1]:NULL;
 lv_init();lv_display_t *d=lv_display_create(1024,600);lv_display_set_color_format(d,LV_COLOR_FORMAT_XRGB8888);
 lv_display_set_buffers(d,buffer,NULL,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(d,flush);
 lv_indev_t *input=lv_indev_create();lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(input,pointer);
 ScopeUI ui;scope_ui_create(&ui);assert(has_label(lv_screen_active(),"自动设置"));
 assert(!scope_ui_autoset_start(NULL)&&!scope_ui_autoset_busy(NULL));scope_ui_autoset_cancel(NULL);
 assert(!scope_ui_autoset_start(&ui)&&ui.autoset_state==SCOPE_AUTO_NO_RECORD);
 save(d,dir,"autoset-no-record-zh");
 scope_ui_enable_adaptive_sampling(&ui,true);scope_ui_enable_hardware_trigger(&ui,true);assert(scope_ui_set_conversion_hz(&ui,50000000));ui.timebase_index=5;
 assert(scope_ui_select_source(&ui,1)&&scope_ui_select_trigger_source(&ui,1));assert(feed(&ui,old_record,1,50000,true,false));
 assert(scope_ui_configure_channel(&ui,0,3,250)&&scope_ui_configure_channel(&ui,1,2,-500));scope_ui_set_cursors(&ui,true);
 assert(scope_ui_arm_single(&ui));unsigned epoch=ui.capture_epoch;assert(!scope_ui_autoset_start(&ui)&&ui.capture_epoch==epoch&&ui.single_armed);
 assert(feed(&ui,old_record,2,50000,true,false));assert(ui.single_complete&&!ui.model.running);double old_start=ui.adc_display_start;
 ScopeMeasurement cached[2];memcpy(cached,ui.measurement,sizeof(cached));ScopeRecordInfo held_info=ui.adc_info;ScopeAdcMetrics held_metrics[2];memcpy(held_metrics,ui.adc_metrics,sizeof(held_metrics));
 assert(scope_ui_show_measurements(&ui,true));click(ui.measurement_close_button);click(ui.channel_panel_button);click(ui.channel_close_button);click(ui.settings_button);click(ui.close_button);
 click(ui.autoset_button);assert(scope_ui_autoset_busy(&ui)&&ui.autoset_state==SCOPE_AUTO_WAITING&&ui.capture_epoch==epoch+1);
 assert(ui.model.running&&!ui.single_complete&&!ui.single_armed&&ui.trigger_channel==1&&ui.trigger_rising&&!ui.trigger_normal);
 assert(ui.timebase_index==6&&ui.adc_display_start==old_start&&ui.cursors_on);assert_old(&ui,cached,2);
 assert(!memcmp(&held_info,&ui.adc_info,sizeof(held_info))&&!memcmp(held_metrics,ui.adc_metrics,sizeof(held_metrics)));
 assert(lv_obj_has_flag(ui.settings,LV_OBJ_FLAG_HIDDEN)&&lv_obj_has_flag(ui.measurement_panel,LV_OBJ_FLAG_HIDDEN)&&lv_obj_has_flag(ui.channel_panel,LV_OBJ_FLAG_HIDDEN));
 ScopeAutosetBackup backup=ui.autoset_backup;unsigned applying_epoch=ui.capture_epoch;int time=ui.timebase_index;
 assert(!scope_ui_autoset_start(&ui)&&ui.capture_epoch==applying_epoch);
 assert(!scope_ui_configure_channel(&ui,1,6,3500)&&!scope_ui_select_source(&ui,0)&&!scope_ui_select_trigger_source(&ui,0));
 assert(!scope_ui_configure_trigger(&ui,true,false,100)&&!scope_ui_arm_single(&ui)&&!scope_ui_move_cursors(&ui,0,1000));
 assert(!scope_ui_set_conversion_hz(&ui,5000000)&&!scope_ui_show_measurements(&ui,true));
 scope_ui_set_cursors(&ui,false);scope_ui_set_dual_display(&ui,true);scope_ui_enable_adaptive_sampling(&ui,false);scope_ui_enable_hardware_trigger(&ui,false);
 assert(ui.cursors_on&&!ui.dual_display&&ui.adaptive_sampling&&ui.hardware_trigger);
 lv_obj_send_event(ui.adjust_buttons[1],LV_EVENT_CLICKED,NULL);assert(ui.timebase_index==time);
 click(ui.adjust_buttons[1]);click(ui.wave_buttons[0]);assert(ui.timebase_index==time&&ui.adc_channel==1);
 save(d,dir,"autoset-waiting-zh");
 /* Stale input must not consume seen ID, nor release the protected DDR slot. */
 ScopeCapturePlan p=profile(&ui);ScopeRecordInfo stale={ui.capture_epoch-1,p.rate_index,p.conversion_hz,p.filter_kind};ScopeAdcMetrics m[2]={{0},{1,.6,50000,true}};ScopeHardwareTrigger forced={false,true,0};
 assert(!scope_ui_set_hardware_adc_record(&ui,next_record,4096,p.rate_hz,20,m,&stale,&forced)&&ui.adc_last_seen_id==2);
 stale.capture_epoch=ui.capture_epoch;stale.filter_kind=SCOPE_FILTER_RAW;
 assert(!scope_ui_set_hardware_adc_record(&ui,next_record,4096,p.rate_hz,20,m,&stale,&forced)&&ui.adc_last_seen_id==2);
 stale.filter_kind=p.filter_kind;stale.conversion_hz=25000000;
 assert(!scope_ui_set_hardware_adc_record(&ui,next_record,4096,p.rate_hz,20,m,&stale,&forced)&&ui.adc_last_seen_id==2);
 click(ui.autoset_cancel_button);assert(ui.autoset_state==SCOPE_AUTO_CANCELED&&!scope_ui_autoset_busy(&ui)&&ui.capture_epoch==applying_epoch+1);same_backup(&ui,&backup);assert_old(&ui,cached,2);
 scope_ui_autoset_cancel(&ui);assert(ui.capture_epoch==applying_epoch+1);
 /* Results expire in frozen as well as running views; audit state survives. */
 assert(strstr(lv_label_get_text(ui.footer_label),"自动设置已取消"));
 scope_ui_step(&ui,5.01);assert(!ui.model.running&&ui.single_complete&&ui.autoset_state==SCOPE_AUTO_CANCELED);
 assert(strstr(lv_label_get_text(ui.footer_label),"采样率")&&strstr(lv_label_get_text(ui.footer_label),"存储深度"));
 /* A valid new record with a changed frequency is consumed, but never displayed. */
 assert(scope_ui_autoset_start(&ui));assert(!feed(&ui,next_record,3,60000,true,false));assert(ui.autoset_state==SCOPE_AUTO_UNSTABLE&&ui.adc_last_seen_id==3);same_backup(&ui,&backup);assert_old(&ui,cached,2);
 assert(scope_ui_autoset_start(&ui));assert(!feed(&ui,next_record,4,50000,false,false));assert(ui.autoset_state==SCOPE_AUTO_UNSTABLE&&ui.adc_last_seen_id==4);same_backup(&ui,&backup);assert_old(&ui,cached,2);
 assert(scope_ui_autoset_start(&ui));assert(!feed(&ui,next_record,5,50000,true,true));assert(ui.autoset_state==SCOPE_AUTO_OVERRANGE&&ui.adc_last_seen_id==5);same_backup(&ui,&backup);assert_old(&ui,cached,2);
 assert(scope_ui_autoset_start(&ui));assert(!feed(&ui,next_record,6,0,true,false));assert(ui.autoset_state==SCOPE_AUTO_UNSTABLE);same_backup(&ui,&backup);assert_old(&ui,cached,2);
 amplitude=150;assert(scope_ui_autoset_start(&ui));assert(!feed(&ui,next_record,60,50000,true,false));assert(ui.autoset_state==SCOPE_AUTO_UNSTABLE&&ui.adc_last_seen_id==60);same_backup(&ui,&backup);assert_old(&ui,cached,2);amplitude=50;
 center=593;assert(scope_ui_autoset_start(&ui));assert(!feed(&ui,next_record,61,50000,true,false));assert(ui.autoset_state==SCOPE_AUTO_UNSTABLE&&ui.adc_last_seen_id==61);same_backup(&ui,&backup);assert_old(&ui,cached,2);center=563;
 /* Every channel whose display was planned must remain valid and visible. */
 other_amplitude=100;assert(scope_ui_autoset_start(&ui));assert(!feed(&ui,next_record,62,50000,true,false));assert(ui.autoset_state==SCOPE_AUTO_UNSTABLE&&ui.adc_last_seen_id==62);same_backup(&ui,&backup);assert_old(&ui,cached,2);other_amplitude=0;
 other_center=525;assert(scope_ui_autoset_start(&ui));assert(!feed(&ui,next_record,63,50000,true,false));assert(ui.autoset_state==SCOPE_AUTO_UNSTABLE&&ui.adc_last_seen_id==63);same_backup(&ui,&backup);assert_old(&ui,cached,2);other_center=512;
 other_clip=true;assert(scope_ui_autoset_start(&ui));assert(!feed(&ui,next_record,64,50000,true,false));assert(ui.autoset_state==SCOPE_AUTO_OVERRANGE&&ui.adc_last_seen_id==64);same_backup(&ui,&backup);assert_old(&ui,cached,2);other_clip=false;
 save(d,dir,"autoset-overrange-zh");
 assert(scope_ui_autoset_start(&ui));assert(!feed(&ui,next_record,65,52500,true,false));assert(ui.autoset_state==SCOPE_AUTO_UNSTABLE&&ui.adc_last_seen_id==65);same_backup(&ui,&backup);assert_old(&ui,cached,2);
 save(d,dir,"autoset-unstable-zh");
 assert(scope_ui_autoset_start(&ui));double deadline=ui.autoset_deadline_s;assert(deadline>=2);scope_ui_step(&ui,deadline-.01);assert(scope_ui_autoset_busy(&ui));scope_ui_step(&ui,.02);assert(ui.autoset_state==SCOPE_AUTO_TIMEOUT);same_backup(&ui,&backup);assert_old(&ui,cached,2);
 save(d,dir,"autoset-timeout-zh");
 assert(scope_ui_autoset_start(&ui));scope_ui_set_running(&ui,false);assert(!scope_ui_autoset_busy(&ui)&&!ui.model.running&&ui.autoset_state==SCOPE_AUTO_CANCELED);assert_old(&ui,cached,2);
 assert(scope_ui_autoset_start(&ui));ui.model.running=false;scope_ui_step(&ui,.01);assert(!scope_ui_autoset_busy(&ui)&&!ui.model.running&&ui.autoset_state==SCOPE_AUTO_CANCELED);
 /* Publication is a second step; successful autoset is never a continuing tracker. */
 other_amplitude=1;assert(scope_ui_autoset_start(&ui));epoch=ui.capture_epoch;assert(feed(&ui,next_record,70,50000,true,false));assert(ui.autoset_state==SCOPE_AUTO_COMPLETE&&!scope_ui_autoset_busy(&ui)&&ui.adc_record==next_record&&ui.adc_id==70&&ui.model.running&&ui.capture_epoch==epoch);other_amplitude=0;
 assert(lv_obj_has_flag(ui.autoset_panel,LV_OBJ_FLAG_HIDDEN));save(d,dir,"autoset-complete-zh");
 assert(strstr(lv_label_get_text(ui.footer_label),"自动设置完成"));scope_ui_step(&ui,4.99);
 assert(strstr(lv_label_get_text(ui.footer_label),"自动设置完成"));scope_ui_step(&ui,.02);
 assert(ui.autoset_state==SCOPE_AUTO_COMPLETE&&strstr(lv_label_get_text(ui.footer_label),"采样率")&&strstr(lv_label_get_text(ui.footer_label),"数字通带"));
 save(d,dir,"autoset-complete-footer-zh");
 assert(scope_ui_configure_channel(&ui,1,3,1000));assert(feed(&ui,old_record,80,50000,true,false));assert(ui.channel_scale_index[1]==3&&ui.channel_position_mdiv[1]==1000);
 assert(scope_ui_show_measurements(&ui,true));assert(scope_ui_autoset_start(&ui));assert(lv_obj_has_flag(ui.measurement_panel,LV_OBJ_FLAG_HIDDEN));scope_ui_autoset_cancel(&ui);assert(ui.model.running&&ui.adc_last_seen_id==80&&ui.adc_record==old_record);
 scope_ui_set_running(&ui,false);epoch=ui.capture_epoch;int original_time=ui.timebase_index;
 assert(scope_ui_select_source(&ui,0));assert(!scope_ui_autoset_start(&ui)&&ui.autoset_state==SCOPE_AUTO_NO_SIGNAL&&ui.capture_epoch==epoch&&ui.timebase_index==original_time);
 assert(strstr(lv_label_get_text(ui.footer_label),"无可靠信号"));scope_ui_step(&ui,5.01);
 assert(!ui.model.running&&ui.autoset_state==SCOPE_AUTO_NO_SIGNAL&&strstr(lv_label_get_text(ui.footer_label),"采样率"));
 assert(scope_ui_select_source(&ui,2));epoch=ui.capture_epoch;assert(!scope_ui_autoset_start(&ui)&&ui.autoset_state==SCOPE_AUTO_UNSUPPORTED&&ui.capture_epoch==epoch);
 save(d,dir,"autoset-unsupported-zh");
 assert(scope_ui_select_source(&ui,1));ui.measurement[1].overrange=true;epoch=ui.capture_epoch;assert(!scope_ui_autoset_start(&ui)&&ui.autoset_state==SCOPE_AUTO_OVERRANGE&&ui.capture_epoch==epoch);ui.measurement[1].overrange=false;
 ui.adc_metrics[1].frequency_hz=1;assert(!scope_ui_autoset_start(&ui)&&ui.autoset_state==SCOPE_AUTO_NO_SIGNAL&&ui.autoset_need_wider);ui.adc_metrics[1].frequency_hz=50000;
 save(d,dir,"autoset-short-record-zh");
 ui.adc_metrics[1].frequency_valid=false;assert(!scope_ui_autoset_start(&ui)&&ui.autoset_state==SCOPE_AUTO_NO_SIGNAL&&!ui.autoset_need_wider);ui.adc_metrics[1].frequency_valid=true;
 save(d,dir,"autoset-no-period-zh");
 click(ui.settings_button);click(ui.language_button);click(ui.close_button);assert(scope_ui_autoset_start(&ui));save(d,dir,"autoset-waiting-en");scope_ui_autoset_cancel(&ui);save(d,dir,"autoset-canceled-en");
 /* The slowest profile needs more than fourteen seconds, not a fixed 2 s. */
 ui.timebase_index=20;scope_ui_set_running(&ui,true);signal_frequency=1;
 assert(feed(&ui,old_record,90,1,true,false));assert(ui.adc_rate==625);
 memcpy(cached,ui.measurement,sizeof(cached));
 assert(scope_ui_autoset_start(&ui));assert(ui.timebase_index==20&&fabs(ui.autoset_deadline_s-(2.0*4096/625+1))<1e-12);
 backup=ui.autoset_backup;deadline=ui.autoset_deadline_s;epoch=ui.capture_epoch;
 scope_ui_step(&ui,NAN);scope_ui_step(&ui,INFINITY);scope_ui_step(&ui,-1);assert(ui.autoset_elapsed_s==0);
 scope_ui_step(&ui,2);assert(scope_ui_autoset_busy(&ui));scope_ui_step(&ui,deadline-2-.001);assert(scope_ui_autoset_busy(&ui));
 scope_ui_step(&ui,.002);assert(ui.autoset_state==SCOPE_AUTO_TIMEOUT&&ui.capture_epoch==epoch+1&&ui.adc_last_seen_id==90);same_backup(&ui,&backup);assert_old(&ui,cached,90);
 save(d,dir,"autoset-slow-timeout-en");
 /* A pre-existing overrange on the other channel is deliberately unplanned. */
 other_clip=true;assert(feed(&ui,old_record,91,1,true,false));
 assert(scope_ui_configure_channel(&ui,0,4,2000));
 assert(scope_ui_autoset_start(&ui)&&ui.autoset_plan.channel_mask==2);
 assert(ui.channel_scale_index[0]==4&&ui.channel_position_mdiv[0]==2000);
 assert(feed(&ui,next_record,92,1.04999,true,false)&&ui.autoset_state==SCOPE_AUTO_COMPLETE);
 assert(ui.channel_scale_index[0]==4&&ui.channel_position_mdiv[0]==2000);other_clip=false;
 lv_deinit();puts("AUTOSET_UI_TEST_PASS: real pointer, two-step publication, protected cache, rollback, monotonic epoch/seen, timeout/fault, guards and one-shot");return 0;
}
