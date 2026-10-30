#include "scope_ui.h"
#include "portable.h"
#include "src/misc/lv_text_private.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static uint32_t framebuffer[1024*600],buffer[1024*60],record[4096],flat[4096];
static ScopeTouchQueue queue;
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *p){
    unsigned width=(unsigned)(a->x2-a->x1+1);
    for(int y=a->y1;y<=a->y2;y++)memcpy(framebuffer+y*1024+a->x1,p+(y-a->y1)*width*4,width*4);
    lv_display_flush_ready(d);
}
static void read_pointer(lv_indev_t *d,lv_indev_data_t *out){
    (void)d;ScopeTouchEvent e;bool more;scope_touch_next(&queue,&e,&more);
    out->point=(lv_point_t){e.x,e.y};out->state=e.pressed?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;out->continue_reading=more;
}
static void click(lv_obj_t *o){
    lv_obj_update_layout(lv_screen_active());lv_area_t a;lv_obj_get_coords(o,&a);
    assert(scope_touch_tap(&queue,(a.x1+a.x2)/2,(a.y1+a.y2)/2));
    for(unsigned i=0;i<12;i++){lv_tick_inc(10);lv_timer_handler();}
}
static lv_obj_t *find_button(lv_obj_t *o,const char *text){
    if(lv_obj_check_type(o,&lv_button_class)&&lv_obj_get_child_count(o)&&
       !strcmp(lv_label_get_text(lv_obj_get_child(o,0)),text))return o;
    for(unsigned i=0;i<lv_obj_get_child_count(o);i++){
        lv_obj_t *found=find_button(lv_obj_get_child(o,i),text);if(found)return found;
    }
    return NULL;
}
static void labels(lv_obj_t *o,bool hidden){
    hidden=hidden||lv_obj_has_flag(o,LV_OBJ_FLAG_HIDDEN);
    if(lv_obj_check_type(o,&lv_label_class)){
        const char *s=lv_label_get_text(o);uint32_t i=0;
        while(s[i]){uint32_t c=lv_text_encoded_next(s,&i);if(c=='\n')continue;lv_font_glyph_dsc_t g={0};
            assert(lv_font_get_glyph_dsc(lv_obj_get_style_text_font(o,LV_PART_MAIN),&g,c,0)&&!g.is_placeholder);}
        if(!hidden){lv_obj_t *p=lv_obj_get_parent(o);
            assert(lv_obj_get_x(o)>=0&&lv_obj_get_y(o)>=0);
            assert(lv_obj_get_x(o)+lv_obj_get_width(o)<=lv_obj_get_width(p));
            assert(lv_obj_get_y(o)+lv_obj_get_height(o)<=lv_obj_get_height(p));}
    }
    for(unsigned i=0;i<lv_obj_get_child_count(o);i++)labels(lv_obj_get_child(o,i),hidden);
}
static void save(lv_display_t *d,const char *dir,const char *name){
    if(!dir)return;
    lv_refr_now(d);char path[512];snprintf(path,sizeof(path),"%s/%s.bgrx",dir,name);
    FILE *f=fopen(path,"wb");assert(f);assert(fwrite(framebuffer,1,sizeof(framebuffer),f)==sizeof(framebuffer));assert(!fclose(f));
}
static void check_sample(const ScopeUI *ui,const int32_t *other,unsigned pixel){
    double at=ui->adc_display_start+(double)pixel/(SCOPE_POINTS-1)*1250;
    unsigned n=(unsigned)at;
    for(unsigned ch=0;ch<2;ch++){
        double a=(record[n]>>(16*ch))&1023,b=(record[n+1]>>(16*ch))&1023;
        int expected=scope_voltage_to_plot((a+(at-n)*(b-a))*10/1023-5,.5);
        assert((ch==ui->adc_channel?ui->samples[pixel]:other[pixel])==expected);
    }
}
int main(int argc,char **argv){
    const char *preview=argc>1?argv[1]:NULL;
    lv_init();lv_display_t *d=lv_display_create(1024,600);lv_display_set_color_format(d,LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(d,buffer,NULL,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(d,flush);
    lv_indev_t *pointer=lv_indev_create();lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(pointer,read_pointer);
    for(unsigned i=0;i<4096;i++){
        unsigned c1=(unsigned)lround(563+40*sin(6.283185307179586*i/625));
        unsigned c2=(unsigned)lround(563+65*sin(6.283185307179586*(i+157)/625));
        record[i]=c1|(c2<<16);flat[i]=512u|(512u<<16);
    }
    ScopeUI ui;scope_ui_create(&ui);ScopeAdcMetrics m[2]={{.8,.5,1000,true},{1.3,.7,1000,true}};
    assert(scope_ui_set_adc_record(&ui,record,4096,625000,1,m));
    lv_chart_series_t *other_series=NULL;
    for(lv_chart_series_t *s=lv_chart_get_series_next(ui.chart,NULL);s;s=lv_chart_get_series_next(ui.chart,s))if(s!=ui.series){assert(!other_series);other_series=s;}
    assert(other_series&&"Dual display needs a second series from the same record");
    int32_t *other=lv_chart_get_y_array(ui.chart,other_series);
    for(unsigned i=0;i<SCOPE_POINTS;i++)assert(other[i]==LV_CHART_POINT_NONE);
    lv_obj_t *toggle=find_button(lv_screen_active(),"通道显示：单通道");assert(toggle);
    uint32_t epoch=ui.capture_epoch;double start=ui.adc_display_start;
    click(ui.settings_button);click(toggle);
    assert(ui.capture_epoch==epoch&&ui.adc_id==1&&ui.adc_display_start==start&&ui.model.running);
    assert(!strcmp(lv_label_get_text(lv_obj_get_child(toggle,0)),"通道显示：双通道"));
    assert(lv_color_to_u32(lv_chart_get_series_color(ui.chart,ui.series))==lv_color_to_u32(lv_color_hex(0x40e0c0)));
    assert(lv_color_to_u32(lv_chart_get_series_color(ui.chart,other_series))==lv_color_to_u32(lv_color_hex(0xffc857)));
    for(unsigned i=0;i<SCOPE_POINTS;i+=143)check_sample(&ui,other,i);
    lv_obj_update_layout(lv_screen_active());labels(lv_screen_active(),false);save(d,preview,"dual-settings-zh");
    click(ui.language_button);lv_obj_update_layout(lv_screen_active());labels(lv_screen_active(),false);save(d,preview,"dual-settings-en");
    assert(find_button(lv_screen_active(),"CHANNEL DISPLAY: DUAL"));
    click(ui.close_button);save(d,preview,"dual-ch1-trigger");
    assert(scope_ui_select_source(&ui,1));assert(scope_ui_set_adc_record(&ui,record,4096,625000,2,m));
    assert(fabs(ui.adc_trigger_crossing-ui.adc_display_start-250)<1e-6);
    assert(!strcmp(lv_label_get_text(ui.vpp_label),"1.300 V"));
    assert(lv_color_to_u32(lv_chart_get_series_color(ui.chart,ui.series))==lv_color_to_u32(lv_color_hex(0xffc857)));
    for(unsigned i=0;i<SCOPE_POINTS;i+=143)check_sample(&ui,other,i);
    save(d,preview,"dual-ch2-trigger");
    assert(scope_ui_arm_single(&ui));assert(scope_ui_set_adc_record(&ui,record,4096,625000,3,m));
    assert(ui.single_complete&&!ui.model.running);
    int32_t held[2][SCOPE_POINTS];memcpy(held[0],ui.samples,sizeof(ui.samples));memcpy(held[1],other,sizeof(ui.samples));
    epoch=ui.capture_epoch;start=ui.adc_display_start;
    click(ui.settings_button);click(toggle);
    assert(ui.single_complete&&!ui.model.running&&ui.adc_id==3&&ui.capture_epoch==epoch&&ui.adc_display_start==start);
    for(unsigned i=0;i<SCOPE_POINTS;i++)assert(other[i]==LV_CHART_POINT_NONE);
    click(toggle);assert(!memcmp(held[0],ui.samples,sizeof(ui.samples))&&!memcmp(held[1],other,sizeof(ui.samples)));
    click(ui.close_button);save(d,preview,"dual-single-frozen");
    lv_obj_send_event(ui.adjust_buttons[1],LV_EVENT_CLICKED,NULL);lv_obj_send_event(ui.adjust_buttons[1],LV_EVENT_CLICKED,NULL);
    assert(ui.timebase_index==4&&ui.samples[SCOPE_POINTS-1]==LV_CHART_POINT_NONE&&other[SCOPE_POINTS-1]==LV_CHART_POINT_NONE);
    lv_obj_send_event(ui.adjust_buttons[0],LV_EVENT_CLICKED,NULL);lv_obj_send_event(ui.adjust_buttons[0],LV_EVENT_CLICKED,NULL);
    scope_ui_set_running(&ui,true);assert(scope_ui_configure_trigger(&ui,true,true,4500));
    assert(!scope_ui_set_adc_record(&ui,flat,4096,625000,4,m));
    assert(ui.adc_id==3&&!memcmp(held[0],ui.samples,sizeof(ui.samples))&&!memcmp(held[1],other,sizeof(ui.samples)));
    assert(scope_ui_select_source(&ui,2));for(unsigned i=0;i<SCOPE_POINTS;i++)assert(other[i]==LV_CHART_POINT_NONE);
    click(ui.settings_button);assert(lv_obj_has_state(toggle,LV_STATE_DISABLED));click(toggle);click(ui.close_button);
    assert(scope_ui_select_source(&ui,1));assert(scope_ui_configure_trigger(&ui,false,true,500));
    assert(scope_ui_set_adc_record(&ui,record,4096,625000,5,m));
    for(unsigned i=0;i<SCOPE_POINTS;i+=143)check_sample(&ui,other,i);
    lv_deinit();puts("DUAL_CHANNEL_UI_TEST_PASS: same-record phase, trigger source, fixed colors, pointer toggle, frozen state, NORMAL hold, SIM isolation, fonts/layout");return 0;
}
