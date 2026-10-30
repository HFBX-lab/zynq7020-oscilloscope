#include "scope_ui.h"
#include "src/misc/lv_text_private.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ScopeUI ui;
static uint32_t buffer[1024*60],frame[1024*600],first[4096],later[4096];
static lv_indev_data_t finger;
static void flush(lv_display_t *display,const lv_area_t *area,uint8_t *pixels){
    unsigned width=(unsigned)(area->x2-area->x1+1);
    for(int y=area->y1;y<=area->y2;y++)memcpy(frame+y*1024+area->x1,pixels+(y-area->y1)*width*4,width*4);
    lv_display_flush_ready(display);
}
static void pointer(lv_indev_t *device,lv_indev_data_t *out){(void)device;*out=finger;}
static void point(int x,int y,bool down){
    finger.point=(lv_point_t){x,y};finger.state=down?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;
    for(unsigned i=0;i<6;i++){lv_tick_inc(10);lv_timer_handler();}
}
static void click(lv_obj_t *object){
    assert(object);lv_obj_update_layout(lv_screen_active());lv_area_t area;lv_obj_get_coords(object,&area);
    point((area.x1+area.x2)/2,(area.y1+area.y2)/2,true);point((area.x1+area.x2)/2,(area.y1+area.y2)/2,false);
}
static void labels(lv_obj_t *object,bool hidden){
    hidden=hidden||lv_obj_has_flag(object,LV_OBJ_FLAG_HIDDEN);
    if(lv_obj_check_type(object,&lv_label_class)){
        const char *text=lv_label_get_text(object);uint32_t at=0;
        while(text[at]){uint32_t c=lv_text_encoded_next(text,&at);if(c=='\n')continue;lv_font_glyph_dsc_t glyph={0};
            if(!lv_font_get_glyph_dsc(lv_obj_get_style_text_font(object,LV_PART_MAIN),&glyph,c,0)||glyph.is_placeholder){
                fprintf(stderr,"MISSING GLYPH U+%04x: %s\n",(unsigned)c,text);assert(false);
            }
        }
        if(!hidden){lv_obj_t *parent=lv_obj_get_parent(object);
            if(lv_obj_get_x(object)<0||lv_obj_get_y(object)<0||lv_obj_get_x(object)+lv_obj_get_width(object)>lv_obj_get_width(parent)||lv_obj_get_y(object)+lv_obj_get_height(object)>lv_obj_get_height(parent))
                fprintf(stderr,"BAD LABEL LAYOUT: %s\n",text);
            assert(lv_obj_get_x(object)>=0&&lv_obj_get_y(object)>=0);
            assert(lv_obj_get_x(object)+lv_obj_get_width(object)<=lv_obj_get_width(parent));
            assert(lv_obj_get_y(object)+lv_obj_get_height(object)<=lv_obj_get_height(parent));
        }
    }
    for(unsigned i=0;i<lv_obj_get_child_count(object);i++)labels(lv_obj_get_child(object,i),hidden);
}
static void save(lv_display_t *display,const char *directory,const char *name){
    lv_obj_update_layout(lv_screen_active());labels(lv_screen_active(),false);lv_refr_now(display);
    if(directory){char path[512];snprintf(path,sizeof(path),"%s/%s.bgrx",directory,name);FILE *file=fopen(path,"wb");
        assert(file&&fwrite(frame,1,sizeof(frame),file)==sizeof(frame));assert(!fclose(file));}
}
static bool feed(uint32_t *words,unsigned id){
    ScopeCapturePlan plan;ScopeHardwareConfig config;
    assert(!scope_ui_capture_plan(&ui,&plan)&&!scope_ui_hardware_config(&ui,&config));
    for(unsigned i=0;i<4096;i++)words[i]=512u|((unsigned)lround(563+60*sin(6.283185307179586*i*50000/plan.rate_hz))<<16);
    words[config.pre_samples-1]=512u|(562u<<16);words[config.pre_samples]=512u|(564u<<16);
    ScopeRecordInfo info={ui.capture_epoch,plan.rate_index,plan.conversion_hz,plan.filter_kind};
    ScopeHardwareTrigger trigger={true,false,config.pre_samples};
    ScopeAdcMetrics metrics[2]={{.02,.07,0,false},{1.2,.7,50000,true}};
    return scope_ui_set_hardware_adc_record(&ui,words,4096,plan.rate_hz,id,metrics,&info,&trigger);
}
typedef struct {
    const uint32_t *pointer;unsigned count,rate,id,seen,epoch,source,trigger_source;
    ScopeRecordInfo info;ScopeAdcMetrics metrics[2];ScopeMeasurement measurement[2];
    bool running,single,complete,matched,waiting,wave_valid,settings_valid;
    int scales[2],positions[2],level;bool normal,rising;
    double start,crossing;uint32_t raw,wave,settings;
} Capture;
static Capture capture(void){
    Capture out={0};out.pointer=ui.adc_record;out.count=ui.adc_count;out.rate=ui.adc_rate;out.id=ui.adc_id;
    out.seen=ui.adc_last_seen_id;out.epoch=ui.capture_epoch;out.source=ui.adc_channel;out.trigger_source=ui.trigger_channel;
    out.info=ui.adc_info;memcpy(out.metrics,ui.adc_metrics,sizeof(out.metrics));memcpy(out.measurement,ui.measurement,sizeof(out.measurement));
    out.running=ui.model.running;out.single=ui.single_armed;out.complete=ui.single_complete;out.matched=ui.adc_triggered;out.waiting=ui.adc_waiting;
    out.wave_valid=ui.memory_valid;out.settings_valid=ui.settings_valid;memcpy(out.scales,ui.channel_scale_index,sizeof(out.scales));memcpy(out.positions,ui.channel_position_mdiv,sizeof(out.positions));
    out.level=ui.trigger_level_mv;out.normal=ui.trigger_normal;out.rising=ui.trigger_rising;out.start=ui.adc_display_start;out.crossing=ui.adc_trigger_crossing;
    out.raw=scope_record_crc32(ui.adc_record,ui.adc_count*4u);out.wave=scope_record_crc32(ui.memory_blob,sizeof(ui.memory_blob));out.settings=scope_record_crc32(ui.settings_blob,sizeof(ui.settings_blob));return out;
}
static void held(const Capture *before){Capture after=capture();assert(!memcmp(before,&after,sizeof(after)));}
static uint32_t spectrum_crc(void){return scope_record_crc32(&ui.spectrum_result,sizeof(ui.spectrum_result))^scope_record_crc32(ui.spectrum_words,sizeof(ui.spectrum_words));}
static void reject_all(void){
    Capture before=capture();unsigned seq=ui.spectrum_sequence,ch=ui.spectrum_channel;bool mean=ui.spectrum_remove_mean;
    ScopeSpectrumWindow window=ui.spectrum_window;uint32_t crc=spectrum_crc();
    assert(!scope_ui_spectrum_update(&ui)&&!scope_ui_show_spectrum(&ui,true));
    assert(!scope_ui_spectrum_channel(&ui,1-ch)&&!scope_ui_spectrum_window(&ui,window==SCOPE_SPECTRUM_HANN?SCOPE_SPECTRUM_RECT:SCOPE_SPECTRUM_HANN)&&!scope_ui_spectrum_mean(&ui,!mean));
    held(&before);assert(seq==ui.spectrum_sequence&&crc==spectrum_crc()&&ch==ui.spectrum_channel&&mean==ui.spectrum_remove_mean&&window==ui.spectrum_window);
}
int main(int argc,char **argv){
    const char *directory=argc>1?argv[1]:NULL;lv_init();lv_display_t *display=lv_display_create(1024,600);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_XRGB8888);lv_display_set_buffers(display,buffer,NULL,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(display,flush);
    lv_indev_t *input=lv_indev_create();lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(input,pointer);
    scope_ui_create(&ui);assert(ui.spectrum_button&&ui.spectrum_panel&&"Spectrum UI is missing");
    assert(ui.spectrum_window==SCOPE_SPECTRUM_HANN&&ui.spectrum_remove_mean&&ui.spectrum_auto_range&&!ui.spectrum_valid);
    reject_all();assert(!scope_ui_spectrum_range(&ui,false));
    scope_ui_enable_adaptive_sampling(&ui,true);assert(scope_ui_set_conversion_hz(&ui,50000000));scope_ui_enable_hardware_trigger(&ui,true);
    ui.timebase_index=6;assert(scope_ui_select_source(&ui,1)&&scope_ui_select_trigger_source(&ui,1));
    assert(feed(first,1)&&scope_ui_arm_single(&ui)&&feed(first,2)&&ui.single_complete&&!ui.model.running);
    assert(scope_ui_memory_save(&ui)&&scope_ui_settings_save(&ui));Capture frozen=capture();
    click(ui.measurement_button);click(ui.spectrum_button);held(&frozen);
    assert(!lv_obj_has_flag(ui.spectrum_panel,LV_OBJ_FLAG_HIDDEN)&&ui.spectrum_valid&&ui.spectrum_sequence==1);
    assert(ui.spectrum_channel==1&&ui.spectrum_id==2&&ui.spectrum_result.rate_hz==6250000&&ui.spectrum_conversion==50000000&&ui.spectrum_filter==3);
    assert(ui.spectrum_result.peak_valid&&fabs(ui.spectrum_result.peak_hz-50000)<100);
    assert(!memcmp(ui.spectrum_words,first,sizeof(first))&&ui.spectrum_raw_crc==frozen.raw);
    save(display,directory,"spectrum-frozen-hann-zh");
    unsigned seq=ui.spectrum_sequence;uint32_t crc=spectrum_crc();assert(scope_ui_spectrum_range(&ui,false));
    assert(ui.spectrum_end_bin==2048&&seq==ui.spectrum_sequence&&crc==spectrum_crc());held(&frozen);save(display,directory,"spectrum-full-zh");
    click(ui.spectrum_range_button);assert(ui.spectrum_auto_range&&ui.spectrum_end_bin<2048&&seq==ui.spectrum_sequence);held(&frozen);
    click(ui.spectrum_choices[0]);assert(ui.spectrum_channel==0&&!ui.spectrum_result.peak_valid&&ui.adc_channel==1&&ui.trigger_channel==1);held(&frozen);save(display,directory,"spectrum-empty-ch1-zh");
    click(ui.spectrum_choices[1]);click(ui.spectrum_window_button);assert(ui.spectrum_window==SCOPE_SPECTRUM_RECT);held(&frozen);
    click(ui.spectrum_mean_button);assert(!ui.spectrum_remove_mean);held(&frozen);save(display,directory,"spectrum-rect-dc-zh");
    click(ui.spectrum_mean_button);click(ui.spectrum_window_button);assert(ui.spectrum_remove_mean&&ui.spectrum_window==SCOPE_SPECTRUM_HANN);
    click(ui.spectrum_close_button);assert(lv_obj_has_flag(ui.spectrum_panel,LV_OBJ_FLAG_HIDDEN)&&!lv_obj_has_flag(ui.measurement_panel,LV_OBJ_FLAG_HIDDEN));held(&frozen);
    click(ui.measurement_close_button);click(ui.settings_button);click(ui.language_button);click(ui.close_button);click(ui.measurement_button);click(ui.spectrum_button);
    save(display,directory,"spectrum-hann-en");assert(ui.language==SCOPE_EN);
    /* Explicitly updating analysis does not resume a frozen acquisition. */
    frozen=capture();seq=ui.spectrum_sequence;click(ui.spectrum_update_button);held(&frozen);assert(ui.spectrum_sequence==seq+1);
    assert(!scope_ui_spectrum_channel(&ui,2)&&!scope_ui_spectrum_window(&ui,(ScopeSpectrumWindow)2));held(&frozen);
    /* A late live record cannot re-label or overwrite the displayed spectrum. */
    scope_ui_set_running(&ui,true);seq=ui.spectrum_sequence;unsigned saved_id=ui.spectrum_id,saved_rate=ui.spectrum_result.rate_hz;crc=spectrum_crc();
    ui.timebase_index=5;assert(feed(later,3)&&ui.adc_rate==50000000);scope_ui_step(&ui,.05);
    assert(ui.spectrum_sequence==seq&&ui.spectrum_id==saved_id&&ui.spectrum_result.rate_hz==saved_rate&&spectrum_crc()==crc);
    Capture running=capture();click(ui.spectrum_update_button);held(&running);
    assert(ui.spectrum_id==3&&ui.spectrum_result.rate_hz==50000000&&ui.spectrum_sequence==seq+1&&ui.model.running);
    save(display,directory,"spectrum-live-raw-en");click(ui.spectrum_close_button);click(ui.measurement_close_button);
    /* Replay keeps the saved rate even when the live ADC profile is faster. */
    assert(scope_ui_memory_load(&ui)&&ui.replay_active&&ui.adc_rate==6250000&&ui.conversion_hz==50000000);
    Capture replay=capture();assert(scope_ui_show_spectrum(&ui,true));held(&replay);
    assert(ui.spectrum_id==2&&ui.spectrum_result.rate_hz==6250000&&ui.spectrum_source_epoch==ui.memory_meta.capture_epoch);
    save(display,directory,"spectrum-replay-en");click(ui.spectrum_close_button);assert(scope_ui_memory_return_live(&ui));scope_ui_set_running(&ui,false);
    /* Bad inputs/unsupported contexts preserve the previous complete result. */
    ui.adc_count=4095;reject_all();ui.adc_count=4096;
    later[10]^=0x8000;reject_all();later[10]^=0x8000;
    ui.bars_on=true;reject_all();ui.bars_on=false;
    ui.autoset_state=SCOPE_AUTO_WAITING;reject_all();ui.autoset_state=SCOPE_AUTO_IDLE;
    assert(scope_ui_select_source(&ui,2));reject_all();assert(lv_obj_has_flag(ui.spectrum_panel,LV_OBJ_FLAG_HIDDEN));
    assert(scope_ui_select_source(&ui,1));assert(scope_ui_show_spectrum(&ui,true));
    /* The real acquisition spectrum remains visible at its full span. */
    assert(scope_ui_spectrum_range(&ui,false));int32_t maximum=-100000;for(unsigned p=0;p<SCOPE_POINTS;p++)if(ui.spectrum_plot[p]>maximum)maximum=ui.spectrum_plot[p];
    assert(maximum>-30000&&maximum<=20000);save(display,directory,"spectrum-final-en");
    /* A one-bin line between nearest-neighbour sample points must survive.
       Bin1001 is skipped by floor(point*2048/719), so point sampling fails. */
    memset(ui.spectrum_result.vrms,0,sizeof(ui.spectrum_result.vrms));ui.spectrum_result.vrms[1001]=.5f;
    seq=ui.spectrum_sequence;assert(scope_ui_spectrum_range(&ui,false));maximum=-100000;
    for(unsigned p=0;p<SCOPE_POINTS;p++)if(ui.spectrum_plot[p]>maximum)maximum=ui.spectrum_plot[p];
    assert(abs(maximum+6021)<=1&&ui.spectrum_sequence==seq);
    lv_deinit();puts("SPECTRUM_UI_TEST_PASS: pointer, bilingual layout, immutable snapshots, source rate, independent channel, options, guards, frozen/raw/epoch/slots");return 0;
}
