#include "scope_ui.h"
#include "src/misc/lv_text_private.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t buffer[1024*600],frame[1024*600],record[4096],invalid[4097];
static lv_indev_data_t finger;
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *p){
 unsigned w=(unsigned)(a->x2-a->x1+1);for(int y=a->y1;y<=a->y2;y++)memcpy(frame+y*1024+a->x1,p+(y-a->y1)*w*4,w*4);lv_display_flush_ready(d);
}
static void pointer(lv_indev_t *d,lv_indev_data_t *out){(void)d;*out=finger;}
static void point(int x,int y,bool down){finger.point=(lv_point_t){x,y};finger.state=down?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;for(int i=0;i<6;i++){lv_tick_inc(10);lv_timer_handler();}}
static void click(lv_obj_t *o){assert(o);lv_obj_update_layout(lv_screen_active());lv_area_t a;lv_obj_get_coords(o,&a);point((a.x1+a.x2)/2,(a.y1+a.y2)/2,true);point((a.x1+a.x2)/2,(a.y1+a.y2)/2,false);}
static void save(lv_display_t *d,const char *dir,const char *name){
 if(!dir)return;
 lv_obj_invalidate(lv_screen_active());lv_refr_now(d);char p[512];snprintf(p,sizeof(p),"%s/%s.bgrx",dir,name);FILE *f=fopen(p,"wb");assert(f);assert(fwrite(frame,1,sizeof(frame),f)==sizeof(frame));assert(!fclose(f));
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
static bool has_label(lv_obj_t *o,const char *text){
 if(lv_obj_check_type(o,&lv_label_class)&&!strcmp(lv_label_get_text(o),text))return true;
 for(unsigned i=0;i<lv_obj_get_child_count(o);i++)if(has_label(lv_obj_get_child(o,i),text))return true;
 return false;
}
static void contents(ScopeUI *ui){
 const ScopeMeasurement *m=&ui->measurement[ui->adc_channel];
 const double expected[]={m->vmax_v,m->vmin_v,m->vmean_v,m->vpp_v,m->rms_total_v,m->rms_ac_v};
 for(unsigned i=0;i<6;i++)assert(fabs(strtod(lv_label_get_text(ui->measurement_values[i]),NULL)-expected[i])<.00006);
 assert(strstr(lv_label_get_text(ui->measurement_note),ui->language==SCOPE_ZH_CN?"完整记录":"FULL RECORD"));
 assert(strstr(lv_label_get_text(ui->measurement_note),"4096"));
}
static void timing_contents(const ScopeUI *ui){
 const ScopeTimingMeasurement *t=&ui->measurement[ui->adc_channel].timing;
 assert(ui->measurement_timing&&!lv_obj_has_flag(ui->measurement_panel,LV_OBJ_FLAG_HIDDEN));
 assert(fabs(strtod(lv_label_get_text(ui->timing_values[0]),NULL)-t->duty_percent)<.00006);
 for(unsigned i=0;i<8;i++)assert(lv_obj_has_flag(ui->measurement_names[i],LV_OBJ_FLAG_HIDDEN)&&lv_obj_has_flag(ui->measurement_values[i],LV_OBJ_FLAG_HIDDEN));
 for(unsigned i=0;i<6;i++)assert(!lv_obj_has_flag(ui->timing_names[i],LV_OBJ_FLAG_HIDDEN)&&!lv_obj_has_flag(ui->timing_values[i],LV_OBJ_FLAG_HIDDEN));
 assert(strstr(lv_label_get_text(ui->measurement_note),"50%")&&strstr(lv_label_get_text(ui->measurement_note),"10–90%"));
}
static bool feed_timing(ScopeUI *ui,uint32_t *words,unsigned id,unsigned width){
 ScopeCapturePlan p;ScopeHardwareConfig cfg;
 assert(!scope_ui_capture_plan(ui,&p)&&!scope_ui_hardware_config(ui,&cfg));
 for(unsigned i=0;i<4096;i++)words[i]=(i%125<width?700u:500u)|((i%125<width?740u:540u)<<16);
 ScopeRecordInfo info={ui->capture_epoch,p.rate_index,p.conversion_hz,p.filter_kind};
 ScopeHardwareTrigger trigger={false,true,0};
 ScopeAdcMetrics m[2]={{2,1,p.rate_hz/125.0,true},{2,1,p.rate_hz/125.0,true}};
 return scope_ui_set_hardware_adc_record(ui,words,4096,p.rate_hz,id,m,&info,&trigger);
}
static void timing_pages(lv_display_t *d,const char *dir){
 lv_obj_clean(lv_screen_active());ScopeUI ui;scope_ui_create(&ui);
 assert(!scope_ui_show_timing_measurements(NULL,true)&&!scope_ui_show_timing_measurements(&ui,true)&&!ui.measurement_timing);
 for(unsigned i=0;i<4096;i++)record[i]=(i%100<20?700u:500u)|((i%100<80?740u:540u)<<16);
 ScopeAdcMetrics m[2]={{2,1,10000,true},{2,1,10000,true}};
 assert(scope_ui_set_adc_record(&ui,record,4096,1000000,1,m));
 assert(scope_ui_select_source(&ui,1)&&scope_ui_select_trigger_source(&ui,1));
 assert(scope_ui_arm_single(&ui)&&scope_ui_set_adc_record(&ui,record,4096,1000000,2,m));
 assert(ui.single_complete&&!ui.model.running&&ui.measurement[0].timing.pulse_valid);
 ScopeMeasurement cache[2];memcpy(cache,ui.measurement,sizeof(cache));
 unsigned epoch=ui.capture_epoch,id=ui.adc_id,seen=ui.adc_last_seen_id,trigger=ui.trigger_channel;
 double crossing=ui.adc_trigger_crossing,start=ui.adc_display_start;
 assert(scope_ui_spectrum_update(&ui));unsigned fftseq=ui.spectrum_sequence;
 uint32_t fftcrc=scope_record_crc32(&ui.spectrum_result,sizeof(ui.spectrum_result));
 uint32_t rawcrc=scope_record_crc32(record,sizeof(record));
 /* These physical coordinates are the accepted basic dialog controls. */
 point(620,32,true);point(620,32,false);assert(!ui.measurement_timing);contents(&ui);
 point(806,162,true);point(806,162,false);timing_contents(&ui);
 assert(fabs(strtod(lv_label_get_text(ui.timing_values[0]),NULL)-80)<.00001);
 assert(!strcmp(lv_label_get_text(ui.timing_values[3]),"--")&&!strcmp(lv_label_get_text(ui.timing_values[4]),"--"));
 assert(!strcmp(lv_label_get_text(ui.timing_values[5]),"1.0000 µs"));
 point(238,162,true);point(238,162,false);timing_contents(&ui);assert(ui.adc_channel==0&&ui.trigger_channel==trigger);
 assert(!strcmp(lv_label_get_text(ui.timing_values[0]),"20.0000 %")&&!strcmp(lv_label_get_text(ui.timing_values[1]),"20.0000 µs")&&!strcmp(lv_label_get_text(ui.timing_values[2]),"80.0000 µs"));
 lv_obj_update_layout(lv_screen_active());labels(lv_screen_active(),false);save(d,dir,"timing-square-20-zh");
 point(402,162,true);point(402,162,false);assert(ui.adc_channel==1);timing_contents(&ui);
 point(811,112,true);point(811,112,false);assert(lv_obj_has_flag(ui.measurement_panel,LV_OBJ_FLAG_HIDDEN));
 click(ui.settings_button);click(ui.language_button);click(ui.close_button);click(ui.measurement_button);timing_contents(&ui);
 assert(has_label(lv_screen_active(),"BASIC"));lv_obj_update_layout(lv_screen_active());labels(lv_screen_active(),false);save(d,dir,"timing-square-80-en");
 click(ui.measurement_timing_button);assert(!ui.measurement_timing&&!lv_obj_has_flag(ui.measurement_panel,LV_OBJ_FLAG_HIDDEN));contents(&ui);
 assert(scope_ui_show_timing_measurements(&ui,true));timing_contents(&ui);
 assert(ui.capture_epoch==epoch&&ui.adc_id==id&&ui.adc_last_seen_id==seen&&ui.single_complete&&!ui.model.running);
 assert(ui.adc_trigger_crossing==crossing&&ui.adc_display_start==start&&!memcmp(cache,ui.measurement,sizeof(cache)));
 assert(rawcrc==scope_record_crc32(record,sizeof(record))&&ui.spectrum_sequence==fftseq&&fftcrc==scope_record_crc32(&ui.spectrum_result,sizeof(ui.spectrum_result)));
 assert(scope_ui_set_conversion_hz(&ui,50000000)&&ui.adc_rate==1000000);
 assert(!strcmp(lv_label_get_text(ui.timing_values[5]),"1.0000 µs")&&!memcmp(cache,ui.measurement,sizeof(cache)));
 /* Actual pointer events cannot reach main timebase controls through the modal.
    Close before adjusting; a requested profile never relabels the frozen raw. */
 int requested_time=ui.timebase_index;unsigned requested_epoch=ui.capture_epoch;
 click(ui.adjust_buttons[1]);click(ui.adjust_buttons[0]);
 assert(ui.timebase_index==requested_time&&ui.capture_epoch==requested_epoch&&!ui.model.running);
 assert(ui.adc_id==id&&ui.adc_rate==1000000&&!memcmp(cache,ui.measurement,sizeof(cache)));
 assert(!strcmp(lv_label_get_text(ui.timing_values[5]),"1.0000 µs"));
 click(ui.measurement_close_button);click(ui.adjust_buttons[1]);
 assert(ui.timebase_index==requested_time+1&&ui.capture_epoch==requested_epoch+1&&!ui.model.running);
 assert(ui.adc_id==id&&ui.adc_rate==1000000&&ui.adc_record==record&&!memcmp(cache,ui.measurement,sizeof(cache)));
 click(ui.measurement_button);timing_contents(&ui);
 assert(!strcmp(lv_label_get_text(ui.timing_values[5]),"1.0000 µs"));
 assert(rawcrc==scope_record_crc32(record,sizeof(record))&&ui.spectrum_sequence==fftseq&&fftcrc==scope_record_crc32(&ui.spectrum_result,sizeof(ui.spectrum_result)));
 click(ui.measurement_close_button);click(ui.adjust_buttons[0]);
 assert(ui.timebase_index==requested_time&&ui.capture_epoch==requested_epoch+2&&!ui.model.running);
 click(ui.measurement_button);timing_contents(&ui);
 assert(ui.adc_id==id&&ui.adc_rate==1000000&&!memcmp(cache,ui.measurement,sizeof(cache)));
 assert(!strcmp(lv_label_get_text(ui.timing_values[5]),"1.0000 µs"));
 puts("TIMING_MODAL_GUARD_PASS: pointer +/- blocked while open; close/adjust/reopen changes requested profile and retains frozen raw rate/ID/timing/FFT");
 click(ui.measurement_close_button);scope_ui_set_running(&ui,true);
 for(unsigned i=0;i<4096;i++)record[i]=(unsigned)lround(512+180*sin(6.283185307179586*i/128))|((unsigned)lround(650+180*sin(6.283185307179586*i/256))<<16);
 assert(scope_ui_set_adc_record(&ui,record,4096,1000000,3,m));assert(scope_ui_show_timing_measurements(&ui,true));
 assert(ui.measurement[1].timing.rise_valid&&ui.measurement[1].timing.fall_valid);
 assert(strcmp(lv_label_get_text(ui.timing_values[3]),"--")&&strcmp(lv_label_get_text(ui.timing_values[4]),"--"));
 lv_obj_update_layout(lv_screen_active());labels(lv_screen_active(),false);save(d,dir,"timing-sine-edges-en");
 click(ui.measurement_close_button);click(ui.settings_button);click(ui.language_button);click(ui.close_button);
 assert(scope_ui_show_timing_measurements(&ui,true));lv_obj_update_layout(lv_screen_active());labels(lv_screen_active(),false);save(d,dir,"timing-sine-edges-zh");
 /* A resolved slow rise and one-sample fall have independent validity. */
 for(unsigned i=0;i<4096;i++){
  unsigned p=i%200,code=p<20?500+10*p:p<80?700:500;record[i]=code|(650u<<16);
 }
 assert(scope_ui_set_adc_record(&ui,record,4096,1000000,4,m)&&scope_ui_select_source(&ui,0));
 assert(ui.measurement[0].timing.pulse_valid&&ui.measurement[0].timing.rise_valid&&!ui.measurement[0].timing.fall_valid);
 assert(!strcmp(lv_label_get_text(ui.timing_values[3]),"16.0000 µs")&&!strcmp(lv_label_get_text(ui.timing_values[4]),"--"));
 assert(scope_ui_select_source(&ui,1));for(unsigned i=0;i<5;i++)assert(!strcmp(lv_label_get_text(ui.timing_values[i]),"--"));
 for(unsigned i=0;i<4096;i++)record[i]=512u|(650u<<16);
 assert(scope_ui_set_adc_record(&ui,record,4096,1000000,5,m));
 for(unsigned i=0;i<5;i++)assert(!strcmp(lv_label_get_text(ui.timing_values[i]),"--"));
 lv_obj_update_layout(lv_screen_active());labels(lv_screen_active(),false);save(d,dir,"timing-no-signal-zh");
 assert(scope_ui_select_source(&ui,2)&&!scope_ui_show_timing_measurements(&ui,false)&&ui.measurement_timing);
 assert(scope_ui_select_source(&ui,1));lv_obj_send_event(ui.bars_button,LV_EVENT_CLICKED,NULL);
 assert(!scope_ui_show_timing_measurements(&ui,false)&&ui.measurement_timing);lv_obj_send_event(ui.bars_button,LV_EVENT_CLICKED,NULL);
 /* Replay must recompute from the saved original rate, then restore live cache. */
 scope_ui_enable_adaptive_sampling(&ui,true);scope_ui_enable_hardware_trigger(&ui,true);ui.timebase_index=6;
 assert(feed_timing(&ui,record,6,25));assert(scope_ui_memory_save(&ui));assert(scope_ui_settings_save(&ui));
 uint32_t wavecrc=scope_record_crc32(ui.memory_blob,sizeof(ui.memory_blob));
 uint32_t paramcrc=scope_record_crc32(ui.settings_blob,sizeof(ui.settings_blob));
 ui.timebase_index=5;assert(feed_timing(&ui,invalid,7,100));ScopeMeasurement live[2];memcpy(live,ui.measurement,sizeof(live));
 assert(scope_ui_memory_load(&ui)&&ui.adc_rate==6250000&&ui.conversion_hz==50000000);
 assert(scope_ui_show_timing_measurements(&ui,true));timing_contents(&ui);
 assert(!strcmp(lv_label_get_text(ui.timing_values[0]),"20.0000 %")&&!strcmp(lv_label_get_text(ui.timing_values[1]),"4.0000 µs")&&!strcmp(lv_label_get_text(ui.timing_values[5]),"160.0000 ns"));
 epoch=ui.capture_epoch;id=ui.adc_id;seen=ui.adc_last_seen_id;ScopeMeasurement replay[2];memcpy(replay,ui.measurement,sizeof(replay));
 assert(scope_ui_show_timing_measurements(&ui,false)&&scope_ui_show_timing_measurements(&ui,true));click(ui.measurement_choices[0]);click(ui.measurement_choices[1]);
 assert(ui.capture_epoch==epoch&&ui.adc_id==id&&ui.adc_last_seen_id==seen&&!memcmp(replay,ui.measurement,sizeof(replay)));
 assert(wavecrc==scope_record_crc32(ui.memory_blob,sizeof(ui.memory_blob))&&paramcrc==scope_record_crc32(ui.settings_blob,sizeof(ui.settings_blob)));
 lv_obj_update_layout(lv_screen_active());labels(lv_screen_active(),false);save(d,dir,"timing-replay-original-rate-zh");
 assert(scope_ui_memory_return_live(&ui)&&ui.adc_rate==50000000&&!memcmp(live,ui.measurement,sizeof(live)));
 assert(!strcmp(lv_label_get_text(ui.timing_values[0]),"80.0000 %")&&!strcmp(lv_label_get_text(ui.timing_values[5]),"20.0000 ns"));
 assert(scope_ui_autoset_start(&ui));epoch=ui.capture_epoch;assert(!scope_ui_show_timing_measurements(&ui,false)&&ui.measurement_timing&&ui.capture_epoch==epoch);scope_ui_autoset_cancel(&ui);
 puts("TIMING_UI_TEST_PASS: pointer pages/old controls, duty20/80, edges, invalid, bilingual glyph/layout, frozen rate, replay/live caches, slots and FFT guards");
}
int main(int argc,char **argv){
 const char *dir=argc>1?argv[1]:NULL;
 lv_init();lv_display_t *d=lv_display_create(1024,600);lv_display_set_color_format(d,LV_COLOR_FORMAT_XRGB8888);
 /* Match the board's full-frame renderer when validating visible labels. */
 lv_display_set_buffers(d,buffer,NULL,sizeof(buffer),LV_DISPLAY_RENDER_MODE_FULL);lv_display_set_flush_cb(d,flush);
 lv_indev_t *input=lv_indev_create();lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(input,pointer);
 ScopeUI ui;scope_ui_create(&ui);
 assert(has_label(lv_screen_active(),"测量")&&"Measurement details entry is missing");
 assert(lv_obj_has_flag(ui.measurement_button,LV_OBJ_FLAG_HIDDEN));assert(!scope_ui_show_measurements(&ui,true));
 assert(!scope_ui_show_measurements(NULL,true));double f=100;assert(!scope_ui_measurement_frequency(&ui,0,&f)&&f==0);
 for(unsigned i=0;i<4096;i++)record[i]=(unsigned)lround(512+80*sin(6.283185307179586*i/625))|((unsigned)lround(563+60*sin(6.283185307179586*i/625))<<16);
 /* The end of the complete record is outside the visible 1250-point window. */
 record[4095]=700u|(600u<<16);
 ScopeAdcMetrics metrics[2]={{1.6,.6,1000,true},{1.2,.7,1000,true}};
 assert(scope_ui_set_adc_record(&ui,record,4096,625000,1,metrics));assert(scope_ui_select_source(&ui,1));
 assert(scope_ui_select_trigger_source(&ui,1));assert(scope_ui_arm_single(&ui));
 assert(scope_ui_set_adc_record(&ui,record,4096,625000,2,metrics));assert(ui.single_complete&&!ui.model.running);
 scope_ui_set_cursors(&ui,true);assert(scope_ui_move_cursors(&ui,100,900));
 unsigned id=ui.adc_id,seen=ui.adc_last_seen_id,epoch=ui.capture_epoch,source=ui.trigger_channel;
 double start=ui.adc_display_start;ScopeCursorResult cursor=ui.cursor_result;
 ScopeMeasurement cached[2];memcpy(cached,ui.measurement,sizeof(cached));
 click(ui.measurement_button);assert(!lv_obj_has_flag(ui.measurement_panel,LV_OBJ_FLAG_HIDDEN));contents(&ui);
 assert(has_label(lv_screen_active(),"时域")&&"Advanced time-domain page entry is missing");
 assert(scope_ui_measurement_frequency(&ui,1,&f)&&f==1000);assert(strstr(lv_label_get_text(ui.measurement_values[7]),"ms"));
 assert(fabs(strtod(lv_label_get_text(ui.measurement_values[7]),NULL)-1)<.00001);
 click(ui.measurement_choices[0]);assert(ui.adc_channel==0&&ui.trigger_channel==source);contents(&ui);
 assert(fabs(ui.measurement[0].vmax_v-(700*10.0/1023-5))<1e-12);
 assert(ui.adc_id==id&&ui.adc_last_seen_id==seen&&ui.capture_epoch==epoch&&ui.single_complete&&!ui.model.running&&ui.adc_display_start==start);
 assert(!memcmp(&cursor,&ui.cursor_result,sizeof(cursor)));lv_obj_update_layout(lv_screen_active());labels(lv_screen_active(),false);save(d,dir,"measurement-zh");
 click(ui.measurement_close_button);assert(lv_obj_has_flag(ui.measurement_panel,LV_OBJ_FLAG_HIDDEN));
 click(ui.settings_button);click(ui.language_button);click(ui.close_button);click(ui.measurement_button);
 assert(!strcmp(lv_label_get_text(ui.measurement_title),"MEASUREMENTS"));contents(&ui);lv_obj_update_layout(lv_screen_active());labels(lv_screen_active(),false);save(d,dir,"measurement-en");
 assert(!memcmp(cached,ui.measurement,sizeof(cached))&&ui.capture_epoch==epoch&&ui.adc_id==id&&ui.single_complete);
 /* Frozen records retain their sample rate across a requested profile change. */
 assert(scope_ui_set_conversion_hz(&ui,50000000));assert(ui.adc_rate==625000);
 assert(scope_ui_measurement_frequency(&ui,0,&f)&&f==1000);assert(strstr(lv_label_get_text(ui.measurement_note),"625000"));
 assert(!memcmp(cached,ui.measurement,sizeof(cached)));
 ui.adc_metrics[0].frequency_hz=NAN;assert(!scope_ui_measurement_frequency(&ui,0,&f)&&f==0);
 ui.adc_metrics[0].frequency_hz=INFINITY;assert(!scope_ui_measurement_frequency(&ui,0,&f));
 ui.adc_metrics[0].frequency_hz=0;assert(!scope_ui_measurement_frequency(&ui,0,&f));
 ui.adc_metrics[0].frequency_hz=-1;assert(!scope_ui_measurement_frequency(&ui,0,&f));
 ui.adc_metrics[0].frequency_hz=1e-100;assert(!scope_ui_measurement_frequency(&ui,0,&f));
 ui.adc_metrics[0].frequency_hz=ui.adc_rate/4.0+1;assert(!scope_ui_measurement_frequency(&ui,0,&f));
 ui.adc_metrics[0].frequency_hz=1000;ui.adc_metrics[0].frequency_valid=false;assert(!scope_ui_measurement_frequency(&ui,0,&f));
 ui.adc_metrics[0].frequency_valid=true;assert(!scope_ui_measurement_frequency(&ui,2,&f));assert(!scope_ui_measurement_frequency(&ui,0,NULL));
 click(ui.measurement_close_button);scope_ui_set_running(&ui,true);
 memcpy(invalid,record,sizeof(record));invalid[4095]|=0x80000000u;seen=ui.adc_last_seen_id;id=ui.adc_id;
 assert(!scope_ui_set_adc_record(&ui,invalid,4096,625000,1000,metrics));
 assert(!scope_ui_set_adc_record(&ui,invalid,4097,625000,1001,metrics));
 assert(ui.adc_last_seen_id==seen&&ui.adc_id==id&&!memcmp(cached,ui.measurement,sizeof(cached)));
 assert(scope_ui_set_adc_record(&ui,record,4096,625000,id+1,metrics));
 for(unsigned i=0;i<4096;i++)record[i]=512u|(563u<<16);
 assert(scope_ui_set_adc_record(&ui,record,4096,625000,id+2,metrics));assert(!scope_ui_measurement_frequency(&ui,0,&f));
 click(ui.measurement_button);assert(!strcmp(lv_label_get_text(ui.measurement_values[6]),"--")&&!strcmp(lv_label_get_text(ui.measurement_values[7]),"--"));
 click(ui.measurement_close_button);
 for(unsigned i=0;i<4096;i++)record[i]=(unsigned)(i%2?550:500)|((unsigned)(i%2?600:540)<<16);
 record[100]|=1024u;record[200]=(record[200]&0xffffu)|(1023u<<16);
 assert(scope_ui_set_adc_record(&ui,record,4096,625000,id+3,metrics));assert(ui.measurement[0].overrange&&ui.measurement[1].overrange);
 click(ui.measurement_button);assert(strstr(lv_label_get_text(ui.measurement_note),"OVERRANGE"));assert(!scope_ui_measurement_frequency(&ui,0,&f));lv_obj_update_layout(lv_screen_active());labels(lv_screen_active(),false);save(d,dir,"measurement-overrange");
 assert(scope_ui_select_source(&ui,2));assert(lv_obj_has_flag(ui.measurement_panel,LV_OBJ_FLAG_HIDDEN)&&lv_obj_has_flag(ui.measurement_button,LV_OBJ_FLAG_HIDDEN));
 assert(!scope_ui_show_measurements(&ui,true));assert(scope_ui_select_source(&ui,0));assert(scope_ui_show_measurements(&ui,true));
 lv_obj_send_event(ui.bars_button,LV_EVENT_CLICKED,NULL);
 assert(lv_obj_has_flag(ui.measurement_panel,LV_OBJ_FLAG_HIDDEN)&&lv_obj_has_flag(ui.measurement_button,LV_OBJ_FLAG_HIDDEN));
 assert(!scope_ui_show_measurements(&ui,true)&&scope_ui_show_measurements(&ui,false));
 timing_pages(d,dir);lv_deinit();puts("MEASUREMENT_UI_TEST_PASS");return 0;
}
