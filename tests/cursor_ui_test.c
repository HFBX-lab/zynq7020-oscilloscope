#include "scope_ui.h"
#include "src/misc/lv_text_private.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static uint32_t framebuffer[1024*600],buffer[1024*60],record[4096];
static lv_indev_data_t finger;
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *p){
    unsigned w=(unsigned)(a->x2-a->x1+1);
    for(int y=a->y1;y<=a->y2;y++)memcpy(framebuffer+y*1024+a->x1,p+(y-a->y1)*w*4,w*4);
    lv_display_flush_ready(d);
}
static void pointer(lv_indev_t *d,lv_indev_data_t *out){(void)d;*out=finger;}
static void point(int x,int y,bool down){
    finger.point=(lv_point_t){x,y};finger.state=down?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;
    for(unsigned i=0;i<6;i++){lv_tick_inc(10);lv_timer_handler();}
}
static void click(lv_obj_t *o){lv_obj_update_layout(lv_screen_active());lv_area_t a;lv_obj_get_coords(o,&a);point((a.x1+a.x2)/2,(a.y1+a.y2)/2,true);point((a.x1+a.x2)/2,(a.y1+a.y2)/2,false);}
static void labels(lv_obj_t *o,bool hidden){
    hidden=hidden||lv_obj_has_flag(o,LV_OBJ_FLAG_HIDDEN);
    if(lv_obj_check_type(o,&lv_label_class)){
        const char *s=lv_label_get_text(o);uint32_t i=0;
        while(s[i]){uint32_t c=lv_text_encoded_next(s,&i);if(c=='\n')continue;lv_font_glyph_dsc_t g={0};assert(lv_font_get_glyph_dsc(lv_obj_get_style_text_font(o,LV_PART_MAIN),&g,c,0)&&!g.is_placeholder);}
        if(!hidden){lv_obj_t *p=lv_obj_get_parent(o);assert(lv_obj_get_x(o)>=0&&lv_obj_get_y(o)>=0);assert(lv_obj_get_x(o)+lv_obj_get_width(o)<=lv_obj_get_width(p));assert(lv_obj_get_y(o)+lv_obj_get_height(o)<=lv_obj_get_height(p));}
    }
    for(unsigned i=0;i<lv_obj_get_child_count(o);i++)labels(lv_obj_get_child(o,i),hidden);
}
static void save(lv_display_t *d,const char *dir,const char *name){
    if(!dir)return;
    lv_refr_now(d);char path[512];snprintf(path,sizeof(path),"%s/%s.bgrx",dir,name);FILE *f=fopen(path,"wb");assert(f);assert(fwrite(framebuffer,1,sizeof(framebuffer),f)==sizeof(framebuffer));assert(!fclose(f));
}
int main(int argc,char **argv){
    const char *preview=argc>1?argv[1]:NULL;
    lv_init();lv_display_t *d=lv_display_create(1024,600);lv_display_set_color_format(d,LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(d,buffer,NULL,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(d,flush);
    lv_indev_t *indev=lv_indev_create();lv_indev_set_type(indev,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(indev,pointer);
    for(unsigned i=0;i<4096;i++)record[i]=(unsigned)lround(563+45*sin(6.283185307179586*i/625))|((unsigned)lround(563+60*sin(6.283185307179586*(i+100)/625))<<16);
    uint32_t original[4096];memcpy(original,record,sizeof(record));
    ScopeUI ui;scope_ui_create(&ui);ScopeAdcMetrics m[2]={{.9,.5,1000,true},{1.2,.7,1000,true}};
    assert(scope_ui_set_adc_record(&ui,record,4096,625000,1,m));scope_ui_set_dual_display(&ui,true);assert(scope_ui_select_source(&ui,1));
    assert(scope_ui_arm_single(&ui));assert(scope_ui_set_adc_record(&ui,record,4096,625000,2,m));
    uint32_t epoch=ui.capture_epoch;double start=ui.adc_display_start;
    assert(!ui.cursors_on&&ui.single_complete);click(ui.cursor_button);
    assert(ui.cursors_on&&ui.cursor_result.delta_valid[1]&&fabs(ui.cursor_result.delta_s-.001)<1e-12);
    assert(fabs(ui.cursor_result.reciprocal_hz-1000)<1e-6);
    assert(!strcmp(lv_label_get_text(ui.vpp_label),"Δt +1.000 ms"));
    lv_obj_update_layout(lv_screen_active());labels(lv_screen_active(),false);save(d,preview,"cursor-zh");
    /* Drag A across B and beyond both chart edges; the grabbed cursor stays A. */
    point(210,260,true);point(650,260,true);assert(ui.cursor_positions[0]>800&&ui.cursor_positions[1]==750);
    point(1000,260,true);assert(ui.cursor_positions[0]==1000);
    point(0,260,true);assert(ui.cursor_positions[0]==0);point(0,260,false);assert(ui.cursor_dragging==-1);
    assert(ui.capture_epoch==epoch&&ui.adc_id==2&&ui.single_complete&&!ui.model.running&&ui.adc_display_start==start);
    assert(!memcmp(original,record,sizeof(record)));
    assert(scope_ui_move_cursors(&ui,500,500)&&!ui.cursor_result.reciprocal_valid&&!strcmp(lv_label_get_text(ui.readout_label),"--"));
    assert(!scope_ui_move_cursors(&ui,1001,500));
    assert(scope_ui_move_cursors(&ui,250,750));
    click(ui.settings_button);click(ui.language_button);click(ui.close_button);
    lv_obj_update_layout(lv_screen_active());labels(lv_screen_active(),false);save(d,preview,"cursor-en");
    click(ui.cursor_button);assert(!ui.cursors_on&&ui.capture_epoch==epoch&&ui.single_complete);
    assert(!strcmp(lv_label_get_text(ui.vpp_label),"1.200 V"));
    click(ui.cursor_button);lv_obj_send_event(ui.adjust_buttons[1],LV_EVENT_CLICKED,NULL);
    assert(ui.timebase_index==3&&fabs(ui.cursor_result.delta_s-.0025)<1e-12);
    lv_obj_send_event(ui.adjust_buttons[1],LV_EVENT_CLICKED,NULL);assert(!ui.cursor_result.delta_valid[1]);
    assert(strstr(lv_label_get_text(ui.rms_label),"--"));
    lv_obj_send_event(ui.adjust_buttons[0],LV_EVENT_CLICKED,NULL);lv_obj_send_event(ui.adjust_buttons[0],LV_EVENT_CLICKED,NULL);
    assert(scope_ui_select_source(&ui,0)&&ui.cursor_result.delta_valid[0]);
    assert(scope_ui_select_source(&ui,2));assert(lv_obj_has_flag(ui.cursor_line[0],LV_OBJ_FLAG_HIDDEN)&&lv_obj_has_state(ui.cursor_button,LV_STATE_DISABLED));
    assert(scope_ui_select_source(&ui,1)&&ui.cursors_on);
    lv_obj_send_event(ui.settings_button,LV_EVENT_CLICKED,NULL);lv_obj_send_event(ui.bars_button,LV_EVENT_CLICKED,NULL);assert(lv_obj_has_flag(ui.cursor_line[0],LV_OBJ_FLAG_HIDDEN));
    lv_deinit();puts("CURSOR_UI_TEST_PASS: pointer drag/clamp, same-point, frozen ID/epoch, timebase, source/SIM/pattern guards, fonts/layout");
}
