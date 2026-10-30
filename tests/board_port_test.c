#include "scope_ui.h"
#include "portable.h"
#include "src/misc/lv_text_private.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static ScopeTouchQueue queue;
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *p) {(void)a;(void)p;lv_display_flush_ready(d);}
static void read_touch(lv_indev_t *d,lv_indev_data_t *out) {
    (void)d;ScopeTouchEvent e;bool more;
    scope_touch_next(&queue,&e,&more);
    out->point=(lv_point_t){e.x,e.y};out->state=e.pressed?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;
    out->continue_reading=more;
}
static void click(int x,int y) {
    assert(scope_touch_tap(&queue,x,y));
    for(unsigned i=0;i<12;i++){lv_tick_inc(10);lv_timer_handler();}
}
static void check_font(lv_obj_t *label) {
    const char *s=lv_label_get_text(label);uint32_t at=0;
    while(s[at]) {
        uint32_t cp=lv_text_encoded_next(s,&at);if(cp=='\n')continue;
        lv_font_glyph_dsc_t glyph={0};
        assert(lv_font_get_glyph_dsc(lv_obj_get_style_text_font(label,LV_PART_MAIN),&glyph,cp,0));
        assert(!glyph.is_placeholder);
    }
}
int main(void) {
    uint8_t src[]={0x33,0x22,0x11,0xff,0xa0,0xb0,0xc0,0x00};
    uint8_t out[8];memset(out,0xa5,sizeof(out));
    assert(scope_pack_rgb888(out+1,6,src,sizeof(src))==0);
    const uint8_t expected[]={0xa5,0x33,0x22,0x11,0xa0,0xb0,0xc0,0xa5};
    assert(memcmp(out,expected,sizeof(out))==0);
    assert(scope_pack_rgb888(out+1,5,src,sizeof(src))<0);
    assert(memcmp(out,expected,sizeof(out))==0);
    assert(scope_pack_rgb888(out+1,6,src,7)<0);
    assert(scope_pack_rgb888(NULL,6,src,8)<0);
    assert(scope_pack_rgb888(out,8,NULL,8)<0);
    assert(scope_pack_rgb888(NULL,0,NULL,0)==0);
    ScopeTouchQueue q={0};ScopeTouchEvent e;bool more;
    assert(!scope_touch_tap(&q,-1,0));assert(!scope_touch_tap(&q,1024,0));
    assert(!scope_touch_tap(&q,0,600));assert(q.count==0);
    for(int i=0;i<8;i++)assert(scope_touch_tap(&q,i,599));
    assert(!scope_touch_tap(&q,9,599) && q.count==16);
    for(int i=0;i<16;i++) {
        assert(scope_touch_next(&q,&e,&more));
        assert(e.x==i/2 && e.y==599 && e.pressed==(i%2==0));
        assert(more==(i<15));
    }
    assert(!scope_touch_next(&q,&e,&more) && !e.pressed && !more);
    static uint32_t buffer[SCOPE_WIDTH*60];
    lv_init();lv_display_t *display=lv_display_create(SCOPE_WIDTH,SCOPE_HEIGHT);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(display,buffer,NULL,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display,flush);
    lv_indev_t *input=lv_indev_create();lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(input,read_touch);
    ScopeUI ui;scope_ui_create(&ui);scope_ui_set_board_mode(&ui,true);
    assert(strstr(lv_label_get_text(ui.footer_label),"演示模式"));
    check_font(ui.footer_label);check_font(ui.subtitle_label);check_font(ui.settings_note);
    click(906,100);assert(!ui.model.running);
    click(970,32);click(500,196);
    assert(ui.language==SCOPE_EN && ui.board_mode);
    assert(strstr(lv_label_get_text(ui.footer_label),"DEMO MODE"));
    assert(!ui.model.running);
    click(500,196);assert(ui.language==SCOPE_ZH_CN);
    check_font(ui.footer_label);check_font(ui.settings_note);
    scope_ui_set_board_mode(&ui,false);
    assert(strstr(lv_label_get_text(ui.footer_label),"演示模式"));
    /* Real records must replace the synthetic plot and cannot stretch beyond
       their duration or let generator-frequency controls alter ADC sampling. */
    static uint32_t record[4096];
    for(unsigned i=0;i<4096;i++)record[i]=512u|(1023u<<16);
    ScopeAdcMetrics metrics[2]={{1.0,.35,1000,true},{0,5,0,false}};
    scope_ui_set_running(&ui,true);
    scope_ui_set_adc_record(&ui,record,4096,625000,7,metrics);
    assert(ui.adc_channel==0&&ui.samples[0]==10);
    assert(strstr(lv_label_get_text(ui.channel_label),"CH1"));
    assert(strstr(lv_label_get_text(ui.readout_label),"1.000"));
    assert(!lv_obj_has_state(ui.adjust_buttons[4],LV_STATE_DISABLED));
    lv_obj_send_event(ui.adjust_buttons[5],LV_EVENT_CLICKED,NULL);assert(ui.trigger_level_mv==600);
    lv_obj_send_event(ui.adjust_buttons[4],LV_EVENT_CLICKED,NULL);assert(ui.trigger_level_mv==500);
    lv_obj_send_event(ui.wave_buttons[1],LV_EVENT_CLICKED,NULL);
    assert(ui.adc_channel==1&&ui.samples[0]==4000);
    assert(strcmp(lv_label_get_text(ui.readout_label),"--")==0);
    ui.timebase_index=6;
    scope_ui_set_adc_record(&ui,record,4096,625000,8,metrics);
    assert(ui.samples[SCOPE_POINTS-1]==LV_CHART_POINT_NONE);
    check_font(ui.channel_label);check_font(ui.state_label);check_font(ui.metric_titles[1]);
    lv_obj_send_event(ui.wave_buttons[2],LV_EVENT_CLICKED,NULL);
    assert(ui.adc_channel==2&&!lv_obj_has_state(ui.adjust_buttons[4],LV_STATE_DISABLED));
    lv_deinit();
    puts("PASS: RGB888 packing/guards, pointer queue, LVGL language/context, ADC channels/coverage and trigger-level controls");
    return 0;
}
