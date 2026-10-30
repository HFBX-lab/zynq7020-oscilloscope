#include "scope_ui.h"
#include "src/misc/lv_text_private.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t frame[1024*600],buffer[1024*60],record[4096],original[4096];
static lv_indev_data_t finger;
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *p){
 unsigned w=(unsigned)(a->x2-a->x1+1);for(int y=a->y1;y<=a->y2;y++)memcpy(frame+y*1024+a->x1,p+(y-a->y1)*w*4,w*4);lv_display_flush_ready(d);
}
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
static void save(lv_display_t *d,const char *dir,const char *name){
 if(!dir)return;
 lv_refr_now(d);char p[512];snprintf(p,sizeof(p),"%s/%s.bgrx",dir,name);FILE *f=fopen(p,"wb");assert(f);assert(fwrite(frame,1,sizeof(frame),f)==sizeof(frame));assert(!fclose(f));
}
static int expected(ScopeUI *ui,unsigned ch,unsigned pixel){
 const double scales[]={.1,.2,.5,1,2,5,10};double at=ui->adc_display_start+1250.0*pixel/(SCOPE_POINTS-1);unsigned n=(unsigned)at;
 double a=(record[n]>>(ch*16))&1023,b=(record[n+1]>>(ch*16))&1023;
 double v=(a+(at-n)*(b-a))*10/1023-5,s=scales[ui->channel_scale_index[ch]];
 return scope_voltage_to_plot(v+ui->channel_position_mdiv[ch]*s/1000,s);
}
static int chart_zero_y(ScopeUI *ui,unsigned ch){
 int32_t *points=lv_chart_get_y_array(ui->chart,ui->series),held=points[0];lv_point_t at;
 points[0]=ui->channel_position_mdiv[ch];lv_chart_get_point_pos_by_id(ui->chart,ui->series,0,&at);points[0]=held;
 return lv_obj_get_y(ui->chart)+at.y;
}
static void zero_badges(ScopeUI *ui){
 lv_obj_update_layout(lv_screen_active());
 int centers[2],target[2];bool shown[2];
 for(unsigned ch=0;ch<2;ch++){
  shown[ch]=ui->adc_record&&ui->adc_channel<2&&!ui->bars_on&&(ui->dual_display||ui->adc_channel==ch);
  assert(lv_obj_has_flag(ui->zero_marker[ch],LV_OBJ_FLAG_HIDDEN)==!shown[ch]);
  assert(lv_obj_has_flag(ui->zero_leader[ch],LV_OBJ_FLAG_HIDDEN)==!shown[ch]);
  if(!shown[ch])continue;
  assert(lv_obj_get_x(ui->zero_marker[ch])==2);
  assert(lv_obj_get_width(ui->zero_marker[ch])==16&&lv_obj_get_height(ui->zero_marker[ch])==18);
  assert(lv_obj_get_style_text_align(ui->zero_marker[ch],0)==LV_TEXT_ALIGN_CENTER);
  assert(!lv_obj_has_flag(ui->zero_marker[ch],LV_OBJ_FLAG_CLICKABLE));
  centers[ch]=lv_obj_get_y(ui->zero_marker[ch])+9;target[ch]=chart_zero_y(ui,ch);
  const lv_point_precise_t *leader=lv_line_get_points(ui->zero_leader[ch]);
  assert(leader&&lv_line_get_point_count(ui->zero_leader[ch])==3);
  assert(lv_obj_get_x(ui->zero_leader[ch])==0&&lv_obj_get_y(ui->zero_leader[ch])==0);
  assert(leader[0].x==18&&leader[0].y==centers[ch]);
  assert(leader[1].x==21&&leader[1].y==target[ch]&&leader[2].x==23&&leader[2].y==target[ch]);
  assert(!lv_obj_has_flag(ui->zero_leader[ch],LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE));
  assert(lv_obj_get_y(ui->zero_marker[ch])>=lv_obj_get_y(ui->chart));
  assert(lv_obj_get_y(ui->zero_marker[ch])+18<=lv_obj_get_y(ui->chart)+lv_obj_get_height(ui->chart));
 }
 if(shown[0]&&shown[1]){
  assert(abs(centers[0]-centers[1])>=20);
  if(abs(target[0]-target[1])>=20){assert(centers[0]==target[0]&&centers[1]==target[1]);}
  else{
   assert(abs((centers[0]+centers[1])-(target[0]+target[1]))<=1);
   assert(target[0]<=target[1]?centers[0]<centers[1]:centers[1]<centers[0]);
  }
 }else for(unsigned ch=0;ch<2;ch++)if(shown[ch])assert(centers[ch]==target[ch]);
}
int main(int argc,char **argv){
 const char *dir=argc>1?argv[1]:NULL;lv_init();lv_display_t *d=lv_display_create(1024,600);lv_display_set_color_format(d,LV_COLOR_FORMAT_XRGB8888);lv_display_set_buffers(d,buffer,NULL,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(d,flush);
 lv_indev_t *input=lv_indev_create();lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(input,pointer);
 for(unsigned i=0;i<4096;i++)record[i]=(unsigned)lround(512+80*sin(6.283185307179586*i/625))|((unsigned)lround(563+60*sin(6.283185307179586*(i+100)/625))<<16);
 memcpy(original,record,sizeof(record));ScopeUI ui;scope_ui_create(&ui);ScopeAdcMetrics m[2]={{1.6,.6,1000,true},{1.2,.7,1000,true}};
 assert(scope_ui_set_adc_record(&ui,record,4096,625000,1,m));assert(scope_ui_select_source(&ui,1));scope_ui_set_dual_display(&ui,true);assert(scope_ui_arm_single(&ui));assert(scope_ui_set_adc_record(&ui,record,4096,625000,2,m));
 assert(ui.channel_panel_button&&"Independent channel controls are missing");
 const int marker_cases[][2]={{0,0},{250,0},{0,250},{1500,-1500},{3500,3500},{-3500,-3500},{3500,-3500}};
 const char *marker_names[]={"markers-overlap","markers-nearby-ch1","markers-nearby-ch2","markers-separated","markers-top","markers-bottom","markers-extremes"};
 uint32_t marker_epoch=ui.capture_epoch;
 for(unsigned c=0;c<sizeof(marker_cases)/sizeof(marker_cases[0]);c++){
  assert(scope_ui_configure_channel(&ui,0,2,marker_cases[c][0]));assert(scope_ui_configure_channel(&ui,1,2,marker_cases[c][1]));
  zero_badges(&ui);save(d,dir,marker_names[c]);
  assert(ui.capture_epoch==marker_epoch&&ui.adc_id==2&&ui.single_complete&&!ui.model.running&&!memcmp(original,record,sizeof(record)));
 }
 assert(scope_ui_configure_channel(&ui,0,2,0)&&scope_ui_configure_channel(&ui,1,2,0));
 scope_ui_set_dual_display(&ui,false);zero_badges(&ui);save(d,dir,"markers-single-ch2");
 assert(scope_ui_select_source(&ui,0));zero_badges(&ui);
 assert(scope_ui_select_source(&ui,1));scope_ui_set_dual_display(&ui,true);zero_badges(&ui);
 uint32_t epoch=ui.capture_epoch;double start=ui.adc_display_start;int32_t held[SCOPE_POINTS];memcpy(held,ui.samples,sizeof(held));
 int32_t *other=lv_chart_get_y_array(ui.chart,ui.secondary_series);
 scope_ui_set_cursors(&ui,true);ScopeCursorResult cursor=ui.cursor_result;
 click(ui.channel_panel_button);assert(!lv_obj_has_flag(ui.channel_panel,LV_OBJ_FLAG_HIDDEN));
 click(ui.channel_choices[0]);click(ui.channel_adjust[1]);
 assert(ui.adc_channel==1&&ui.channel_scale_index[0]==3&&ui.scale_index==2&&ui.channel_scale_index[1]==2);
 assert(!memcmp(held,ui.samples,sizeof(held)));
 for(unsigned i=0;i<SCOPE_POINTS;i+=143)assert(other[i]==expected(&ui,0,i));
 int y=lv_obj_get_y(ui.zero_marker[0]);click(ui.channel_adjust[3]);
 assert(ui.channel_position_mdiv[0]==250&&lv_obj_get_y(ui.zero_marker[0])<y&&!memcmp(held,ui.samples,sizeof(held)));
 click(ui.channel_zero_button);assert(ui.channel_position_mdiv[0]==0);
 click(ui.channel_choices[1]);click(ui.channel_adjust[1]);click(ui.channel_adjust[2]);
 assert(ui.scale_index==3&&ui.channel_scale_index[1]==3&&ui.channel_position_mdiv[1]==-250);
 for(unsigned i=0;i<SCOPE_POINTS;i+=143){assert(ui.samples[i]==expected(&ui,1,i));assert(other[i]==expected(&ui,0,i));}
 assert(ui.adc_id==2&&ui.capture_epoch==epoch&&ui.single_complete&&!ui.model.running&&ui.adc_display_start==start);
 assert(!memcmp(original,record,sizeof(record))&&!memcmp(&cursor,&ui.cursor_result,sizeof(cursor)));
 /* Limit handling and the order of clipping: a large DC can move back in view. */
 for(unsigned i=0;i<20;i++){click(ui.channel_adjust[1]);}assert(ui.channel_scale_index[1]==6);
 for(unsigned i=0;i<20;i++){click(ui.channel_adjust[0]);}assert(ui.channel_scale_index[1]==0);
 for(unsigned i=0;i<40;i++){click(ui.channel_adjust[3]);}assert(ui.channel_position_mdiv[1]==3500);
 for(unsigned i=0;i<50;i++){click(ui.channel_adjust[2]);}assert(ui.channel_position_mdiv[1]==-3500);
 for(unsigned i=0;i<SCOPE_POINTS;i+=143)assert(ui.samples[i]==expected(&ui,1,i));
 click(ui.channel_zero_button);assert(ui.channel_position_mdiv[1]==0);
 for(unsigned i=0;i<2;i++){click(ui.channel_adjust[1]);}assert(ui.channel_scale_index[1]==2);
 for(unsigned i=0;i<6;i++)click(ui.channel_adjust[2]);
 click(ui.channel_choices[0]);for(unsigned i=0;i<6;i++)click(ui.channel_adjust[3]);
 lv_obj_update_layout(lv_screen_active());labels(lv_screen_active(),false);save(d,dir,"channel-settings-zh");
 click(ui.channel_close_button);save(d,dir,"channel-separated");
 if(dir){
  char path[512];snprintf(path,sizeof(path),"%s/channel-single-record.bin",dir);FILE *f=fopen(path,"wb");assert(f);assert(fwrite(record,1,sizeof(record),f)==sizeof(record));assert(!fclose(f));
  snprintf(path,sizeof(path),"%s/channel-frame-status.json",dir);f=fopen(path,"wb");assert(f);
  fprintf(f,"{\"view_start\":\"%.9f\",\"ch1_scale\":\"%d\",\"ch2_scale\":\"%d\",\"ch1_pos\":\"%d\",\"ch2_pos\":\"%d\"}\n",ui.adc_display_start,ui.channel_scale_index[0],ui.channel_scale_index[1],ui.channel_position_mdiv[0],ui.channel_position_mdiv[1]);assert(!fclose(f));
 }
 click(ui.settings_button);click(ui.language_button);click(ui.close_button);click(ui.channel_panel_button);
 lv_obj_update_layout(lv_screen_active());labels(lv_screen_active(),false);save(d,dir,"channel-settings-en");click(ui.channel_close_button);
 zero_badges(&ui);save(d,dir,"markers-separated-en");
 assert(scope_ui_select_source(&ui,0)&&ui.scale_index==3&&ui.channel_position_mdiv[0]==1500);
 assert(scope_ui_select_source(&ui,2)&&ui.scale_index==2);
 lv_obj_send_event(ui.adjust_buttons[3],LV_EVENT_CLICKED,NULL);assert(ui.scale_index==3);
 assert(lv_obj_has_flag(ui.channel_panel_button,LV_OBJ_FLAG_HIDDEN)&&lv_obj_has_flag(ui.zero_marker[0],LV_OBJ_FLAG_HIDDEN));
 zero_badges(&ui);
 assert(scope_ui_select_source(&ui,1)&&ui.scale_index==2&&ui.channel_position_mdiv[1]==-1500);
 assert(scope_ui_select_source(&ui,2)&&ui.scale_index==3);
 assert(scope_ui_select_source(&ui,1));lv_obj_send_event(ui.settings_button,LV_EVENT_CLICKED,NULL);lv_obj_send_event(ui.bars_button,LV_EVENT_CLICKED,NULL);assert(lv_obj_has_flag(ui.zero_marker[0],LV_OBJ_FLAG_HIDDEN));
 assert(lv_obj_has_flag(ui.channel_panel_button,LV_OBJ_FLAG_HIDDEN)&&lv_obj_has_flag(ui.channel_panel,LV_OBJ_FLAG_HIDDEN));
 zero_badges(&ui);
 lv_deinit();puts("CHANNEL_UI_TEST_PASS: independent scale/position, pointers, zero references, bounds, raw/cursor/frozen invariants, source/demo retention, fonts/layout");
}
