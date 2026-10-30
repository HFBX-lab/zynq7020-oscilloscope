#include "scope_ui.h"
#include "scope_acquisition.h"
#include "src/misc/lv_text_private.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static uint32_t buffer[1024*60], record[4096], other[4096];
static const double seconds_per_div[]={.00005,.0001,.0002,.0005,.001,.002,.005,.01,.02,.05,.1,.2,.5};
static const unsigned expected_rates[]={625000,625000,625000,625000,62500,62500,62500,6250,6250,6250,625,625,625};
static const unsigned expected_timeout[]={107,166,756,6654};
static const char *passbands[]={"0–62.5 kHz","0–6.25 kHz","0–625 Hz","0–62.5 Hz"};
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *p){(void)a;(void)p;lv_display_flush_ready(d);}
static void adjust(ScopeUI *ui,unsigned index){
    while(ui->timebase_index<(int)index)lv_obj_send_event(ui->adjust_buttons[1],LV_EVENT_CLICKED,NULL);
    while(ui->timebase_index>(int)index)lv_obj_send_event(ui->adjust_buttons[0],LV_EVENT_CLICKED,NULL);
    assert(ui->timebase_index==(int)index);
}
static ScopeRecordInfo info_for(ScopeUI *ui){
    ScopeCapturePlan p;assert(scope_acquisition_plan((unsigned)ui->timebase_index,&p)==0);
    ScopeRecordInfo info={ui->capture_epoch,p.rate_index,p.conversion_hz,3};return info;
}
static void check_labels(lv_obj_t *obj,bool hidden){
    hidden=hidden||lv_obj_has_flag(obj,LV_OBJ_FLAG_HIDDEN);
    if(lv_obj_check_type(obj,&lv_label_class)){
        const char *s=lv_label_get_text(obj);uint32_t i=0;
        while(s[i]){uint32_t c=lv_text_encoded_next(s,&i);if(c=='\n')continue;
            lv_font_glyph_dsc_t g={0};assert(lv_font_get_glyph_dsc(lv_obj_get_style_text_font(obj,LV_PART_MAIN),&g,c,0)&&!g.is_placeholder);}
        if(!hidden){lv_obj_t *parent=lv_obj_get_parent(obj);
            if(lv_obj_get_x(obj)<0||lv_obj_get_y(obj)<0)fprintf(stderr,"label out of bounds: %s at %ld,%ld\n",s,(long)lv_obj_get_x(obj),(long)lv_obj_get_y(obj));
            assert(lv_obj_get_x(obj)>=0&&lv_obj_get_y(obj)>=0);
            assert(lv_obj_get_x(obj)+lv_obj_get_width(obj)<=lv_obj_get_width(parent));
            assert(lv_obj_get_y(obj)+lv_obj_get_height(obj)<=lv_obj_get_height(parent));}
    }
    for(unsigned i=0;i<lv_obj_get_child_count(obj);i++)check_labels(lv_obj_get_child(obj,i),hidden);
}
int main(void){
    ScopeCapturePlan p;
    for(unsigned i=0;i<13;i++){
        assert(scope_acquisition_plan(i,&p)==0);assert(p.rate_hz==expected_rates[i]);
        assert(p.rate_index==(i<4?0:i<7?1:i<10?2:3));
        assert(p.count==4096&&p.conversion_hz==5000000&&p.timeout_ms==expected_timeout[p.rate_index]);
        assert(10*seconds_per_div[i]*p.rate_hz<=3125.000001);
    }
    assert(scope_acquisition_plan(13,&p)<0&&scope_acquisition_plan(0,NULL)<0);
    lv_init();lv_display_t *d=lv_display_create(1024,600);lv_display_set_color_format(d,LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(d,buffer,NULL,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(d,flush);
    for(unsigned i=0;i<4096;i++){unsigned c=(unsigned)lround(563+60*sin(6.283185307179586*(i+100)/625));record[i]=c|(c<<16);other[i]=512u|(512u<<16);}
    ScopeUI ui;scope_ui_create(&ui);lv_obj_update_layout(lv_screen_active());
    scope_ui_enable_adaptive_sampling(&ui,true);assert(ui.adaptive_sampling);
    uint32_t initial_epoch=ui.capture_epoch;
    lv_obj_send_event(ui.adjust_buttons[1],LV_EVENT_CLICKED,NULL);
    assert(ui.timebase_index==3&&ui.capture_epoch==initial_epoch+1);
    ScopeAdcMetrics m[2]={{1.2,.7,1000,true},{1.2,.7,1000,true}};unsigned id=1;
    scope_ui_set_dual_display(&ui,true);scope_ui_set_cursors(&ui,true);
    lv_obj_send_event(ui.settings_button,LV_EVENT_CLICKED,NULL);
    for(unsigned i=0;i<13;i++){
        adjust(&ui,i);ScopeRecordInfo info=info_for(&ui);
        assert(scope_ui_set_adc_record_info(&ui,record,4096,expected_rates[i],id++,m,&info));
        assert(ui.adc_triggered&&!ui.adc_waiting&&ui.model.running);
        assert(ui.samples[SCOPE_POINTS-1]!=LV_CHART_POINT_NONE&&ui.secondary_samples[SCOPE_POINTS-1]!=LV_CHART_POINT_NONE);
        assert(ui.adc_rate==expected_rates[i]&&ui.adc_info.rate_index==info.rate_index&&ui.adc_info.capture_epoch==info.capture_epoch);
        assert(strstr(lv_label_get_text(ui.footer_label),"数字通带"));
        assert(strstr(lv_label_get_text(ui.footer_label),passbands[info.rate_index]));
        assert(strstr(lv_label_get_text(ui.settings_note),"长时基降低采样率"));
        assert(strstr(lv_label_get_text(ui.settings_note),"较快信号请选更短时基"));
        assert(strstr(lv_label_get_text(ui.settings_note),"衰减 <0.5 dB"));
        assert(strstr(lv_label_get_text(ui.settings_note),"电压读数尚未校准"));
        assert(fabs(ui.adc_trigger_crossing-ui.adc_display_start-.2*10*seconds_per_div[i]*expected_rates[i])<1e-6);
        assert(ui.cursor_result.point_valid[0][0]&&ui.cursor_result.point_valid[1][1]);
        assert(fabs(ui.cursor_result.delta_s-5*seconds_per_div[i])<1e-9);
        assert(scope_ui_arm_single(&ui));info=info_for(&ui);
        assert(scope_ui_set_adc_record_info(&ui,record,4096,expected_rates[i],id++,m,&info));
        assert(ui.single_complete&&!ui.model.running);
        ScopeRecordInfo frozen=ui.adc_info;unsigned frozen_id=ui.adc_id;
        scope_ui_select_source(&ui,1-ui.adc_channel);scope_ui_configure_channel(&ui,1,3,500);
        assert(ui.single_complete&&ui.adc_id==frozen_id&&!memcmp(&frozen,&ui.adc_info,sizeof(frozen)));
        assert(!scope_ui_set_adc_record_info(&ui,other,4096,expected_rates[i],id++,m,&info));
        assert(ui.adc_record==record&&ui.adc_id==frozen_id&&!memcmp(&frozen,&ui.adc_info,sizeof(frozen)));
        scope_ui_set_running(&ui,true);
        lv_obj_update_layout(lv_screen_active());check_labels(lv_screen_active(),false);
        assert(lv_obj_get_y(ui.settings_note)+lv_obj_get_height(ui.settings_note)<=lv_obj_get_y(ui.close_button));
    }
    lv_obj_send_event(ui.close_button,LV_EVENT_CLICKED,NULL);
    assert(strstr(lv_label_get_text(ui.footer_label),"625 Sa/s"));
    lv_obj_send_event(ui.adjust_buttons[1],LV_EVENT_CLICKED,NULL);assert(ui.timebase_index==12);
    /* Changing from a fast frozen record to a long window keeps its true rate and gaps. */
    adjust(&ui,2);ScopeRecordInfo info=info_for(&ui);assert(scope_ui_set_adc_record_info(&ui,record,4096,625000,id++,m,&info));
    assert(scope_ui_arm_single(&ui));info=info_for(&ui);assert(scope_ui_set_adc_record_info(&ui,record,4096,625000,id++,m,&info));
    ScopeRecordInfo frozen=ui.adc_info;unsigned frozen_id=ui.adc_id;
    adjust(&ui,12);assert(!ui.single_complete&&!ui.adc_triggered&&ui.adc_id==frozen_id);
    assert(ui.adc_rate==625000&&!memcmp(&frozen,&ui.adc_info,sizeof(frozen))&&ui.samples[SCOPE_POINTS-1]==LV_CHART_POINT_NONE);
    assert(strstr(lv_label_get_text(ui.footer_label),"0–62.5 kHz")); /* Held record's passband, not requested rate. */
    assert(!ui.cursor_result.point_valid[0][1]);
    assert(scope_ui_arm_single(&ui)&&ui.single_armed&&ui.model.running);uint32_t epoch=ui.capture_epoch;
    adjust(&ui,11);assert(ui.single_armed&&ui.model.running&&ui.capture_epoch>epoch);
    unsigned seen=ui.adc_last_seen_id;info=info_for(&ui);ScopeRecordInfo bad=info;bad.capture_epoch--;
    assert(!scope_ui_set_adc_record_info(&ui,other,4096,625,id+100,m,&bad));assert(ui.adc_last_seen_id==seen);
    bad=info;bad.rate_index=0;assert(!scope_ui_set_adc_record_info(&ui,other,4096,625,id+100,m,&bad));
    bad=info;bad.filter_kind=0;assert(!scope_ui_set_adc_record_info(&ui,other,4096,625,id+100,m,&bad));
    bad=info;bad.conversion_hz=50000000;assert(!scope_ui_set_adc_record_info(&ui,other,4096,625,id+100,m,&bad));
    assert(!scope_ui_set_adc_record_info(&ui,other,4096,6250,id+100,m,&info));
    assert(!scope_ui_set_adc_record_info(&ui,other,4095,625,id+100,m,&info));
    assert(!scope_ui_set_adc_record_info(&ui,other,4096,625,id+100,m,NULL));
    assert(ui.adc_last_seen_id==seen&&ui.adc_id==frozen_id&&ui.single_armed);
    assert(scope_ui_set_adc_record_info(&ui,record,4096,625,id++,m,&info)&&ui.single_complete);
    scope_ui_set_running(&ui,true);adjust(&ui,8);info=info_for(&ui);
    assert(scope_ui_set_adc_record_info(&ui,record,4096,6250,id++,m,&info));
    assert(strstr(lv_label_get_text(ui.footer_label),"6.25 kSa/s"));
    lv_obj_send_event(ui.settings_button,LV_EVENT_CLICKED,NULL);lv_obj_send_event(ui.language_button,LV_EVENT_CLICKED,NULL);
    lv_obj_update_layout(lv_screen_active());check_labels(lv_screen_active(),false);
    assert(strstr(lv_label_get_text(ui.footer_label),"6.25 kSa/s"));
    assert(strstr(lv_label_get_text(ui.footer_label),"DIGITAL PASSBAND 0–625 Hz"));
    assert(strstr(lv_label_get_text(ui.settings_note),"Faster signals: use a shorter timebase."));
    assert(strstr(lv_label_get_text(ui.settings_note),"Passband loss <0.5 dB; volts uncalibrated."));
    assert(lv_obj_get_y(ui.settings_note)+lv_obj_get_height(ui.settings_note)<=lv_obj_get_y(ui.close_button));
    scope_ui_enable_adaptive_sampling(&ui,false);assert(!ui.adaptive_sampling&&ui.timebase_index<=6);
    lv_deinit();puts("LONG_TIMEBASE_TEST_PASS: 13 full windows, rates/deadlines, epoch/profile rejection, frozen metadata, short old view, single rearm, bilingual fonts/layout");return 0;
}
