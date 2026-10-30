#include "scope_ui.h"
#include "scope_trigger.h"
#include "scope_calibration_profile.h"
#include "fonts/scope_fonts.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

static const double timebase[] = {.00005,.0001,.0002,.0005,.001,.002,.005,.01,.02,.05,.1,.2,.5};
static const char *time_names[] = {"50 µs", "100 µs", "200 µs", "500 µs", "1 ms", "2 ms", "5 ms", "10 ms", "20 ms", "50 ms", "100 ms", "200 ms", "500 ms"};
static const double fast_timebase[]={.0000001,.0000002,.0000005,.000001,.000002,.000005,.00001,.00002};
static const char *fast_time_names[]={"100 ns","200 ns","500 ns","1 µs","2 µs","5 µs","10 µs","20 µs"};
static unsigned timebase_prefix(unsigned conversion_hz){
    return conversion_hz==50000000?8:conversion_hz==10000000||conversion_hz==25000000?5:0;
}
static bool highspeed_profile(const ScopeUI *ui){return timebase_prefix(ui->conversion_hz)!=0;}
static double current_timebase(const ScopeUI *ui){
    unsigned index=(unsigned)ui->timebase_index;
    unsigned prefix=timebase_prefix(ui->conversion_hz);
    if(index<prefix)return fast_timebase[index+8-prefix];
    index-=prefix;
    return index<SCOPE_TIMEBASE_COUNT?timebase[index]:0;
}
static const char *current_time_name(const ScopeUI *ui){
    unsigned index=(unsigned)ui->timebase_index;
    unsigned prefix=timebase_prefix(ui->conversion_hz);
    if(index<prefix)return fast_time_names[index+8-prefix];
    index-=prefix;
    return index<SCOPE_TIMEBASE_COUNT?time_names[index]:"--";
}
static unsigned timebase_count(const ScopeUI *ui){
    return ui->conversion_hz==50000000?SCOPE_50M_TIMEBASE_COUNT:
        highspeed_profile(ui)?SCOPE_HIGHSPEED_TIMEBASE_COUNT:ui->adaptive_sampling?SCOPE_TIMEBASE_COUNT:7;
}
static const double scales[] = {.1,.2,.5,1,2,5,10};
static const double frequencies[] = {100,500,1000,2000,5000,10000,20000};
static bool single_capable(const ScopeUI *ui){
    if(!ui->adc_record||ui->adc_channel>=2)return false;
    if(ui->adaptive_sampling){
        ScopeCapturePlan p;
        return scope_ui_capture_plan(ui,&p)==0&&10*current_timebase(ui)*p.rate_hz<=p.count-1;
    }
    return 10*current_timebase(ui)*ui->adc_rate<=ui->adc_count-1;
}
bool scope_ui_set_calibration(ScopeUI *ui,const ScopeCalibration *profile){
    if(!ui||ui->adc_record||!scope_calibration_valid(profile))return false;
    ui->instrument_calibration=profile?*profile:(ScopeCalibration){0};
    ui->record_calibration=ui->instrument_calibration;
    return true;
}
static double code_span_volts(const ScopeCalibration *profile,unsigned channel,unsigned codes){
    return codes*(scope_calibration_voltage(profile,channel,1023)-
        scope_calibration_voltage(profile,channel,0))/1023;
}
static int align_record(const ScopeUI *ui,const uint32_t *record,unsigned count,unsigned rate,ScopeTriggerResult *result){
    uint32_t q8;
    if(scope_calibration_trigger_q8(&ui->instrument_calibration,ui->trigger_channel,ui->trigger_level_mv,&q8))return -1;
    ScopeTriggerConfig config={ui->trigger_channel,ui->trigger_rising,q8/256.0,10,10*current_timebase(ui)*rate,.2};
    return scope_trigger_find(record,count,&config,result);
}
static bool valid_record_profile(const ScopeUI *ui,const uint32_t *record,unsigned count,unsigned rate,
                                  const ScopeAdcMetrics metrics[2],const ScopeRecordInfo *info){
    if(!ui||ui->replay_active||!record||!count||count>4096||!rate||!metrics)return false;
    /* Reject malformed words before advancing the record-ID gate. OTR is data. */
    for(unsigned i=0;i<count;i++)if(record[i]&UINT32_C(0xf800f800))return false;
    if(ui->adaptive_sampling){
        ScopeCapturePlan p;
        if(!info||scope_ui_capture_plan(ui,&p)!=0||
            info->capture_epoch!=ui->capture_epoch||info->rate_index!=p.rate_index||
            rate!=p.rate_hz||count!=p.count||info->conversion_hz!=p.conversion_hz||
            info->filter_kind!=p.filter_kind)return false;
    }
    return true;
}
static lv_color_t color(uint32_t c) { return lv_color_hex(c); }
static const uint32_t BG=0x0c111b, PANEL=0x151e2c, BORDER=0x28364a, TEXT=0xe5edf7, MUTED=0x8798af, CYAN=0x40e0c0;
static const uint32_t channel_colors[]={0x40e0c0,0xffc857};

typedef enum {
    TITLE, SUBTITLE, SETTINGS, CHANNEL, AXES, STATE_RUN, STATE_STOP, PAUSE, RUN,
    TIME_FMT, SCALE_FMT, SOURCE_FMT, VPP_TITLE, RMS_TITLE, FREQ_TITLE,
    WAVE_TITLE, SINE, SQUARE, TRIANGLE, SOURCE_NOTE, FOOTER, DIALOG_TITLE,
    GRID_ON, GRID_OFF, BARS_ON, BARS_OFF, SETTINGS_NOTE, CLOSE, LANGUAGE,
    FOOTER_BOARD, SUBTITLE_BOARD, NOTE_BOARD, NOTE_ADAPTIVE, NOTE_RAW, TEXT_COUNT
} TextId;
static const char *const translations[TEXT_COUNT][2]={
    [TITLE]={"数字示波器","OSCILLOSCOPE"},
    [SUBTITLE]={"演示模式","DEMO MODE"},
    [SETTINGS]={"设置","SETTINGS"},
    [CHANNEL]={"演示信号","DEMO SIGNAL"},
    [AXES]={"水平：10 div  |  垂直：8 div  |  零位：0 V","HORIZONTAL: 10 div  |  VERTICAL: 8 div  |  ZERO: 0 V"},
    [STATE_RUN]={"演示运行","DEMO RUN"},
    [STATE_STOP]={"演示停止","DEMO STOP"},
    [PAUSE]={"停止","STOP"}, [RUN]={"运行","RUN"},
    [TIME_FMT]={"水平时基   %s/div","TIMEBASE   %s/div"},
    [SCALE_FMT]={"垂直刻度   %.1f V/div","VERTICAL   %.1f V/div"},
    [SOURCE_FMT]={"演示频率   %.1f kHz","DEMO FREQ   %.1f kHz"},
    [VPP_TITLE]={"峰峰值 Vpp","PEAK-TO-PEAK"},
    [RMS_TITLE]={"有效值 Vrms","RMS VALUE"},
    [FREQ_TITLE]={"频率 f","FREQUENCY"},
    [WAVE_TITLE]={"演示波形","DEMO WAVEFORM"},
    [SINE]={"正弦","SINE"}, [SQUARE]={"方波","SQR"}, [TRIANGLE]={"三角","TRI"},
    [SOURCE_NOTE]={"幅度 4.00 Vpp\n直流偏置 0.00 V","AMPLITUDE 4.00 Vpp\nDC OFFSET 0.00 V"},
    [FOOTER]={"演示模式  |  合成波形","DEMO MODE  |  SYNTHETIC WAVEFORM"},
    [DIALOG_TITLE]={"仪器设置","INSTRUMENT SETUP"},
    [GRID_ON]={"网格：显示","GRID: ON"}, [GRID_OFF]={"网格：隐藏","GRID: OFF"},
    [BARS_ON]={"显示内容：测试色条","DISPLAY: TEST PATTERN"},
    [BARS_OFF]={"显示内容：波形","DISPLAY: WAVEFORM"},
    [SETTINGS_NOTE]={"演示模式使用合成信号。",
                     "Demo mode uses a synthetic signal."},
    [CLOSE]={"关闭","CLOSE"},
    [LANGUAGE]={"语言：简体中文 / English","LANGUAGE: ENGLISH / 简体中文"},
    [FOOTER_BOARD]={"演示模式  |  合成波形","DEMO MODE  |  SYNTHETIC WAVEFORM"},
    [SUBTITLE_BOARD]={"双通道","DUAL DSO"},
    [NOTE_BOARD]={"电压读数尚未校准。\n单次采集窗口须位于记录范围内。",
                  "Voltage readings are uncalibrated.\nSingle acquisition requires a complete window."},
    [NOTE_ADAPTIVE]={"长时基降低采样率，较快信号请选更短时基。\n数字通带内幅度衰减 <0.5 dB。\n电压读数尚未校准。",
                     "Long timebases reduce the sample rate.\nFaster signals: use a shorter timebase.\nPassband loss <0.5 dB; volts uncalibrated."},
    [NOTE_RAW]={"直采模式无数字抗混叠滤波。\n采样率不代表整机模拟带宽。\n电压读数尚未校准。",
                "RAW acquisition: no digital antialias filter.\nSample rate is not measured analog bandwidth.\nVoltage readings are uncalibrated."}
};
static const char *tr(const ScopeUI *ui,TextId id) {return translations[id][ui->language];}
static const char *voltage_reference(const ScopeUI *ui,const ScopeCalibration *profile,unsigned channel){
    bool calibrated=channel<2&&(profile->flags&(1u<<channel));
    if(ui->language==SCOPE_ZH_CN)return calibrated?"电压：按源设定值工程校准":"电压未校准";
    return calibrated?"V: source-setpoint calibration":"V: uncalibrated";
}
static void button_text(lv_obj_t *obj,const char *text) {lv_label_set_text(lv_obj_get_child(obj,0),text);}
static void localize(ScopeUI *ui) {
    lv_label_set_text(ui->title_label,tr(ui,TITLE));
    lv_label_set_text(ui->subtitle_label,tr(ui,ui->board_mode?SUBTITLE_BOARD:SUBTITLE));
    button_text(ui->settings_button,tr(ui,SETTINGS));
    lv_label_set_text(ui->channel_label,tr(ui,CHANNEL));
    lv_label_set_text(ui->axes_label,tr(ui,AXES));
    lv_label_set_text(ui->wave_title,tr(ui,WAVE_TITLE));
    for(int i=0;i<3;i++) {
        lv_label_set_text(ui->metric_titles[i],tr(ui,(TextId)(VPP_TITLE+i)));
        button_text(ui->wave_buttons[i],tr(ui,(TextId)(SINE+i)));
    }
    lv_label_set_text(ui->source_note,tr(ui,SOURCE_NOTE));
    lv_label_set_text(ui->footer_label,tr(ui,ui->board_mode?FOOTER_BOARD:FOOTER));
    lv_label_set_text(ui->settings_title,tr(ui,DIALOG_TITLE));
    lv_label_set_text(ui->settings_note,tr(ui,ui->board_mode?NOTE_BOARD:SETTINGS_NOTE));
    button_text(ui->close_button,tr(ui,CLOSE));
    button_text(ui->language_button,tr(ui,LANGUAGE));
}

static lv_obj_t *box(lv_obj_t *parent,int x,int y,int w,int h,uint32_t bg) {
    lv_obj_t *o=lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o,x,y); lv_obj_set_size(o,w,h);
    lv_obj_set_style_bg_color(o,color(bg),0); lv_obj_set_style_bg_opa(o,255,0);
    lv_obj_set_style_radius(o,8,0);
    lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);
    return o;
}
static lv_obj_t *label(lv_obj_t *parent,int x,int y,int w,const char *text,uint32_t fg,const lv_font_t *font) {
    lv_obj_t *o=lv_label_create(parent);
    lv_obj_set_pos(o,x,y); lv_obj_set_width(o,w);
    lv_obj_set_style_text_font(o,font,0); lv_obj_set_style_text_color(o,color(fg),0);
    lv_label_set_text(o,text); return o;
}
static lv_obj_t *button(lv_obj_t *parent,int x,int y,int w,int h,const char *text,lv_event_cb_t cb,ScopeUI *ui) {
    lv_obj_t *o=lv_button_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o,x,y); lv_obj_set_size(o,w,h);
    lv_obj_set_style_bg_color(o,color(0x223146),0); lv_obj_set_style_bg_opa(o,255,0);
    lv_obj_set_style_bg_color(o,color(0x36516b),LV_STATE_PRESSED);
    lv_obj_set_style_radius(o,6,0);
    lv_obj_t *l=lv_label_create(o); lv_label_set_text(l,text);
    lv_obj_set_style_text_color(l,color(TEXT),0); lv_obj_center(l);
    if(cb) lv_obj_add_event_cb(o,cb,LV_EVENT_CLICKED,ui);
    return o;
}
static void update_cursors(ScopeUI *ui){
    bool adc=ui->adc_record&&ui->adc_channel<2;
    bool visible=adc&&ui->cursors_on&&!ui->bars_on;
    button_text(ui->cursor_button,ui->language==SCOPE_ZH_CN?(ui->cursors_on?"游标：开":"游标：关"):(ui->cursors_on?"CURSORS ON":"CURSORS OFF"));
    if(ui->adc_record)lv_obj_remove_flag(ui->cursor_button,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(ui->cursor_button,LV_OBJ_FLAG_HIDDEN);
    if(adc)lv_obj_remove_state(ui->cursor_button,LV_STATE_DISABLED);else lv_obj_add_state(ui->cursor_button,LV_STATE_DISABLED);
    memset(&ui->cursor_result,0,sizeof(ui->cursor_result));
    for(unsigned p=0;p<2;p++){
        if(visible){
            lv_obj_remove_flag(ui->cursor_line[p],LV_OBJ_FLAG_HIDDEN);lv_obj_remove_flag(ui->cursor_tag[p],LV_OBJ_FLAG_HIDDEN);
            int x=1+(int)((lv_obj_get_width(ui->chart)-3)*ui->cursor_positions[p]/1000);
            lv_obj_set_pos(ui->cursor_line[p],x,1);lv_obj_set_height(ui->cursor_line[p],lv_obj_get_height(ui->chart)-2);
            lv_obj_set_pos(ui->cursor_tag[p],x>lv_obj_get_width(ui->chart)-18?lv_obj_get_width(ui->chart)-18:x+3,2);
        }else{lv_obj_add_flag(ui->cursor_line[p],LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(ui->cursor_tag[p],LV_OBJ_FLAG_HIDDEN);}
    }
    if(!visible)return;
    ScopeCursorResult *r=&ui->cursor_result;
    if(scope_cursor_measure_calibrated(ui->adc_record,ui->adc_count,ui->adc_rate,ui->adc_display_start,
        10*current_timebase(ui)*ui->adc_rate,ui->cursor_positions,&ui->record_calibration,r))return;
    char text[96],a[20],b[20];unsigned ch=ui->adc_channel;
    bool fast=(unsigned)ui->timebase_index<timebase_prefix(ui->conversion_hz);
    double time_units=fast?1000000:1000;const char *time_unit=fast?"µs":"ms";
    snprintf(text,sizeof(text),"tA %.3f / tB %.3f %s",time_units*r->time_s[0],time_units*r->time_s[1],time_unit);lv_label_set_text(ui->metric_titles[0],text);
    snprintf(text,sizeof(text),"Δt %+.3f %s",time_units*r->delta_s,time_unit);lv_label_set_text(ui->vpp_label,text);
    if(r->point_valid[ch][0])snprintf(a,sizeof(a),"%+.3f",r->voltage_v[ch][0]);else snprintf(a,sizeof(a),"--");
    if(r->point_valid[ch][1])snprintf(b,sizeof(b),"%+.3f",r->voltage_v[ch][1]);else snprintf(b,sizeof(b),"--");
    snprintf(text,sizeof(text),"VA %s / VB %s V",a,b);lv_label_set_text(ui->metric_titles[1],text);
    if(r->delta_valid[ch])snprintf(text,sizeof(text),"ΔV %+.3f V",r->delta_v[ch]);else snprintf(text,sizeof(text),"ΔV --");
    lv_label_set_text(ui->rms_label,text);
    lv_label_set_text(ui->metric_titles[2],ui->language==SCOPE_ZH_CN?"间隔倒数 1/|Δt|":"RECIPROCAL 1/|Δt|");
    if(r->reciprocal_valid)snprintf(text,sizeof(text),"%.3f kHz",r->reciprocal_hz/1000);else snprintf(text,sizeof(text),"--");
    lv_label_set_text(ui->readout_label,text);
}
static void update_channel_controls(ScopeUI *ui){
    bool adc=ui->adc_record&&ui->adc_channel<2;
    if(adc&&!ui->bars_on)lv_obj_remove_flag(ui->channel_panel_button,LV_OBJ_FLAG_HIDDEN);
    else {lv_obj_add_flag(ui->channel_panel_button,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(ui->channel_panel,LV_OBJ_FLAG_HIDDEN);}
    lv_obj_set_y(ui->source_note,adc?522:490);
    button_text(ui->channel_panel_button,ui->language==SCOPE_ZH_CN?"通道设置":"CHANNEL SETUP");
    lv_label_set_text(ui->channel_panel_title,ui->language==SCOPE_ZH_CN?"通道设置":"CHANNEL SETUP");
    button_text(ui->channel_zero_button,ui->language==SCOPE_ZH_CN?"位置归零":"ZERO POSITION");
    button_text(ui->channel_close_button,tr(ui,CLOSE));
    unsigned ch=ui->channel_config_target;char text[96];
    snprintf(text,sizeof(text),ui->language==SCOPE_ZH_CN?"CH%u 垂直刻度 %.1f V/div":"CH%u VERTICAL %.1f V/div",ch+1,scales[ui->channel_scale_index[ch]]);
    lv_label_set_text(ui->channel_scale_label,text);
    snprintf(text,sizeof(text),ui->language==SCOPE_ZH_CN?"CH%u 垂直位置 %+.2f div":"CH%u POSITION %+.2f div",ch+1,ui->channel_position_mdiv[ch]/1000.0);
    lv_label_set_text(ui->channel_position_label,text);
    /* Map 0 V with the same content height and integer arithmetic used by
     * lv_chart's line renderer. Only labels move when references are close;
     * the leaders always end at the exact channel zero. */
    lv_obj_update_layout(ui->chart);
    int height=lv_obj_get_content_height(ui->chart);
    int top=lv_obj_get_y(ui->chart)+lv_obj_get_style_pad_top(ui->chart,LV_PART_MAIN)+
        lv_obj_get_style_border_width(ui->chart,LV_PART_MAIN);
    int zero[2],center[2];bool shown[2];
    for(unsigned c=0;c<2;c++){
        zero[c]=top+height-(ui->channel_position_mdiv[c]+4000)*height/8000;
        center[c]=zero[c];shown[c]=adc&&!ui->bars_on&&(ui->dual_display||ui->adc_channel==c);
    }
    if(shown[0]&&shown[1]&&abs(zero[0]-zero[1])<20){
        unsigned upper=zero[0]<=zero[1]?0:1,lower=1-upper;
        center[upper]=(zero[0]+zero[1]-20)/2;center[lower]=center[upper]+20;
    }
    for(unsigned c=0;c<2;c++){
        lv_obj_set_style_bg_color(ui->channel_choices[c],color(c==ch?0x21715f:0x223146),0);
        if(shown[c]){
            lv_obj_remove_flag(ui->zero_marker[c],LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(ui->zero_leader[c],LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_y(ui->zero_marker[c],center[c]-9);
            ui->zero_leader_points[c][0]=(lv_point_precise_t){18,center[c]};
            ui->zero_leader_points[c][1]=(lv_point_precise_t){21,zero[c]};
            ui->zero_leader_points[c][2]=(lv_point_precise_t){23,zero[c]};
            lv_line_set_points(ui->zero_leader[c],ui->zero_leader_points[c],3);
        }else{
            lv_obj_add_flag(ui->zero_marker[c],LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui->zero_leader[c],LV_OBJ_FLAG_HIDDEN);
        }
        snprintf(text,sizeof(text),"CH%u %.1f V/div",c+1,scales[ui->channel_scale_index[c]]);
        lv_label_set_text(ui->channel_legend[c],text);
    }
}
bool scope_ui_measurement_frequency(const ScopeUI *ui,unsigned channel,double *frequency_hz){
    if(frequency_hz)*frequency_hz=0;
    if(!ui||!frequency_hz||!ui->adc_record||ui->adc_channel>=2||channel>=2||ui->adc_count<2||!ui->adc_rate)return false;
    const ScopeMeasurement *m=&ui->measurement[channel];
    const ScopeAdcMetrics *a=&ui->adc_metrics[channel];
    if(!m->valid||m->overrange||m->vpp_v<code_span_volts(&ui->record_calibration,channel,16)-1e-12||!a->frequency_valid||
        !isfinite(a->frequency_hz)||a->frequency_hz<2.0*ui->adc_rate/(ui->adc_count-1)||a->frequency_hz>ui->adc_rate/4.0)return false;
    *frequency_hz=a->frequency_hz;return true;
}
static void update_measurements(ScopeUI *ui){
    bool available=ui->adc_record&&ui->adc_channel<2&&!ui->bars_on;
    button_text(ui->measurement_button,ui->language==SCOPE_ZH_CN?"测量":"MEASURE");
    lv_label_set_text(ui->measurement_title,ui->language==SCOPE_ZH_CN?"测量详情":"MEASUREMENTS");
    button_text(ui->measurement_close_button,tr(ui,CLOSE));
    button_text(ui->measurement_timing_button,ui->language==SCOPE_ZH_CN?
        (ui->measurement_timing?"基本":"时域"):(ui->measurement_timing?"BASIC":"TIMING"));
    if(available)lv_obj_remove_flag(ui->measurement_button,LV_OBJ_FLAG_HIDDEN);
    else {lv_obj_add_flag(ui->measurement_button,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(ui->measurement_panel,LV_OBJ_FLAG_HIDDEN);}
    static const char *const names[8][2]={
        {"最大值 Vmax","MAXIMUM Vmax"},{"最小值 Vmin","MINIMUM Vmin"},
        {"平均值 Vmean","MEAN Vmean"},{"峰峰值 Vpp","PEAK-TO-PEAK Vpp"},
        {"总有效值","RMS (AC+DC)"},{"交流有效值","RMS (AC)"},
        {"频率 f","FREQUENCY f"},{"周期 T","PERIOD T"}
    };
    for(unsigned i=0;i<8;i++){
        lv_label_set_text(ui->measurement_names[i],names[i][ui->language]);
        if(ui->measurement_timing){lv_obj_add_flag(ui->measurement_names[i],LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(ui->measurement_values[i],LV_OBJ_FLAG_HIDDEN);}
        else{lv_obj_remove_flag(ui->measurement_names[i],LV_OBJ_FLAG_HIDDEN);lv_obj_remove_flag(ui->measurement_values[i],LV_OBJ_FLAG_HIDDEN);}
    }
    static const char *const timing_names[6][2]={
        {"正占空比","POSITIVE DUTY"},{"正脉宽","POSITIVE WIDTH"},{"负脉宽","NEGATIVE WIDTH"},
        {"上升时间 10–90%","RISE 10–90%"},{"下降时间 90–10%","FALL 90–10%"},{"采样间隔","SAMPLE INTERVAL"}
    };
    for(unsigned i=0;i<6;i++){
        lv_label_set_text(ui->timing_names[i],timing_names[i][ui->language]);
        if(ui->measurement_timing){lv_obj_remove_flag(ui->timing_names[i],LV_OBJ_FLAG_HIDDEN);lv_obj_remove_flag(ui->timing_values[i],LV_OBJ_FLAG_HIDDEN);}
        else{lv_obj_add_flag(ui->timing_names[i],LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(ui->timing_values[i],LV_OBJ_FLAG_HIDDEN);}
    }
    if(!available)return;
    unsigned ch=ui->adc_channel;const ScopeMeasurement *m=&ui->measurement[ch];
    for(unsigned i=0;i<2;i++)lv_obj_set_style_bg_color(ui->measurement_choices[i],color(i==ch?0x21715f:0x223146),0);
    double values[]={m->vmax_v,m->vmin_v,m->vmean_v,m->vpp_v,m->rms_total_v,m->rms_ac_v};char text[320];
    for(unsigned i=0;i<6;i++){
        if(m->valid)snprintf(text,sizeof(text),"%.4f V",values[i]);else snprintf(text,sizeof(text),"--");
        lv_label_set_text(ui->measurement_values[i],text);
    }
    double frequency;
    bool valid=scope_ui_measurement_frequency(ui,ch,&frequency);
    if(valid){
        double value=frequency;const char *unit="Hz";
        if(value>=1000000){value/=1000000;unit="MHz";}else if(value>=1000){value/=1000;unit="kHz";}
        snprintf(text,sizeof(text),"%.4f %s",value,unit);
    }else snprintf(text,sizeof(text),"--");
    lv_label_set_text(ui->measurement_values[6],text);
    if(valid){
        double value=1.0/frequency;const char *unit="s";
        if(value<.000001){value*=1e9;unit="ns";}else if(value<.001){value*=1e6;unit="µs";}else if(value<1){value*=1000;unit="ms";}
        snprintf(text,sizeof(text),"%.4f %s",value,unit);
    }else snprintf(text,sizeof(text),"--");
    lv_label_set_text(ui->measurement_values[7],text);
    const ScopeTimingMeasurement *t=&m->timing;
    double timing_values[]={t->duty_percent,t->positive_s,t->negative_s,t->rise_s,t->fall_s,1.0/ui->adc_rate};
    bool timing_valid[]={t->pulse_valid,t->pulse_valid,t->pulse_valid,t->rise_valid,t->fall_valid,true};
    for(unsigned i=0;i<6;i++){
        if(!timing_valid[i])snprintf(text,sizeof(text),"--");
        else if(!i)snprintf(text,sizeof(text),"%.4f %%",timing_values[i]);
        else{
            double value=timing_values[i];const char *unit="s";
            if(value<.000001){value*=1e9;unit="ns";}else if(value<.001){value*=1e6;unit="µs";}else if(value<1){value*=1000;unit="ms";}
            snprintf(text,sizeof(text),"%.4f %s",value,unit);
        }
        lv_label_set_text(ui->timing_values[i],text);
    }
    snprintf(text,sizeof(text),ui->language==SCOPE_ZH_CN?
        "CH%u  |  完整记录  |  采样率 %u Sa/s\n%u 点  |  记录 ID %u  |  %s\n%s":
        "CH%u  |  FULL RECORD  |  SAMPLE RATE %u Sa/s\n%u pts  |  RECORD ID %u  |  %s\n%s",
        ch+1,ui->adc_rate,ui->adc_count,ui->adc_id,voltage_reference(ui,&ui->record_calibration,ch),m->overrange?
        (ui->language==SCOPE_ZH_CN?"过量程：电压统计仅供参考，频率和周期无效。":"OVERRANGE: voltage statistics only; frequency and period unavailable."):
        (ui->language==SCOPE_ZH_CN?"统计范围为完整采集记录。":"Statistics use the complete acquisition record."));
    if(ui->measurement_timing)snprintf(text,sizeof(text),ui->language==SCOPE_ZH_CN?
        "CH%u | 完整记录 | %u Sa/s | %u 点 | ID %u\n脉宽：中点 50%%；边沿：10–90%%；%s\n无可靠周期、边沿有噪声或采样不足时显示 --":
        "CH%u | FULL RECORD | %u Sa/s | %u pts | ID %u\nPulse: midpoint 50%% | Edge: 10–90%% | %s\nNo reliable cycles / noisy edges / insufficient samples: --",
        ch+1,ui->adc_rate,ui->adc_count,ui->adc_id,voltage_reference(ui,&ui->record_calibration,ch));
    lv_label_set_text(ui->measurement_note,text);
    lv_obj_set_style_text_color(ui->measurement_note,color(m->overrange?0xffc857:MUTED),0);
}
bool scope_ui_show_measurements(ScopeUI *ui,bool show){
    if(!ui||scope_ui_autoset_busy(ui))return false;
    if(!ui||!ui->measurement_panel)return false;
    if(show){
        if(!ui->adc_record||ui->adc_channel>=2||ui->bars_on)return false;
        update_measurements(ui);lv_obj_remove_flag(ui->measurement_panel,LV_OBJ_FLAG_HIDDEN);
    }else lv_obj_add_flag(ui->measurement_panel,LV_OBJ_FLAG_HIDDEN);
    return true;
}
bool scope_ui_show_timing_measurements(ScopeUI *ui,bool timing){
    if(!scope_ui_show_measurements(ui,true))return false;
    ui->measurement_timing=timing;update_measurements(ui);return true;
}
static void measurement_timing_event(lv_event_t *e){
    ScopeUI *ui=lv_event_get_user_data(e);
    (void)scope_ui_show_timing_measurements(ui,!ui->measurement_timing);
}
static void measurement_event(lv_event_t *e){
    ScopeUI *ui=lv_event_get_user_data(e);
    (void)scope_ui_show_measurements(ui,lv_obj_has_flag(ui->measurement_panel,LV_OBJ_FLAG_HIDDEN));
}
static void measurement_choice_event(lv_event_t *e){
    ScopeUI *ui=lv_event_get_user_data(e);
    (void)scope_ui_select_source(ui,(unsigned)(uintptr_t)lv_obj_get_user_data(lv_event_get_target(e)));
}
static bool spectrum_available(const ScopeUI *ui){
    return ui&&ui->adc_record&&ui->adc_count==SCOPE_SPECTRUM_N&&ui->adc_rate&&
        ui->adc_channel<2&&!ui->bars_on&&!scope_ui_autoset_busy(ui);
}
static void spectrum_frequency(char *text,size_t size,double hz){
    const char *unit="Hz";
    if(hz>=1000000){hz/=1000000;unit="MHz";}else if(hz>=1000){hz/=1000;unit="kHz";}
    snprintf(text,size,"%.3f %s",hz,unit);
}
static void spectrum_project(ScopeUI *ui){
    if(!ui->spectrum_valid)return;
    unsigned end=SCOPE_SPECTRUM_BINS-1;
    if(ui->spectrum_auto_range&&ui->spectrum_result.peak_valid){
        unsigned need=(unsigned)ceil(ui->spectrum_result.peak_bin)*4u;
        end=16;while(end<need&&end<SCOPE_SPECTRUM_BINS-1)end*=2;
    }
    ui->spectrum_end_bin=end;
    for(unsigned point=0;point<SCOPE_POINTS;point++){
        unsigned first=point*end/(SCOPE_POINTS-1);
        unsigned last=((point+1)*end+SCOPE_POINTS-2)/(SCOPE_POINTS-1);
        if(last>end)last=end;
        float maximum=0;for(unsigned bin=first;bin<=last;bin++)if(ui->spectrum_result.vrms[bin]>maximum)maximum=ui->spectrum_result.vrms[bin];
        double db=maximum>0?20*log10(maximum):-100;
        ui->spectrum_plot[point]=(int32_t)lround(fmax(-100,fmin(20,db))*1000);
    }
    lv_chart_refresh(ui->spectrum_chart);
}
static void update_spectrum(ScopeUI *ui){
    if(!ui->spectrum_panel)return;
    bool zh=ui->language==SCOPE_ZH_CN,available=ui->adc_record&&ui->adc_count==SCOPE_SPECTRUM_N&&ui->adc_channel<2&&!ui->bars_on;
    button_text(ui->spectrum_button,zh?"频谱分析":"SPECTRUM");
    if(available)lv_obj_remove_flag(ui->spectrum_button,LV_OBJ_FLAG_HIDDEN);
    else {lv_obj_add_flag(ui->spectrum_button,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(ui->spectrum_panel,LV_OBJ_FLAG_HIDDEN);}
    if(spectrum_available(ui))lv_obj_remove_state(ui->spectrum_button,LV_STATE_DISABLED);else lv_obj_add_state(ui->spectrum_button,LV_STATE_DISABLED);
    lv_label_set_text(ui->spectrum_title,zh?"频谱分析":"SPECTRUM ANALYSIS");
    button_text(ui->spectrum_close_button,tr(ui,CLOSE));
    button_text(ui->spectrum_update_button,zh?"更新频谱":"UPDATE SPECTRUM");
    button_text(ui->spectrum_window_button,zh?(ui->spectrum_window==SCOPE_SPECTRUM_HANN?"窗函数：Hann":"窗函数：矩形"):
        (ui->spectrum_window==SCOPE_SPECTRUM_HANN?"WINDOW: HANN":"WINDOW: RECTANGULAR"));
    button_text(ui->spectrum_mean_button,zh?(ui->spectrum_remove_mean?"去均值：开":"去均值：关"):
        (ui->spectrum_remove_mean?"REMOVE MEAN: ON":"REMOVE MEAN: OFF"));
    button_text(ui->spectrum_range_button,zh?(ui->spectrum_auto_range?"频率范围：自动":"频率范围：全频"):
        (ui->spectrum_auto_range?"RANGE: AUTOMATIC":"RANGE: FULL"));
    for(unsigned ch=0;ch<2;ch++)lv_obj_set_style_bg_color(ui->spectrum_choices[ch],color(ch==ui->spectrum_channel?0x21715f:0x223146),0);
    if(!ui->spectrum_valid){lv_label_set_text(ui->spectrum_note,zh?"请采集完整记录后更新频谱。":"Acquire a complete record, then update the spectrum.");return;}
    const ScopeSpectrumResult *result=&ui->spectrum_result;
    char text[384],rate[32],resolution[32],limit[32],passband[32],peak[32];
    double samples=result->rate_hz;const char *rate_unit="Sa/s";
    if(samples>=1000000){samples/=1000000;rate_unit="MSa/s";}else if(samples>=1000){samples/=1000;rate_unit="kSa/s";}
    snprintf(rate,sizeof(rate),"%.3f %s",samples,rate_unit);spectrum_frequency(resolution,sizeof(resolution),result->bin_hz);
    spectrum_frequency(limit,sizeof(limit),result->rate_hz*.5);spectrum_frequency(passband,sizeof(passband),result->rate_hz*.1);
    const char *status=result->overrange?(zh?"输入超量程：幅度不可靠，谱峰无效。":"Input overrange; peak unavailable."):
        !result->peak_valid?(zh?"无明确交流谱峰或分辨率不足。":"No clear AC peak or insufficient resolution."):
        (zh?"频谱为快照；按更新频谱重新分析。":"Spectrum snapshot; update to analyze again.");
    char reference_status[192];snprintf(reference_status,sizeof(reference_status),"%s | %s",status,voltage_reference(ui,&ui->spectrum_calibration,ui->spectrum_channel));
    if(ui->spectrum_filter==3)snprintf(text,sizeof(text),zh?
        "CH%u  |  4096 点  |  采样率 %s  |  %s 窗\n数字平坦通带 0–%s；全频上限 %s，通带外幅度受滤波影响。\n%s":
        "CH%u  |  4096 pts  |  SAMPLE RATE %s  |  %s WINDOW\nDigital flat passband 0–%s; full limit %s (filter loss outside passband).\n%s",
        ui->spectrum_channel+1,rate,ui->spectrum_window==SCOPE_SPECTRUM_HANN?"Hann":(zh?"矩形":"RECTANGULAR"),passband,limit,reference_status);
    else snprintf(text,sizeof(text),zh?
        "CH%u  |  4096 点  |  采样率 %s  |  %s 窗\n全频范围 0–%s；采样率不代表整机模拟带宽。\n%s":
        "CH%u  |  4096 pts  |  SAMPLE RATE %s  |  %s WINDOW\nFull range 0–%s; sample rate is not analog bandwidth.\n%s",
        ui->spectrum_channel+1,rate,ui->spectrum_window==SCOPE_SPECTRUM_HANN?"Hann":(zh?"矩形":"RECTANGULAR"),limit,reference_status);
    lv_label_set_text(ui->spectrum_note,text);
    lv_obj_set_style_text_color(ui->spectrum_note,color(result->overrange?0xffc857:MUTED),0);
    if(result->peak_valid){
        spectrum_frequency(peak,sizeof(peak),result->peak_hz);
        snprintf(text,sizeof(text),zh?"主交流谱峰\n%s":"MAIN AC PEAK\n%s",peak);
    }else snprintf(text,sizeof(text),zh?"主交流谱峰\n--":"MAIN AC PEAK\n--");
    lv_label_set_text(ui->spectrum_values[0],text);
    if(result->peak_valid)snprintf(text,sizeof(text),zh?"最大谱线\n%.4f Vrms / %+.2f dBV":"MAX LINE AMPLITUDE\n%.4f Vrms / %+.2f dBV",result->peak_vrms,result->peak_dbv);
    else snprintf(text,sizeof(text),zh?"最大谱线\n--":"MAX LINE AMPLITUDE\n--");
    lv_label_set_text(ui->spectrum_values[1],text);
    snprintf(text,sizeof(text),zh?"频率分辨率 Δf\n%s":"FREQUENCY RESOLUTION Δf\n%s",resolution);lv_label_set_text(ui->spectrum_values[2],text);
    for(unsigned i=0;i<5;i++){spectrum_frequency(text,sizeof(text),i*ui->spectrum_end_bin*result->bin_hz/4);lv_label_set_text(ui->spectrum_xlabels[i],text);}
}
static bool spectrum_analyze_options(ScopeUI *ui,unsigned channel,ScopeSpectrumWindow window,bool remove_mean){
    if(!spectrum_available(ui)||channel>1||(window!=SCOPE_SPECTRUM_RECT&&window!=SCOPE_SPECTRUM_HANN))return false;
    uint32_t started=lv_tick_get();
    if(scope_spectrum_analyze_calibrated(ui->adc_record,ui->adc_count,ui->adc_rate,channel,window,remove_mean,&ui->record_calibration,&ui->spectrum_workspace,&ui->spectrum_result))return false;
    ui->spectrum_calibration=ui->record_calibration;
    memcpy(ui->spectrum_words,ui->adc_record,sizeof(ui->spectrum_words));
    ui->spectrum_channel=channel;ui->spectrum_window=window;ui->spectrum_remove_mean=remove_mean;
    ui->spectrum_id=ui->adc_id;ui->spectrum_source_epoch=ui->adc_info.capture_epoch;
    ui->spectrum_conversion=ui->adc_info.conversion_hz;ui->spectrum_filter=ui->adc_info.filter_kind;
    ui->spectrum_raw_crc=scope_record_crc32(ui->spectrum_words,sizeof(ui->spectrum_words));
    ui->spectrum_valid=true;ui->spectrum_sequence++;ui->spectrum_elapsed_ms=lv_tick_elaps(started);
    lv_chart_set_series_color(ui->spectrum_chart,ui->spectrum_series,color(channel_colors[channel]));
    spectrum_project(ui);update_spectrum(ui);return true;
}
bool scope_ui_spectrum_update(ScopeUI *ui){
    return ui&&spectrum_analyze_options(ui,ui->spectrum_channel,ui->spectrum_window,ui->spectrum_remove_mean);
}
bool scope_ui_show_spectrum(ScopeUI *ui,bool show){
    if(!ui||!ui->spectrum_panel)return false;
    if(show){
        unsigned channel=ui->spectrum_valid?ui->spectrum_channel:ui->adc_channel;
        if(!spectrum_analyze_options(ui,channel,ui->spectrum_window,ui->spectrum_remove_mean))return false;
        lv_obj_remove_flag(ui->spectrum_panel,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(ui->spectrum_panel);
    }else lv_obj_add_flag(ui->spectrum_panel,LV_OBJ_FLAG_HIDDEN);
    return true;
}
bool scope_ui_spectrum_channel(ScopeUI *ui,unsigned channel){return ui&&spectrum_analyze_options(ui,channel,ui->spectrum_window,ui->spectrum_remove_mean);}
bool scope_ui_spectrum_window(ScopeUI *ui,ScopeSpectrumWindow window){return ui&&spectrum_analyze_options(ui,ui->spectrum_channel,window,ui->spectrum_remove_mean);}
bool scope_ui_spectrum_mean(ScopeUI *ui,bool remove_mean){return ui&&spectrum_analyze_options(ui,ui->spectrum_channel,ui->spectrum_window,remove_mean);}
bool scope_ui_spectrum_range(ScopeUI *ui,bool auto_range){
    if(!spectrum_available(ui)||!ui->spectrum_valid)return false;
    ui->spectrum_auto_range=auto_range;spectrum_project(ui);update_spectrum(ui);return true;
}
static void spectrum_event(lv_event_t *event){
    ScopeUI *ui=lv_event_get_user_data(event);(void)scope_ui_show_spectrum(ui,lv_obj_has_flag(ui->spectrum_panel,LV_OBJ_FLAG_HIDDEN));
}
static void spectrum_action_event(lv_event_t *event){
    ScopeUI *ui=lv_event_get_user_data(event);lv_obj_t *target=lv_event_get_target(event);
    if(target==ui->spectrum_choices[0]||target==ui->spectrum_choices[1])(void)scope_ui_spectrum_channel(ui,target==ui->spectrum_choices[1]);
    else if(target==ui->spectrum_window_button)(void)scope_ui_spectrum_window(ui,ui->spectrum_window==SCOPE_SPECTRUM_HANN?SCOPE_SPECTRUM_RECT:SCOPE_SPECTRUM_HANN);
    else if(target==ui->spectrum_mean_button)(void)scope_ui_spectrum_mean(ui,!ui->spectrum_remove_mean);
    else if(target==ui->spectrum_range_button)(void)scope_ui_spectrum_range(ui,!ui->spectrum_auto_range);
    else if(target==ui->spectrum_update_button)(void)scope_ui_spectrum_update(ui);
}
bool scope_ui_autoset_busy(const ScopeUI *ui){return ui&&ui->autoset_state==SCOPE_AUTO_WAITING;}
static void update_autoset(ScopeUI *ui){
    static const char *const messages[10][2]={
        {"",""},{"自动设置中，请稍候","AUTOSET IN PROGRESS"},
        {"自动设置完成","AUTOSET COMPLETE"},{"无完整记录，请先采集","NO COMPLETE RECORD: acquire first"},
        {"无可靠信号，自动设置未执行","NO RELIABLE SIGNAL: settings unchanged"},
        {"通道过量程，自动设置未完成","OVERRANGE: autoset unsuccessful"},
        {"信号不稳定，已恢复原设置","UNSTABLE SIGNAL: settings restored"},
        {"当前模式不支持自动设置","AUTOSET UNAVAILABLE IN THIS MODE"},
        {"自动设置已取消，已恢复原设置","AUTOSET CANCELED: settings restored"},
        {"自动设置超时，已恢复原设置","AUTOSET TIMEOUT: settings restored"}
    };
    if(!ui->autoset_button)return;
    button_text(ui->autoset_button,ui->language==SCOPE_ZH_CN?"自动设置":"AUTOSET");
    button_text(ui->autoset_cancel_button,ui->language==SCOPE_ZH_CN?"取消":"CANCEL");
    ui->autoset_message=ui->autoset_need_wider?
        (ui->language==SCOPE_ZH_CN?"记录内周期不足，请加宽时基后重试":"TOO FEW CYCLES: widen the timebase and retry"):
        messages[ui->autoset_state][ui->language];
    lv_label_set_text(ui->autoset_label,ui->autoset_message);
    if(scope_ui_autoset_busy(ui)){
        lv_obj_remove_flag(ui->autoset_panel,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(ui->autoset_panel);
    }else lv_obj_add_flag(ui->autoset_panel,LV_OBJ_FLAG_HIDDEN);
    if(scope_ui_autoset_busy(ui)||ui->autoset_notice_s>0)lv_label_set_text(ui->footer_label,ui->autoset_message);
}
static void redraw(ScopeUI *ui);
static void update_memory(ScopeUI *ui);
static void update_parameters(ScopeUI *ui);
static uint32_t physical_timebase_ns(const ScopeUI *ui){return (uint32_t)lround(current_timebase(ui)*1e9);}
static int memory_timebase_index(const ScopeUI *ui,uint32_t ns){
    unsigned prefix=timebase_prefix(ui->conversion_hz);
    for(unsigned i=0;i<timebase_count(ui);i++){
        double seconds=i<prefix?fast_timebase[i+8-prefix]:timebase[i-prefix];
        if((uint32_t)lround(seconds*1e9)==ns)return (int)i;
    }
    return -1;
}
static bool settings_live(const ScopeUI *ui){
    return ui&&!ui->replay_active&&!scope_ui_autoset_busy(ui)&&!ui->bars_on&&ui->adc_record&&
        ui->adc_channel<2&&ui->adaptive_sampling&&ui->hardware_trigger;
}
static bool ui_buffer_disjoint(const ScopeUI *ui,const void *buffer,size_t length){
    uintptr_t a=(uintptr_t)ui,b=(uintptr_t)buffer;
    return ui&&buffer&&length<=UINTPTR_MAX-b&&sizeof(*ui)<=UINTPTR_MAX-a&&
        (a+sizeof(*ui)<=b||b+length<=a);
}
static bool settings_failure(ScopeUI *ui,unsigned notice){
    if(ui){ui->settings_notice=notice;update_parameters(ui);}return false;
}
static bool settings_export(ScopeUI *ui,uint8_t *blob,size_t capacity,ScopeSettings *out){
    if(!settings_live(ui))return false;
    ScopeSettings settings={0};
    settings.timebase_ns=physical_timebase_ns(ui);settings.origin_conversion_hz=ui->conversion_hz;
    settings.selected_channel=ui->adc_channel;settings.trigger_channel=ui->trigger_channel;
    settings.trigger_level_mv=ui->trigger_level_mv;settings.language=(uint32_t)ui->language;
    settings.flags=(ui->trigger_rising?SCOPE_SETTINGS_RISING:0)|(ui->trigger_normal?SCOPE_SETTINGS_NORMAL:0)|
        (ui->dual_display?SCOPE_SETTINGS_DUAL:0)|(ui->grid_on?SCOPE_SETTINGS_GRID:0)|(ui->cursors_on?SCOPE_SETTINGS_CURSORS:0);
    for(unsigned c=0;c<2;c++){
        settings.scale_index[c]=(uint32_t)ui->channel_scale_index[c];settings.position_mdiv[c]=ui->channel_position_mdiv[c];
        settings.cursor[c]=ui->cursor_positions[c];
    }
    size_t written;
    if(scope_settings_encode(&settings,blob,capacity,&written))return false;
    *out=settings;return true;
}
bool scope_ui_export_settings(ScopeUI *ui,uint8_t *blob,size_t capacity){
    if(!ui_buffer_disjoint(ui,blob,capacity))return false;
    ScopeSettings s;return settings_export(ui,blob,capacity,&s);
}
bool scope_ui_settings_save(ScopeUI *ui){
    if(!settings_live(ui))return settings_failure(ui,1);
    ScopeSettings settings;
    if(!settings_export(ui,ui->settings_blob,sizeof(ui->settings_blob),&settings))return settings_failure(ui,5);
    ui->settings_cache=settings;ui->settings_valid=true;ui->settings_notice=2;update_parameters(ui);return true;
}
static bool settings_apply(ScopeUI *ui,const ScopeSettings *settings,unsigned notice){
    int index=memory_timebase_index(ui,settings->timebase_ns);ScopeCapturePlan plan;
    if(index<0||scope_acquisition_plan_for((unsigned)index,ui->conversion_hz,&plan))return settings_failure(ui,5);
    /* Validate and map before committing. Stored origin conversion never controls
     * the live platform, and the retained record keeps its rate and caches. */
    ui->timebase_index=index;ui->adc_channel=settings->selected_channel;
    ui->trigger_channel=settings->trigger_channel;ui->trigger_level_mv=settings->trigger_level_mv;
    ui->trigger_rising=(settings->flags&SCOPE_SETTINGS_RISING)!=0;ui->trigger_normal=(settings->flags&SCOPE_SETTINGS_NORMAL)!=0;
    ui->dual_display=(settings->flags&SCOPE_SETTINGS_DUAL)!=0;ui->grid_on=(settings->flags&SCOPE_SETTINGS_GRID)!=0;
    ui->cursors_on=(settings->flags&SCOPE_SETTINGS_CURSORS)!=0;ui->language=(ScopeLanguage)settings->language;
    for(unsigned c=0;c<2;c++){
        ui->channel_scale_index[c]=(int)settings->scale_index[c];ui->channel_position_mdiv[c]=settings->position_mdiv[c];
        ui->cursor_positions[c]=settings->cursor[c];
    }
    ui->scale_index=ui->channel_scale_index[ui->adc_channel];ui->channel_config_target=ui->adc_channel;ui->cursor_dragging=-1;
    ui->single_armed=false;ui->single_complete=false;ui->adc_triggered=false;
    ui->adc_waiting=ui->model.running&&ui->trigger_normal;ui->adc_display_start=0;ui->adc_trigger_crossing=0;
    ui->capture_epoch++;ui->autoset_notice_s=0;ui->settings_notice=notice;localize(ui);redraw(ui);return true;
}
bool scope_ui_settings_load(ScopeUI *ui){
    if(!settings_live(ui)||!ui->settings_valid)return settings_failure(ui,1);
    ScopeSettings settings;
    if(scope_settings_decode(ui->settings_blob,sizeof(ui->settings_blob),&settings))return settings_failure(ui,5);
    if(!settings_apply(ui,&settings,3))return false;
    ui->settings_cache=settings;return true;
}
bool scope_ui_import_settings(ScopeUI *ui,const uint8_t *blob,size_t length){
    if(!settings_live(ui)||!ui_buffer_disjoint(ui,blob,length))return false;
    ScopeSettings settings;
    if(scope_settings_decode(blob,length,&settings)||memory_timebase_index(ui,settings.timebase_ns)<0)return false;
    return settings_apply(ui,&settings,3);
}
bool scope_ui_settings_defaults(ScopeUI *ui){
    if(!settings_live(ui))return settings_failure(ui,1);
    ScopeSettings settings;
    if(scope_settings_defaults(ui->conversion_hz,&settings))return settings_failure(ui,5);
    return settings_apply(ui,&settings,4);
}
bool scope_ui_show_parameters(ScopeUI *ui,bool show){
    if(!ui||!ui->parameter_panel)return false;
    if(show){
        if(!settings_live(ui))return settings_failure(ui,1);
        ui->settings_notice=0;update_parameters(ui);lv_obj_remove_flag(ui->parameter_panel,LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(ui->parameter_panel);
    }else lv_obj_add_flag(ui->parameter_panel,LV_OBJ_FLAG_HIDDEN);
    return true;
}
static void parameter_event(lv_event_t *e){
    ScopeUI *ui=lv_event_get_user_data(e);(void)scope_ui_show_parameters(ui,lv_obj_has_flag(ui->parameter_panel,LV_OBJ_FLAG_HIDDEN));
}
static void parameter_action_event(lv_event_t *e){
    ScopeUI *ui=lv_event_get_user_data(e);lv_obj_t *object=lv_event_get_target(e);
    if(object==ui->parameter_save_button)(void)scope_ui_settings_save(ui);
    else if(object==ui->parameter_load_button)(void)scope_ui_settings_load(ui);
    else if(object==ui->parameter_default_button)(void)scope_ui_settings_defaults(ui);
}
static void storage_entry_event(lv_event_t *e){
    ScopeUI *ui=lv_event_get_user_data(e);
    if(ui->storage_open)ui->storage_open(ui->storage_context,lv_event_get_target(e)==ui->tf_parameter_button?1u:0u);
}
static void memory_preferences(ScopeUI *ui,const ScopeRecordMeta *m,int index){
    ui->timebase_index=index;ui->adc_channel=m->selected_channel;
    for(unsigned c=0;c<2;c++){
        ui->channel_scale_index[c]=(int)m->scale_index[c];ui->channel_position_mdiv[c]=m->position_mdiv[c];
        ui->cursor_positions[c]=m->cursor[c];
    }
    ui->scale_index=ui->channel_scale_index[ui->adc_channel];ui->channel_config_target=ui->adc_channel;
    ui->trigger_channel=m->trigger_channel;ui->trigger_level_mv=m->trigger_level_mv;
    ui->trigger_normal=(m->flags&SCOPE_RECORD_NORMAL)!=0;ui->trigger_rising=(m->flags&SCOPE_RECORD_RISING)!=0;
    ui->dual_display=(m->flags&SCOPE_RECORD_DUAL)!=0;ui->cursors_on=(m->flags&SCOPE_RECORD_CURSORS)!=0;
    ui->grid_on=(m->flags&SCOPE_RECORD_GRID)!=0;ui->bars_on=false;ui->cursor_dragging=-1;
}
static void memory_backup_live(ScopeUI *ui){
    ScopeLiveViewBackup *b=&ui->live_view_backup;
    b->record=ui->adc_record;b->count=ui->adc_count;b->rate=ui->adc_rate;b->id=ui->adc_id;b->channel=ui->adc_channel;
    b->info=ui->adc_info;b->calibration=ui->record_calibration;memcpy(b->metrics,ui->adc_metrics,sizeof(b->metrics));memcpy(b->measurement,ui->measurement,sizeof(b->measurement));
    b->timebase_index=ui->timebase_index;b->scale_index=ui->scale_index;b->config_target=ui->channel_config_target;
    memcpy(b->channel_scale_index,ui->channel_scale_index,sizeof(b->channel_scale_index));
    memcpy(b->channel_position_mdiv,ui->channel_position_mdiv,sizeof(b->channel_position_mdiv));
    memcpy(b->cursor_positions,ui->cursor_positions,sizeof(b->cursor_positions));
    b->trigger_channel=ui->trigger_channel;b->trigger_level_mv=ui->trigger_level_mv;b->trigger_normal=ui->trigger_normal;b->trigger_rising=ui->trigger_rising;
    b->adc_triggered=ui->adc_triggered;b->adc_waiting=ui->adc_waiting;b->display_start=ui->adc_display_start;b->trigger_crossing=ui->adc_trigger_crossing;
    b->dual_display=ui->dual_display;b->cursors_on=ui->cursors_on;b->grid_on=ui->grid_on;b->bars_on=ui->bars_on;
}
static bool record_export(ScopeUI *ui,uint8_t *blob,size_t capacity,ScopeRecordMeta *out){
    if(!ui||ui->replay_active||scope_ui_autoset_busy(ui)||!ui->adc_record||ui->adc_channel>=2||
       !ui->adaptive_sampling||!ui->hardware_trigger||ui->adc_count!=SCOPE_RECORD_WORDS)return false;
    ScopeRecordMeta m={0};m.count=ui->adc_count;m.rate_hz=ui->adc_rate;m.conversion_hz=ui->adc_info.conversion_hz;
    m.filter_kind=ui->adc_info.filter_kind;m.record_id=ui->adc_id;m.capture_epoch=ui->adc_info.capture_epoch;
    m.timebase_ns=physical_timebase_ns(ui);
    double span=10*current_timebase(ui)*ui->adc_rate;
    if(!isfinite(ui->adc_display_start)||!isfinite(ui->adc_trigger_crossing)||span<1||span>4095||
       ui->adc_display_start<0||ui->adc_display_start+span>4095+1e-7)return false;
    m.view_start_q16=(uint32_t)lround(ui->adc_display_start*65536);m.trigger_crossing_q16=(uint32_t)lround(ui->adc_trigger_crossing*65536);
    m.flags=(ui->adc_triggered?SCOPE_RECORD_TRIGGERED:0)|(ui->trigger_rising?SCOPE_RECORD_RISING:0)|
        (ui->trigger_normal?SCOPE_RECORD_NORMAL:0)|(ui->dual_display?SCOPE_RECORD_DUAL:0)|
        (ui->cursors_on?SCOPE_RECORD_CURSORS:0)|(ui->grid_on?SCOPE_RECORD_GRID:0);
    m.trigger_channel=ui->trigger_channel;m.trigger_level_mv=ui->trigger_level_mv;m.selected_channel=ui->adc_channel;
    m.calibration=ui->record_calibration;
    if(m.calibration.flags&&scope_calibration_trigger_q8(&m.calibration,m.trigger_channel,m.trigger_level_mv,&m.trigger_level_q8))return false;
    m.trigger_after_index=ui->adc_triggered?(uint32_t)floor(ui->adc_trigger_crossing)+1:0;
    for(unsigned c=0;c<2;c++){m.scale_index[c]=(uint32_t)ui->channel_scale_index[c];m.position_mdiv[c]=ui->channel_position_mdiv[c];m.cursor[c]=ui->cursor_positions[c];}
    size_t written;
    if(scope_record_encode(&m,ui->adc_record,blob,capacity,&written))return false;
    *out=m;return true;
}
bool scope_ui_export_record(ScopeUI *ui,uint8_t *blob,size_t capacity){
    if(!ui_buffer_disjoint(ui,blob,capacity))return false;
    ScopeRecordMeta m;return record_export(ui,blob,capacity,&m);
}
bool scope_ui_memory_save(ScopeUI *ui){
    if(!ui)return false;
    ScopeRecordMeta m;
    if(!record_export(ui,ui->memory_blob,sizeof(ui->memory_blob),&m))return false;
    ui->memory_meta=m;ui->memory_valid=true;ui->memory_notice=2;update_memory(ui);return true;
}
/* Map the physical axis before decoding into the replay destination. All codec
 * failures are atomic, so a damaged slot cannot overwrite an active replay. */
static int memory_decode(ScopeUI *ui,ScopeRecordMeta *m){
    const uint8_t *p=ui->memory_blob+40;
    uint32_t ns=(uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
    int index=memory_timebase_index(ui,ns);
    if(index<0||scope_record_decode(ui->memory_blob,sizeof(ui->memory_blob),m,ui->replay_words,SCOPE_RECORD_WORDS))return -1;
    return index;
}
static bool record_load(ScopeUI *ui,const uint8_t *blob,size_t length,const char *filename){
    if(!ui||scope_ui_autoset_busy(ui)||ui->adc_channel>=2||!ui->adc_record||!ui->adaptive_sampling||!ui->hardware_trigger||!blob||length!=SCOPE_RECORD_BLOB_BYTES)return false;
    const uint8_t*p=blob+40;uint32_t ns=(uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
    ScopeRecordMeta m;int index=memory_timebase_index(ui,ns);
    if(index>=0&&scope_record_decode(blob,length,&m,ui->replay_words,SCOPE_RECORD_WORDS))index=-1;
    if(index<0){ui->memory_notice=1;update_memory(ui);return false;}
    ScopeMeasurement measured[2];ScopeAdcMetrics metrics[2];
    /* The codec has validated every word and the nonzero rate/count. */
    if(scope_measurement_analyze_calibrated(ui->replay_words,m.count,m.rate_hz,&m.calibration,measured))return false;
    for(unsigned c=0;c<2;c++){
        double frequency=0;bool valid=scope_measurement_frequency(ui->replay_words,m.count,m.rate_hz,c,&frequency);
        metrics[c]=(ScopeAdcMetrics){measured[c].vpp_v,measured[c].rms_total_v,frequency,valid};
    }
    if(!ui->replay_active)memory_backup_live(ui);
    memory_preferences(ui,&m,index);ui->replay_meta=m;ui->replay_active=true;ui->model.running=false;
    ui->replay_from_tf=filename!=NULL;
    if(filename)memcpy(ui->replay_filename,filename,13);else ui->replay_filename[0]=0;
    ui->single_armed=false;ui->single_complete=false;ui->adc_waiting=false;ui->capture_epoch++;
    ui->adc_record=ui->replay_words;ui->adc_count=m.count;ui->adc_rate=m.rate_hz;ui->adc_id=m.record_id;
    ui->record_calibration=m.calibration;
    unsigned rate_index=0;
    for(unsigned i=0;i<SCOPE_50M_TIMEBASE_COUNT;i++){
        ScopeCapturePlan p;
        if(!scope_acquisition_plan_for(i,m.conversion_hz,&p)&&p.rate_hz==m.rate_hz&&p.filter_kind==m.filter_kind){rate_index=p.rate_index;break;}
    }
    ui->adc_info=(ScopeRecordInfo){m.capture_epoch,rate_index,m.conversion_hz,m.filter_kind};
    ui->adc_triggered=(m.flags&SCOPE_RECORD_TRIGGERED)!=0;
    ui->adc_display_start=m.view_start_q16/65536.0;ui->adc_trigger_crossing=m.trigger_crossing_q16/65536.0;
    memcpy(ui->measurement,measured,sizeof(measured));memcpy(ui->adc_metrics,metrics,sizeof(metrics));
    ui->autoset_notice_s=0;ui->memory_notice=0;redraw(ui);return true;
}
bool scope_ui_memory_load(ScopeUI *ui){
    return ui&&ui->memory_valid&&record_load(ui,ui->memory_blob,sizeof(ui->memory_blob),NULL);
}
bool scope_ui_import_record(ScopeUI *ui,const uint8_t *blob,size_t length,const char *filename){
    if(!ui_buffer_disjoint(ui,blob,length)||!filename||strlen(filename)!=12||filename[0]!='W'||strcmp(filename+8,".SCW"))return false;
    unsigned id=0;for(unsigned i=1;i<8;i++){if(filename[i]<'0'||filename[i]>'9')return false;id=id*10+(unsigned)(filename[i]-'0');}
    if(!id)return false;
    char name[13];memcpy(name,filename,13);return record_load(ui,blob,length,name);
}
bool scope_ui_memory_restore_settings(ScopeUI *ui){
    if(!ui||!ui->memory_valid||ui->replay_active||scope_ui_autoset_busy(ui)||!ui->adc_record||ui->adc_channel>=2)return false;
    ScopeRecordMeta m;int index=memory_decode(ui,&m);
    if(index<0){ui->memory_notice=1;update_memory(ui);return false;}
    memory_preferences(ui,&m,index);ui->capture_epoch++;ui->single_armed=false;ui->single_complete=false;
    ui->adc_triggered=false;ui->adc_waiting=ui->model.running&&ui->trigger_normal;
    ui->memory_notice=3;redraw(ui);return true;
}
bool scope_ui_memory_return_live(ScopeUI *ui){
    if(!ui||!ui->replay_active)return false;
    const ScopeLiveViewBackup *b=&ui->live_view_backup;
    ui->adc_record=b->record;ui->adc_count=b->count;ui->adc_rate=b->rate;ui->adc_id=b->id;ui->adc_channel=b->channel;
    ui->adc_info=b->info;ui->record_calibration=b->calibration;memcpy(ui->adc_metrics,b->metrics,sizeof(b->metrics));memcpy(ui->measurement,b->measurement,sizeof(b->measurement));
    ui->timebase_index=b->timebase_index;ui->scale_index=b->scale_index;ui->channel_config_target=b->config_target;
    memcpy(ui->channel_scale_index,b->channel_scale_index,sizeof(b->channel_scale_index));memcpy(ui->channel_position_mdiv,b->channel_position_mdiv,sizeof(b->channel_position_mdiv));
    memcpy(ui->cursor_positions,b->cursor_positions,sizeof(b->cursor_positions));ui->cursor_dragging=-1;
    ui->trigger_channel=b->trigger_channel;ui->trigger_level_mv=b->trigger_level_mv;ui->trigger_normal=b->trigger_normal;ui->trigger_rising=b->trigger_rising;
    ui->adc_display_start=b->display_start;ui->adc_trigger_crossing=b->trigger_crossing;ui->adc_triggered=b->adc_triggered;
    ui->dual_display=b->dual_display;ui->cursors_on=b->cursors_on;ui->grid_on=b->grid_on;ui->bars_on=b->bars_on;
    ui->replay_active=false;ui->replay_from_tf=false;ui->replay_filename[0]=0;ui->model.running=true;ui->single_armed=false;ui->single_complete=false;
    ui->adc_waiting=ui->trigger_normal;ui->capture_epoch++;ui->memory_notice=0;redraw(ui);return true;
}
bool scope_ui_show_memory(ScopeUI *ui,bool show){
    if(!ui||!ui->memory_panel||scope_ui_autoset_busy(ui))return false;
    if(show){ui->memory_notice=0;update_memory(ui);lv_obj_remove_flag(ui->memory_panel,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(ui->memory_panel);}
    else lv_obj_add_flag(ui->memory_panel,LV_OBJ_FLAG_HIDDEN);
    return true;
}
static void memory_event(lv_event_t *e){ScopeUI *ui=lv_event_get_user_data(e);(void)scope_ui_show_memory(ui,lv_obj_has_flag(ui->memory_panel,LV_OBJ_FLAG_HIDDEN));}
static void memory_action_event(lv_event_t *e){
    ScopeUI *ui=lv_event_get_user_data(e);lv_obj_t *o=lv_event_get_target(e);
    bool ok=o==ui->memory_save_button?scope_ui_memory_save(ui):o==ui->memory_load_button?scope_ui_memory_load(ui):
        o==ui->memory_settings_button?scope_ui_memory_restore_settings(ui):scope_ui_memory_return_live(ui);
    if(!ok){ui->memory_notice=1;update_memory(ui);}
}
static void memory_enable(lv_obj_t *o,bool enabled){if(enabled)lv_obj_remove_state(o,LV_STATE_DISABLED);else lv_obj_add_state(o,LV_STATE_DISABLED);}
static void update_parameters(ScopeUI *ui){
    if(!ui->parameter_panel)return;
    bool zh=ui->language==SCOPE_ZH_CN,live=settings_live(ui),adc=ui->adc_record&&ui->adc_channel<2;
    button_text(ui->parameter_button,zh?"参数管理":"PARAMETERS");
    lv_label_set_text(ui->parameter_title,zh?"参数管理":"PARAMETERS");
    button_text(ui->parameter_save_button,zh?(ui->settings_valid?"更新参数":"暂存参数"):(ui->settings_valid?"UPDATE SETTINGS":"STORE SETTINGS"));
    button_text(ui->parameter_load_button,zh?"恢复参数":"RESTORE SETTINGS");
    button_text(ui->parameter_default_button,zh?"恢复默认":"RESTORE DEFAULTS");button_text(ui->parameter_close_button,tr(ui,CLOSE));
    if(ui->tf_parameter_button)button_text(ui->tf_parameter_button,zh?"TF 存储":"TF STORAGE");
    const char *status=ui->settings_notice==1?(zh?"操作未执行，请检查采集模式和暂存参数。":"Action unavailable: check acquisition and saved settings."):
        ui->settings_notice==2?(zh?"已暂存参数。":"Settings stored."):
        ui->settings_notice==3?(zh?"已恢复参数。":"Settings restored."):
        ui->settings_notice==4?(zh?"已恢复默认参数。":"Default settings restored."):
        ui->settings_notice==5?(zh?"参数无效或时基不支持。":"Invalid settings or unsupported timebase."):
        (zh?(ui->settings_valid?"已有一份暂存参数。":"暂无暂存参数。"):(ui->settings_valid?"One saved configuration available.":"No saved configuration."));
    char text[256];snprintf(text,sizeof(text),"%s\n%s",zh?"内存暂存，断电清除。":"Stored in memory. Cleared on power loss.",status);
    lv_label_set_text(ui->parameter_note,text);
    if(adc){lv_obj_remove_flag(ui->parameter_button,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(ui->bars_button,LV_OBJ_FLAG_HIDDEN);}
    else {lv_obj_add_flag(ui->parameter_button,LV_OBJ_FLAG_HIDDEN);lv_obj_remove_flag(ui->bars_button,LV_OBJ_FLAG_HIDDEN);}
    memory_enable(ui->parameter_button,live);memory_enable(ui->parameter_save_button,live);
    memory_enable(ui->parameter_load_button,live&&ui->settings_valid);memory_enable(ui->parameter_default_button,live);
}
static void update_memory(ScopeUI *ui){
    if(!ui->memory_panel)return;
    bool zh=ui->language==SCOPE_ZH_CN;
    button_text(ui->memory_button,zh?"记录":"RECORD");lv_label_set_text(ui->memory_title,zh?"内存记录":"MEMORY RECORD");
    button_text(ui->memory_save_button,zh?(ui->memory_valid?"更新暂存":"暂存当前"):(ui->memory_valid?"UPDATE RECORD":"STORE CURRENT"));
    button_text(ui->memory_load_button,zh?"回放暂存":"REPLAY RECORD");
    button_text(ui->memory_settings_button,zh?"恢复暂存设置":"RESTORE STORED SETTINGS");
    button_text(ui->memory_live_button,zh?"返回采集":"RETURN TO ACQUISITION");button_text(ui->memory_close_button,tr(ui,CLOSE));
    if(ui->tf_record_button)button_text(ui->tf_record_button,zh?"TF 存储":"TF STORAGE");
    const char *status=ui->memory_notice==1?(zh?"操作未执行，请检查记录和时基。":"Action unavailable: check record and timebase."):
        ui->memory_notice==2?(zh?"已暂存当前记录。":"Current record stored."):ui->memory_notice==3?(zh?"已恢复暂存设置。":"Stored settings restored."):
        (zh?(ui->memory_valid?"已有一条暂存记录。":"暂无暂存记录。"):(ui->memory_valid?"One record available.":"No stored record."));
    char text[256];snprintf(text,sizeof(text),"%s\n%s",zh?"一条完整双通道记录，断电清除。":"One full dual-channel record. Cleared on power loss.",status);lv_label_set_text(ui->memory_note,text);
    bool live=!ui->replay_active&&!scope_ui_autoset_busy(ui)&&ui->adc_record&&ui->adc_channel<2&&ui->adaptive_sampling&&ui->hardware_trigger;
    memory_enable(ui->memory_save_button,live);memory_enable(ui->memory_load_button,ui->memory_valid&&(live||ui->replay_active));
    memory_enable(ui->memory_settings_button,ui->memory_valid&&live);memory_enable(ui->memory_live_button,ui->replay_active);
    if(ui->replay_active){
        lv_label_set_text(ui->run_label,zh?"返回采集":"RETURN TO ACQUISITION");
        lv_label_set_text(ui->state_label,zh?"内存回放":"MEMORY REPLAY");
        lv_label_set_text(ui->subtitle_label,zh?"内存回放":"MEMORY REPLAY");
        lv_label_set_text(ui->axes_label,zh?"回放原始时间轴":"REPLAY: ORIGINAL RECORD TIME AXIS");
        snprintf(text,sizeof(text),zh?"内存回放  |  断电清除  |  采样率 %u Sa/s  |  %u 点":"MEMORY REPLAY  |  CLEARED ON POWER LOSS  |  %u Sa/s  |  %u pts",ui->adc_rate,ui->adc_count);lv_label_set_text(ui->footer_label,text);
        if(ui->replay_from_tf){
            lv_label_set_text(ui->state_label,zh?"TF 回放":"TF REPLAY");lv_label_set_text(ui->subtitle_label,zh?"TF 回放":"TF REPLAY");
            snprintf(text,sizeof(text),zh?"TF 回放  |  %s  |  采样率 %u Sa/s  |  %u 点":"TF REPLAY  |  %s  |  %u Sa/s  |  %u pts",ui->replay_filename,ui->adc_rate,ui->adc_count);lv_label_set_text(ui->footer_label,text);
        }
    }
    memory_enable(ui->autoset_button,!ui->replay_active);
    if(ui->replay_active){
        memory_enable(ui->single_button,false);memory_enable(ui->trigger_button,false);memory_enable(ui->trigger_source_button,false);
        memory_enable(ui->trigger_mode_button,false);memory_enable(ui->wave_buttons[2],false);
        memory_enable(ui->adjust_buttons[4],false);memory_enable(ui->adjust_buttons[5],false);
    }else{memory_enable(ui->trigger_mode_button,true);memory_enable(ui->wave_buttons[2],true);}
}
static void autoset_restore(ScopeUI *ui,ScopeAutosetState state,bool keep_stopped){
    const ScopeAutosetBackup *b=&ui->autoset_backup;
    ui->timebase_index=b->timebase_index;ui->scale_index=b->scale_index;
    memcpy(ui->channel_scale_index,b->channel_scale_index,sizeof(b->channel_scale_index));
    memcpy(ui->channel_position_mdiv,b->channel_position_mdiv,sizeof(b->channel_position_mdiv));
    ui->trigger_channel=b->trigger_channel;ui->trigger_normal=b->trigger_normal;
    ui->trigger_rising=b->trigger_rising;ui->trigger_level_mv=b->trigger_level_mv;
    ui->model.running=b->running&&!keep_stopped;ui->single_armed=false;ui->single_complete=b->single_complete;
    ui->adc_triggered=b->adc_triggered;ui->adc_waiting=b->adc_waiting&&!keep_stopped;
    ui->adc_display_start=b->adc_display_start;ui->adc_trigger_crossing=b->adc_trigger_crossing;
    ui->capture_epoch++;ui->autoset_state=state;ui->autoset_notice_s=5;ui->autoset_need_wider=false;redraw(ui);
}
void scope_ui_autoset_cancel(ScopeUI *ui){
    if(scope_ui_autoset_busy(ui))autoset_restore(ui,SCOPE_AUTO_CANCELED,!ui->model.running);
}
bool scope_ui_autoset_start(ScopeUI *ui){
    if(!ui||ui->replay_active||scope_ui_autoset_busy(ui))return false;
    ui->autoset_notice_s=5;
    ui->autoset_need_wider=false;
    if(!ui->adc_record||ui->adc_count<2||!ui->adc_rate){ui->autoset_state=SCOPE_AUTO_NO_RECORD;update_autoset(ui);return false;}
    if(ui->adc_channel>=2||!ui->adaptive_sampling||!ui->hardware_trigger||ui->single_armed||ui->bars_on){
        ui->autoset_state=SCOPE_AUTO_UNSUPPORTED;update_autoset(ui);return false;
    }
    unsigned ch=ui->adc_channel;const ScopeMeasurement *m=&ui->measurement[ch];
    double frequency=ui->adc_metrics[ch].frequency_hz;
    if(m->overrange){ui->autoset_state=SCOPE_AUTO_OVERRANGE;update_autoset(ui);return false;}
    if(!m->valid||m->vpp_v<code_span_volts(&ui->record_calibration,ch,32)-1e-12){ui->autoset_state=SCOPE_AUTO_NO_SIGNAL;update_autoset(ui);return false;}
    if(!ui->adc_metrics[ch].frequency_valid||!isfinite(frequency)||frequency<=0||frequency<2.0*ui->adc_rate/(ui->adc_count-1)){
        ui->autoset_state=SCOPE_AUTO_NO_SIGNAL;
        ui->autoset_need_wider=ui->adc_metrics[ch].frequency_valid&&isfinite(frequency)&&frequency>0&&
            frequency<2.0*ui->adc_rate/(ui->adc_count-1);
        update_autoset(ui);return false;
    }
    ScopeAutosetPlan plan;
    ScopeAutosetResult result=scope_autoset_plan_calibrated(ui->conversion_hz,ui->adc_count,ui->adc_rate,ch,frequency,ui->measurement,&ui->record_calibration,&plan);
    if(result!=SCOPE_AUTOSET_OK){
        ui->autoset_state=result==SCOPE_AUTOSET_OVERRANGE?SCOPE_AUTO_OVERRANGE:
            result==SCOPE_AUTOSET_NO_SIGNAL?SCOPE_AUTO_NO_SIGNAL:SCOPE_AUTO_UNSUPPORTED;
        update_autoset(ui);return false;
    }
    ScopeCapturePlan capture;if(scope_acquisition_plan_for(plan.timebase_index,ui->conversion_hz,&capture))return false;
    ScopeAutosetBackup *b=&ui->autoset_backup;
    b->timebase_index=ui->timebase_index;b->scale_index=ui->scale_index;
    memcpy(b->channel_scale_index,ui->channel_scale_index,sizeof(b->channel_scale_index));
    memcpy(b->channel_position_mdiv,ui->channel_position_mdiv,sizeof(b->channel_position_mdiv));
    b->trigger_channel=ui->trigger_channel;b->trigger_normal=ui->trigger_normal;b->trigger_rising=ui->trigger_rising;
    b->trigger_level_mv=ui->trigger_level_mv;b->running=ui->model.running;b->single_complete=ui->single_complete;
    b->adc_triggered=ui->adc_triggered;b->adc_waiting=ui->adc_waiting;
    b->adc_display_start=ui->adc_display_start;b->adc_trigger_crossing=ui->adc_trigger_crossing;
    ui->autoset_plan=plan;ui->autoset_state=SCOPE_AUTO_WAITING;ui->autoset_elapsed_s=0;ui->autoset_notice_s=0;
    ui->autoset_deadline_s=fmax(2.0,2.0*4096/capture.rate_hz+1.0);
    ui->timebase_index=(int)plan.timebase_index;
    for(unsigned c=0;c<2;c++)if(plan.channel_mask&(1u<<c)){
        ui->channel_scale_index[c]=(int)plan.scale_index[c];ui->channel_position_mdiv[c]=plan.position_mdiv[c];
    }
    ui->scale_index=ui->channel_scale_index[ch];ui->trigger_channel=plan.trigger_channel;
    ui->trigger_normal=false;ui->trigger_rising=true;ui->trigger_level_mv=plan.level_mv;
    ui->model.running=true;ui->single_armed=false;ui->single_complete=false;ui->capture_epoch++;
    ui->adc_waiting=true;ui->cursor_dragging=-1;
    lv_obj_add_flag(ui->settings,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(ui->channel_panel,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui->measurement_panel,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui->memory_panel,LV_OBJ_FLAG_HIDDEN);
    /* Keep the displayed pointer and caches untouched until the confirming record.
       The poller therefore reuses its other DDR slot throughout this transaction. */
    update_autoset(ui);return true;
}
static void autoset_event(lv_event_t *e){(void)scope_ui_autoset_start(lv_event_get_user_data(e));}
static void autoset_cancel_event(lv_event_t *e){scope_ui_autoset_cancel(lv_event_get_user_data(e));}
static void redraw(ScopeUI *ui) {
    char text[192];
    bool adc=ui->adc_record&&ui->adc_channel<2;
    bool dual=adc&&ui->dual_display;
    if(adc)ui->channel_scale_index[ui->adc_channel]=ui->scale_index;
    else ui->sim_scale_index=ui->scale_index;
    lv_label_set_text(ui->subtitle_label,tr(ui,adc?SUBTITLE_BOARD:SUBTITLE));
    lv_label_set_text(ui->axes_label,tr(ui,AXES));
    lv_label_set_text(ui->footer_label,tr(ui,FOOTER));
    lv_label_set_text(ui->settings_note,tr(ui,SETTINGS_NOTE));
    lv_label_set_text(ui->wave_title,tr(ui,WAVE_TITLE));
    for(int i=0;i<SCOPE_POINTS;i++) {
        ui->secondary_samples[i]=LV_CHART_POINT_NONE;
        /* Visual drift is deliberately independent from a wall-clock acquisition rate.
           Timebase is physical seconds/division over ten horizontal divisions. */
        double t=(double)i/(SCOPE_POINTS-1)*10*current_timebase(ui);
        if(adc){
            double at=ui->adc_display_start+t*ui->adc_rate;unsigned n=(unsigned)at;
            if(n>=ui->adc_count){ui->samples[i]=LV_CHART_POINT_NONE;continue;}
            unsigned shift=ui->adc_channel*16;
            double code=(ui->adc_record[n]>>shift)&1023u;
            if(n+1<ui->adc_count)code+=(at-n)*((double)((ui->adc_record[n+1]>>shift)&1023u)-code);
            double scale=scales[ui->scale_index];
            ui->samples[i]=scope_voltage_to_plot(scope_calibration_voltage(&ui->record_calibration,ui->adc_channel,code)+ui->channel_position_mdiv[ui->adc_channel]*scale/1000.0,scale);
            if(dual){
                unsigned other_shift=(1-ui->adc_channel)*16;
                double other_code=(ui->adc_record[n]>>other_shift)&1023u;
                if(n+1<ui->adc_count)other_code+=(at-n)*((double)((ui->adc_record[n+1]>>other_shift)&1023u)-other_code);
                double other_scale=scales[ui->channel_scale_index[1-ui->adc_channel]];
                ui->secondary_samples[i]=scope_voltage_to_plot(scope_calibration_voltage(&ui->record_calibration,1-ui->adc_channel,other_code)+ui->channel_position_mdiv[1-ui->adc_channel]*other_scale/1000.0,other_scale);
            }
        }else{
            double phase=ui->model.elapsed_s*.15/ui->model.frequency_hz;
            ui->samples[i]=scope_voltage_to_plot(scope_sample(&ui->model,t+phase),scales[ui->scale_index]);
        }
    }
    lv_chart_hide_series(ui->chart,ui->secondary_series,!dual);
    lv_chart_set_series_color(ui->chart,ui->series,color(adc?channel_colors[ui->adc_channel]:CYAN));
    lv_chart_set_series_color(ui->chart,ui->secondary_series,color(adc?channel_colors[1-ui->adc_channel]:channel_colors[1]));
    lv_chart_refresh(ui->chart);
    lv_label_set_text(ui->run_label,tr(ui,ui->model.running?PAUSE:RUN));
    lv_obj_set_style_bg_color(ui->run_button,color(ui->model.running?0x174b45:0x684027),0);
    lv_label_set_text(ui->state_label,tr(ui,ui->model.running?STATE_RUN:STATE_STOP));
    for(int i=0;i<3;i++) lv_obj_set_style_bg_color(ui->wave_buttons[i],color(i==(int)ui->model.wave?0x21715f:0x223146),0);
    snprintf(text,sizeof(text),tr(ui,TIME_FMT),current_time_name(ui));
    lv_label_set_text(ui->time_label,text);
    snprintf(text,sizeof(text),tr(ui,SCALE_FMT),scales[ui->scale_index]);
    lv_label_set_text(ui->scale_label,text);
    snprintf(text,sizeof(text),tr(ui,SOURCE_FMT),ui->model.frequency_hz/1000);
    lv_label_set_text(ui->frequency_label,text);
    lv_label_set_text(ui->metric_titles[0],tr(ui,VPP_TITLE));
    snprintf(text,sizeof(text),"%.2f V",2*ui->model.amplitude_v); lv_label_set_text(ui->vpp_label,text);
    snprintf(text,sizeof(text),"%.3f V",scope_rms(&ui->model)); lv_label_set_text(ui->rms_label,text);
    snprintf(text,sizeof(text),"%.2f kHz",ui->model.frequency_hz/1000); lv_label_set_text(ui->readout_label,text);
    lv_chart_set_div_line_count(ui->chart,ui->grid_on?9:0,ui->grid_on?11:0);
    if(ui->bars_on) {lv_obj_add_flag(ui->chart,LV_OBJ_FLAG_HIDDEN); lv_obj_remove_flag(ui->bars,LV_OBJ_FLAG_HIDDEN);}
    else {lv_obj_remove_flag(ui->chart,LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(ui->bars,LV_OBJ_FLAG_HIDDEN);}
    button_text(ui->grid_button,tr(ui,ui->grid_on?GRID_ON:GRID_OFF));
    button_text(ui->bars_button,tr(ui,ui->bars_on?BARS_ON:BARS_OFF));
    if(ui->single_button){
        bool capable=single_capable(ui);
        button_text(ui->single_button,ui->language==SCOPE_ZH_CN?"单次采集":"SINGLE");
        if(adc)snprintf(text,sizeof(text),"%s",ui->language==SCOPE_ZH_CN?(ui->trigger_rising?"边沿：上升":"边沿：下降"):(ui->trigger_rising?"EDGE: RISING":"EDGE: FALLING"));
        else snprintf(text,sizeof(text),"%s",ui->language==SCOPE_ZH_CN?"边沿触发":"EDGE TRIGGER");
        button_text(ui->trigger_button,text);
        snprintf(text,sizeof(text),ui->language==SCOPE_ZH_CN?"触发源：CH%u":"TRIG SRC: CH%u",ui->trigger_channel+1);
        button_text(ui->trigger_source_button,text);
        lv_obj_set_style_text_color(lv_obj_get_child(ui->trigger_source_button,0),color(channel_colors[ui->trigger_channel]),0);
        button_text(ui->trigger_mode_button,ui->language==SCOPE_ZH_CN?(ui->trigger_normal?"触发模式：正常":"触发模式：自动"):(ui->trigger_normal?"TRIGGER: NORMAL":"TRIGGER: AUTO"));
        if(capable)lv_obj_remove_state(ui->single_button,LV_STATE_DISABLED);else lv_obj_add_state(ui->single_button,LV_STATE_DISABLED);
        if(adc){lv_obj_remove_state(ui->trigger_button,LV_STATE_DISABLED);lv_obj_remove_state(ui->trigger_source_button,LV_STATE_DISABLED);}
        else {lv_obj_add_state(ui->trigger_button,LV_STATE_DISABLED);lv_obj_add_state(ui->trigger_source_button,LV_STATE_DISABLED);}
        if(ui->adc_record){
            lv_obj_remove_flag(ui->single_button,LV_OBJ_FLAG_HIDDEN);lv_obj_remove_flag(ui->trigger_button,LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(ui->trigger_source_button,LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(ui->trigger_mode_button,LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(ui->dual_button,LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_y(ui->settings_dialog,55);lv_obj_set_height(ui->settings_dialog,490);
            lv_obj_set_y(ui->settings_note,350);lv_obj_set_y(ui->close_button,414);
        }else{
            lv_obj_add_flag(ui->single_button,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(ui->trigger_button,LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui->trigger_source_button,LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui->trigger_mode_button,LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui->dual_button,LV_OBJ_FLAG_HIDDEN);
        }
    }
    button_text(ui->dual_button,ui->language==SCOPE_ZH_CN?(ui->dual_display?"通道显示：双通道":"通道显示：单通道"):(ui->dual_display?"CHANNEL DISPLAY: DUAL":"CHANNEL DISPLAY: SINGLE"));
    if(adc)lv_obj_remove_state(ui->dual_button,LV_STATE_DISABLED);else lv_obj_add_state(ui->dual_button,LV_STATE_DISABLED);
    for(unsigned ch=0;ch<2;ch++){
        if(dual){lv_obj_remove_flag(ui->channel_legend[ch],LV_OBJ_FLAG_HIDDEN);}
        else {lv_obj_add_flag(ui->channel_legend[ch],LV_OBJ_FLAG_HIDDEN);}
    }
    lv_obj_set_width(ui->axes_label,dual?530:744);
    if(ui->adc_record){
        button_text(ui->wave_buttons[0],"CH1");button_text(ui->wave_buttons[1],"CH2");
        button_text(ui->wave_buttons[2],ui->language==SCOPE_ZH_CN?"演示":"DEMO");
        lv_label_set_text(ui->wave_title,ui->language==SCOPE_ZH_CN?"测量通道":"MEASUREMENT SOURCE");
        for(unsigned i=0;i<3;i++)lv_obj_set_style_bg_color(ui->wave_buttons[i],color(i==ui->adc_channel?0x21715f:0x223146),0);
        for(unsigned i=4;i<6;i++)if(ui->adjust_buttons[i]){lv_obj_remove_state(ui->adjust_buttons[i],LV_STATE_DISABLED);lv_obj_set_style_opa(ui->adjust_buttons[i],255,0);}
        if(adc){
            lv_label_set_text(ui->settings_note,tr(ui,ui->adaptive_sampling?(ui->adc_info.filter_kind==SCOPE_FILTER_RAW?NOTE_RAW:NOTE_ADAPTIVE):NOTE_BOARD));
            if(ui->record_calibration.flags&(1u<<ui->adc_channel)){
                char note[256];snprintf(note,sizeof(note),ui->language==SCOPE_ZH_CN?
                    "%s。\n采样率不代表整机模拟带宽。\n小幅信号仍有剩余误差。":
                    "%s.\nSample rate is not analog bandwidth.\nSmall-signal errors remain.",
                    voltage_reference(ui,&ui->record_calibration,ui->adc_channel));
                lv_label_set_text(ui->settings_note,note);
            }
            snprintf(text,sizeof(text),ui->language==SCOPE_ZH_CN?"测量通道：CH%u":"MEASUREMENT: CH%u",ui->adc_channel+1);lv_label_set_text(ui->channel_label,text);
            lv_obj_set_style_text_color(ui->channel_label,color(channel_colors[ui->adc_channel]),0);
            lv_obj_set_style_text_color(ui->vpp_label,color(channel_colors[ui->adc_channel]),0);
            lv_label_set_text(ui->state_label,ui->language==SCOPE_ZH_CN?(ui->model.running?"采集中":"已停止"):(ui->model.running?"ACQUIRING":"STOPPED"));
            if(ui->single_complete)lv_label_set_text(ui->state_label,ui->language==SCOPE_ZH_CN?"单次完成":"SINGLE COMPLETE");
            else if(ui->single_armed)lv_label_set_text(ui->state_label,ui->language==SCOPE_ZH_CN?"等待单次触发":"SINGLE WAIT");
            else if(ui->model.running&&ui->adc_waiting)lv_label_set_text(ui->state_label,ui->language==SCOPE_ZH_CN?"等待触发":"WAITING");
            else if(ui->model.running&&ui->adc_triggered)lv_label_set_text(ui->state_label,ui->language==SCOPE_ZH_CN?"已触发":"TRIGGERED");
            snprintf(text,sizeof(text),ui->language==SCOPE_ZH_CN?"触发模式：%s  |  触发位置：20%%":"TRIGGER: %s  |  POSITION: 20%%",ui->language==SCOPE_ZH_CN?(ui->trigger_normal?"正常":"自动"):(ui->trigger_normal?"NORMAL":"AUTO"));lv_label_set_text(ui->axes_label,text);
            snprintf(text,sizeof(text),ui->language==SCOPE_ZH_CN?"触发电平 %+.2f V":"TRIG LEVEL %+.2f V",ui->trigger_level_mv/1000.0);lv_label_set_text(ui->frequency_label,text);
            snprintf(text,sizeof(text),ui->language==SCOPE_ZH_CN?"记录长度 %u 点\n记录时长 %.2f ms":"RECORD %u pts\nDURATION %.2f ms",ui->adc_count,1000.0*ui->adc_count/ui->adc_rate);lv_label_set_text(ui->source_note,text);
            char rate_text[24];
            if(ui->adc_rate>=1000000)snprintf(rate_text,sizeof(rate_text),"%.4g MSa/s",ui->adc_rate/1000000.0);
            else if(ui->adc_rate>=1000)snprintf(rate_text,sizeof(rate_text),"%.4g kSa/s",ui->adc_rate/1000.0);
            else snprintf(rate_text,sizeof(rate_text),"%u Sa/s",ui->adc_rate);
            if(ui->adaptive_sampling&&ui->adc_info.filter_kind==SCOPE_FILTER_RAW){
                snprintf(text,sizeof(text),ui->language==SCOPE_ZH_CN?"采样率 %s  |  直采  |  存储深度 %u 点":"SAMPLE RATE %s  |  RAW  |  MEMORY DEPTH %u pts",rate_text,ui->adc_count);
            }else if(ui->adaptive_sampling){
                double passband=ui->adc_rate/10.0;const char *unit="Hz";
                if(passband>=1000){passband/=1000;unit="kHz";}
                snprintf(text,sizeof(text),ui->language==SCOPE_ZH_CN?"采样率 %s  |  数字通带 0–%.4g %s  |  存储深度 %u 点":"SAMPLE RATE %s  |  DIGITAL PASSBAND 0–%.4g %s  |  MEMORY DEPTH %u pts",rate_text,passband,unit,ui->adc_count);
            }else snprintf(text,sizeof(text),ui->language==SCOPE_ZH_CN?"采样率 %s  |  存储深度 %u 点":"SAMPLE RATE %s  |  MEMORY DEPTH %u pts",rate_text,ui->adc_count);
            lv_label_set_text(ui->footer_label,text);
            lv_label_set_text(ui->metric_titles[1],ui->language==SCOPE_ZH_CN?"总有效值 Vrms":"RMS (AC+DC)");
            lv_label_set_text(ui->metric_titles[2],tr(ui,FREQ_TITLE));
            const ScopeAdcMetrics *m=&ui->adc_metrics[ui->adc_channel];
            snprintf(text,sizeof(text),"%.3f V",m->vpp_v);lv_label_set_text(ui->vpp_label,text);
            snprintf(text,sizeof(text),"%.3f V",m->rms_v);lv_label_set_text(ui->rms_label,text);
            if(m->frequency_valid)snprintf(text,sizeof(text),"%.3f kHz",m->frequency_hz/1000.0);else snprintf(text,sizeof(text),"--");
            lv_label_set_text(ui->readout_label,text);
        }else{
            lv_obj_set_style_text_color(ui->channel_label,color(CYAN),0);
            lv_obj_set_style_text_color(ui->vpp_label,color(CYAN),0);
            lv_label_set_text(ui->channel_label,tr(ui,CHANNEL));lv_label_set_text(ui->source_note,tr(ui,SOURCE_NOTE));
            lv_label_set_text(ui->metric_titles[1],tr(ui,RMS_TITLE));lv_label_set_text(ui->metric_titles[2],tr(ui,FREQ_TITLE));
        }
    }
    update_cursors(ui);
    update_channel_controls(ui);
    update_measurements(ui);
    update_spectrum(ui);
    update_autoset(ui);
    update_memory(ui);
    update_parameters(ui);
}
const char *scope_ui_acquisition_error_text(const ScopeUI *ui){
    return ui->language==SCOPE_ZH_CN?"采集异常，已停止":"ACQUISITION ERROR";
}
bool scope_ui_set_adc_record(ScopeUI *ui,const uint32_t *record,unsigned count,unsigned rate,unsigned id,const ScopeAdcMetrics metrics[2]) {
    return scope_ui_set_adc_record_info(ui,record,count,rate,id,metrics,NULL);
}
void scope_ui_enable_adaptive_sampling(ScopeUI *ui,bool enabled){
    if(!ui||ui->replay_active||scope_ui_autoset_busy(ui))return;
    if(ui->adaptive_sampling!=enabled){
        ui->adaptive_sampling=enabled;
        int legacy_limit=(int)timebase_prefix(ui->conversion_hz)+6;
        if(!enabled&&ui->timebase_index>legacy_limit)ui->timebase_index=legacy_limit;
        ui->capture_epoch++;ui->single_armed=false;ui->single_complete=false;ui->adc_triggered=false;
        ui->adc_waiting=ui->model.running&&ui->trigger_normal&&ui->adc_channel<2;
    }
    redraw(ui);
}
int scope_ui_capture_plan(const ScopeUI *ui,ScopeCapturePlan *out){
    if(!ui)return -1;
    return scope_acquisition_plan_for((unsigned)ui->timebase_index,ui->conversion_hz,out);
}
bool scope_ui_set_conversion_hz(ScopeUI *ui,unsigned conversion_hz){
    if(!ui||ui->replay_active||scope_ui_autoset_busy(ui))return false;
    if(!ui||(conversion_hz!=5000000&&conversion_hz!=10000000&&conversion_hz!=25000000&&conversion_hz!=50000000))return false;
    if(ui->conversion_hz!=conversion_hz){
        ui->timebase_index+=(int)timebase_prefix(conversion_hz)-(int)timebase_prefix(ui->conversion_hz);
        if(ui->timebase_index<0)ui->timebase_index=0;
        unsigned count=timebase_prefix(conversion_hz)+SCOPE_TIMEBASE_COUNT;
        if((unsigned)ui->timebase_index>=count)ui->timebase_index=(int)count-1;
        ui->conversion_hz=conversion_hz;ui->capture_epoch++;
        ui->single_armed=false;ui->single_complete=false;ui->adc_triggered=false;
        ui->adc_display_start=0;ui->adc_trigger_crossing=0;
        ui->adc_waiting=ui->model.running&&ui->trigger_normal&&ui->adc_channel<2;
    }
    redraw(ui);return true;
}
int scope_ui_hardware_config(const ScopeUI *ui,ScopeHardwareConfig *out){
    ScopeCapturePlan p;
    if(!ui||!out||!ui->adaptive_sampling||!ui->hardware_trigger||ui->trigger_channel>1||
        ui->trigger_level_mv < -5000||ui->trigger_level_mv>5000||
        scope_ui_capture_plan(ui,&p))return -1;
    double span=10*current_timebase(ui)*p.rate_hz;
    unsigned pre=(unsigned)ceil(.2*span)+1;
    if(pre<1||pre>p.count-2)return -1;
    out->control=1u|(ui->trigger_channel<<1)|(ui->trigger_rising?4u:0u)|((ui->trigger_normal||ui->single_armed)?8u:0u);
    uint32_t level_q8;
    if(scope_calibration_trigger_q8(&ui->instrument_calibration,ui->trigger_channel,ui->trigger_level_mv,&level_q8))return -1;
    out->level_q8=(unsigned)level_q8;
    out->pre_samples=pre;out->hysteresis_codes=10;return 0;
}
void scope_ui_enable_hardware_trigger(ScopeUI *ui,bool enabled){
    if(!ui||ui->replay_active||scope_ui_autoset_busy(ui))return;
    if(!ui)return;
    if(ui->hardware_trigger!=enabled){
        ui->hardware_trigger=enabled;ui->capture_epoch++;
        ui->single_armed=false;ui->single_complete=false;ui->adc_triggered=false;
        ui->adc_waiting=ui->model.running&&ui->trigger_normal&&ui->adc_channel<2;
    }
    redraw(ui);
}
static bool display_adc_record(ScopeUI *ui,const uint32_t *record,unsigned count,unsigned rate,unsigned id,
                                 const ScopeAdcMetrics metrics[2],const ScopeRecordInfo *info,bool matched,const ScopeTriggerResult *result){
    ScopeMeasurement measured[2];
    if(scope_measurement_analyze_calibrated(record,count,rate,&ui->instrument_calibration,measured))return false;
    if(!ui->adc_record){
        ui->sim_scale_index=ui->scale_index;
        if(ui->adc_channel<2)ui->scale_index=ui->channel_scale_index[ui->adc_channel];
    }
    ui->adc_record=record;ui->adc_count=count;ui->adc_rate=rate;ui->adc_id=id;
    ui->record_calibration=ui->instrument_calibration;
    if(info)ui->adc_info=*info;else memset(&ui->adc_info,0,sizeof(ui->adc_info));
    ui->adc_triggered=matched;ui->adc_waiting=false;
    ui->adc_display_start=matched?result->display_start_sample:0;ui->adc_trigger_crossing=matched?result->crossing_sample:0;
    if(ui->single_armed){ui->single_armed=false;ui->single_complete=true;ui->model.running=false;}
    memcpy(ui->adc_metrics,metrics,sizeof(ui->adc_metrics));
    if(ui->record_calibration.flags)for(unsigned ch=0;ch<2;ch++){
        ui->adc_metrics[ch].vpp_v=measured[ch].vpp_v;
        ui->adc_metrics[ch].rms_v=measured[ch].rms_total_v;
    }
    memcpy(ui->measurement,measured,sizeof(ui->measurement));redraw(ui);return true;
}
bool scope_ui_set_adc_record_info(ScopeUI *ui,const uint32_t *record,unsigned count,unsigned rate,unsigned id,const ScopeAdcMetrics metrics[2],const ScopeRecordInfo *info){
    if(!valid_record_profile(ui,record,count,rate,metrics,info)||ui->hardware_trigger)return false;
    if(id<=ui->adc_last_seen_id)return false;
    ui->adc_last_seen_id=id;
    if(!ui->model.running)return false;
    if(ui->single_armed&&id<=ui->single_armed_after_id)return false;
    ScopeTriggerResult result={0};int found=0;
    if(ui->adc_channel<2){
        found=align_record(ui,record,count,rate,&result);
        if(found<0)return false;
        if(!found&&(ui->trigger_normal||ui->single_armed)){
            ui->adc_waiting=true;redraw(ui);return false;
        }
    }
    return display_adc_record(ui,record,count,rate,id,metrics,info,found==1,&result);
}
bool scope_ui_set_hardware_adc_record(ScopeUI *ui,const uint32_t *record,unsigned count,unsigned rate,unsigned id,
                                      const ScopeAdcMetrics metrics[2],const ScopeRecordInfo *info,const ScopeHardwareTrigger *trigger){
    ScopeHardwareConfig hardware;
    if(!valid_record_profile(ui,record,count,rate,metrics,info)||!trigger||scope_ui_hardware_config(ui,&hardware))return false;
    if(trigger->matched==trigger->auto_forced)return false;
    ScopeTriggerResult result={0};
    if(trigger->matched){
        if(trigger->after_index!=hardware.pre_samples)return false;
        ScopeTriggerConfig config={ui->trigger_channel,ui->trigger_rising,hardware.level_q8/256.0,
            hardware.hysteresis_codes,10*current_timebase(ui)*rate,.2};
        if(scope_trigger_interpolate_at(record,count,trigger->after_index,&config,&result)!=1)return false;
    }else{
        if(trigger->after_index!=0||ui->trigger_normal||ui->single_armed)return false;
        for(unsigned i=0;i<count;i++)if(record[i]&UINT32_C(0xf800f800))return false;
    }
    /* Malformed, stale and frozen records must not advance the ID gate. */
    if(!ui->model.running||id<=ui->adc_last_seen_id||(ui->single_armed&&id<=ui->single_armed_after_id))return false;
    ui->adc_last_seen_id=id;
    if(scope_ui_autoset_busy(ui)){
        ScopeMeasurement measured[2];unsigned ch=ui->autoset_plan.trigger_channel;
        if(scope_measurement_analyze_calibrated(record,count,rate,&ui->instrument_calibration,measured)){
            autoset_restore(ui,SCOPE_AUTO_UNSTABLE,false);return false;
        }
        for(unsigned c=0;c<2;c++)if(ui->autoset_plan.channel_mask&(1u<<c)){
            double scale=scales[ui->channel_scale_index[c]],position=ui->channel_position_mdiv[c]/1000.0;
            if(measured[c].overrange){autoset_restore(ui,SCOPE_AUTO_OVERRANGE,false);return false;}
            if(!measured[c].valid||measured[c].vmin_v/scale+position < -3.5||
                measured[c].vmax_v/scale+position > 3.5||fabs(measured[c].vmean_v/scale+position)>.5){
                autoset_restore(ui,SCOPE_AUTO_UNSTABLE,false);return false;
            }
        }
        double f=metrics[ch].frequency_hz;
        if(!trigger->matched||!measured[ch].valid||measured[ch].vpp_v<code_span_volts(&ui->instrument_calibration,ch,32)-1e-12||
            !metrics[ch].frequency_valid||!isfinite(f)||f<2.0*rate/(count-1)||f>rate/4.0||
            fabs(f-ui->autoset_plan.frequency_hz)>=.05*ui->autoset_plan.frequency_hz){
            autoset_restore(ui,SCOPE_AUTO_UNSTABLE,false);return false;
        }
        ui->autoset_state=SCOPE_AUTO_COMPLETE;ui->autoset_notice_s=5;ui->adc_waiting=false;
    }
    return display_adc_record(ui,record,count,rate,id,metrics,info,trigger->matched,&result);
}
bool scope_ui_arm_single(ScopeUI *ui){
    if(!ui||ui->replay_active||scope_ui_autoset_busy(ui))return false;
    if(!single_capable(ui))return false;
    ui->single_armed=true;ui->single_complete=false;ui->model.running=true;ui->adc_waiting=true;
    ui->single_armed_after_id=ui->adc_last_seen_id;ui->capture_epoch++;redraw(ui);return true;
}
void scope_ui_set_running(ScopeUI *ui,bool running){
    if(!ui)return;
    if(ui->replay_active){if(running)(void)scope_ui_memory_return_live(ui);return;}
    if(scope_ui_autoset_busy(ui)){
        if(running)return;
        autoset_restore(ui,SCOPE_AUTO_CANCELED,true);return;
    }
    ui->single_armed=false;ui->single_complete=false;ui->model.running=running;
    ui->adc_waiting=running&&ui->trigger_normal&&ui->adc_channel<2;ui->capture_epoch++;redraw(ui);
}
bool scope_ui_configure_trigger(ScopeUI *ui,bool normal,bool rising,int level_mv){
    if(!ui||ui->replay_active||scope_ui_autoset_busy(ui))return false;
    if(level_mv < -5000||level_mv>5000)return false;
    if(ui->trigger_normal!=normal||ui->trigger_rising!=rising||ui->trigger_level_mv!=level_mv){
        ui->trigger_normal=normal;ui->trigger_rising=rising;ui->trigger_level_mv=level_mv;
        ui->capture_epoch++;ui->adc_triggered=false;ui->single_complete=false;
        if(ui->single_armed)ui->single_armed_after_id=ui->adc_last_seen_id;
        ui->adc_waiting=ui->model.running&&(normal||ui->single_armed);
    }
    redraw(ui);return true;
}
bool scope_ui_select_source(ScopeUI *ui,unsigned source){
    if(!ui||scope_ui_autoset_busy(ui))return false;
    if(ui->replay_active&&source>=2)return false;
    if(source>2)return false;
    if(source!=ui->adc_channel){
        bool measurement_only=ui->adc_record&&ui->adc_channel<2&&source<2;
        if(ui->adc_record&&ui->adc_channel<2)ui->channel_scale_index[ui->adc_channel]=ui->scale_index;
        else ui->sim_scale_index=ui->scale_index;
        ui->scale_index=source<2?ui->channel_scale_index[source]:ui->sim_scale_index;
        ui->adc_channel=source;ui->model.wave=SCOPE_SINE;
        if(!measurement_only){
            ui->single_armed=false;ui->single_complete=false;
            ui->adc_triggered=false;ui->adc_waiting=ui->model.running&&ui->trigger_normal&&source<2;ui->capture_epoch++;
        }
    }
    redraw(ui);return true;
}
bool scope_ui_select_trigger_source(ScopeUI *ui,unsigned channel){
    if(!ui||ui->replay_active||scope_ui_autoset_busy(ui))return false;
    if(channel>1)return false;
    if(channel!=ui->trigger_channel){
        ui->trigger_channel=channel;ui->capture_epoch++;
        ui->adc_triggered=false;ui->single_complete=false;
        ui->adc_waiting=ui->adc_channel<2&&ui->model.running&&(ui->trigger_normal||ui->single_armed);
        if(ui->single_armed)ui->single_armed_after_id=ui->adc_last_seen_id;
    }
    redraw(ui);return true;
}
static void trigger_source_event(lv_event_t *e){
    ScopeUI *ui=lv_event_get_user_data(e);
    if(ui->adc_record&&ui->adc_channel<2)(void)scope_ui_select_trigger_source(ui,1-ui->trigger_channel);
}
void scope_ui_set_dual_display(ScopeUI *ui,bool enabled){
    if(!ui||scope_ui_autoset_busy(ui))return;
    ui->dual_display=enabled;redraw(ui);
}
bool scope_ui_configure_channel(ScopeUI *ui,unsigned channel,unsigned scale_index,int position_mdiv){
    if(!ui||scope_ui_autoset_busy(ui))return false;
    if(channel>1||scale_index>6||position_mdiv < -3500||position_mdiv>3500)return false;
    ui->channel_scale_index[channel]=(int)scale_index;ui->channel_position_mdiv[channel]=position_mdiv;
    if(ui->adc_record&&ui->adc_channel==channel)ui->scale_index=(int)scale_index;
    redraw(ui);return true;
}
static void channel_panel_event(lv_event_t *e){
    ScopeUI *ui=lv_event_get_user_data(e);if(scope_ui_autoset_busy(ui))return;
    if(lv_obj_has_flag(ui->channel_panel,LV_OBJ_FLAG_HIDDEN)){
        if(!ui->adc_record||ui->adc_channel>=2||ui->bars_on)return;
        ui->channel_config_target=ui->adc_channel;lv_obj_remove_flag(ui->channel_panel,LV_OBJ_FLAG_HIDDEN);
    }else lv_obj_add_flag(ui->channel_panel,LV_OBJ_FLAG_HIDDEN);
    redraw(ui);
}
static void channel_choice_event(lv_event_t *e){
    ScopeUI *ui=lv_event_get_user_data(e);if(scope_ui_autoset_busy(ui))return;ui->channel_config_target=(unsigned)(uintptr_t)lv_obj_get_user_data(lv_event_get_target(e));redraw(ui);
}
static void channel_adjust_event(lv_event_t *e){
    ScopeUI *ui=lv_event_get_user_data(e);if(scope_ui_autoset_busy(ui))return;unsigned ch=ui->channel_config_target;
    int id=(int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    int scale=ui->channel_scale_index[ch],position=ui->channel_position_mdiv[ch];
    if(id<2){scale+=id?1:-1;if(scale<0)scale=0;if(scale>6)scale=6;}
    else {position+=id==3?250:-250;if(position < -3500)position=-3500;if(position>3500)position=3500;}
    (void)scope_ui_configure_channel(ui,ch,(unsigned)scale,position);
}
static void channel_zero_event(lv_event_t *e){
    ScopeUI *ui=lv_event_get_user_data(e);if(scope_ui_autoset_busy(ui))return;unsigned ch=ui->channel_config_target;
    (void)scope_ui_configure_channel(ui,ch,(unsigned)ui->channel_scale_index[ch],0);
}
void scope_ui_set_cursors(ScopeUI *ui,bool enabled){
    if(!ui||scope_ui_autoset_busy(ui))return;
    ui->cursors_on=enabled;ui->cursor_dragging=-1;redraw(ui);}
bool scope_ui_move_cursors(ScopeUI *ui,unsigned a,unsigned b){
    if(!ui||scope_ui_autoset_busy(ui))return false;
    if(a>1000||b>1000)return false;
    ui->cursor_positions[0]=a;ui->cursor_positions[1]=b;redraw(ui);return true;
}
static void cursor_event(lv_event_t *e){ScopeUI *ui=lv_event_get_user_data(e);scope_ui_set_cursors(ui,!ui->cursors_on);}
static void cursor_pointer(lv_event_t *e){
    ScopeUI *ui=lv_event_get_user_data(e);if(scope_ui_autoset_busy(ui))return;lv_event_code_t code=lv_event_get_code(e);
    if(code==LV_EVENT_RELEASED||code==LV_EVENT_PRESS_LOST){ui->cursor_dragging=-1;return;}
    if((code!=LV_EVENT_PRESSED&&code!=LV_EVENT_PRESSING)||!ui->cursors_on||!ui->adc_record||ui->adc_channel>=2||ui->bars_on)return;
    lv_indev_t *input=lv_event_get_indev(e);if(!input)return;
    lv_point_t p;lv_indev_get_point(input,&p);lv_area_t a;lv_obj_get_coords(ui->chart,&a);
    int width=lv_obj_get_width(ui->chart)-3;int x=p.x-a.x1-1;if(x<0)x=0;if(x>width)x=width;
    unsigned position=(unsigned)((x*1000+width/2)/width);
    if(code==LV_EVENT_PRESSED)ui->cursor_dragging=abs((int)position-(int)ui->cursor_positions[0])<=abs((int)position-(int)ui->cursor_positions[1])?0:1;
    if(ui->cursor_dragging>=0){ui->cursor_positions[ui->cursor_dragging]=position;redraw(ui);}
}
static void dual_event(lv_event_t *e){ScopeUI *ui=lv_event_get_user_data(e);scope_ui_set_dual_display(ui,!ui->dual_display);}
static void single_event(lv_event_t *e){(void)scope_ui_arm_single(lv_event_get_user_data(e));}
static void trigger_event(lv_event_t *e){ScopeUI *ui=lv_event_get_user_data(e);(void)scope_ui_configure_trigger(ui,ui->trigger_normal,!ui->trigger_rising,ui->trigger_level_mv);}
static void trigger_mode_event(lv_event_t *e){ScopeUI *ui=lv_event_get_user_data(e);(void)scope_ui_configure_trigger(ui,!ui->trigger_normal,ui->trigger_rising,ui->trigger_level_mv);}
static void language_event(lv_event_t *e) {
    ScopeUI *ui=lv_event_get_user_data(e);if(scope_ui_autoset_busy(ui))return;
    ui->language=ui->language==SCOPE_ZH_CN?SCOPE_EN:SCOPE_ZH_CN;
    localize(ui);redraw(ui);
}
void scope_ui_set_board_mode(ScopeUI *ui,bool enabled) {
    if(!ui||scope_ui_autoset_busy(ui))return;
    ui->board_mode=enabled;localize(ui);
}
static void run_event(lv_event_t *e) {
    ScopeUI *ui=lv_event_get_user_data(e);scope_ui_set_running(ui,!ui->model.running);
}
static void wave_event(lv_event_t *e) {
    ScopeUI *ui=lv_event_get_user_data(e);if(scope_ui_autoset_busy(ui))return;
    for(int i=0;i<3;i++) if(lv_event_get_target(e)==ui->wave_buttons[i]) {
        if(ui->adc_record)(void)scope_ui_select_source(ui,(unsigned)i);
        else ui->model.wave=(ScopeWave)i;
    }
    redraw(ui);
}
static void adjust_event(lv_event_t *e) {
    ScopeUI *ui=lv_event_get_user_data(e);if(scope_ui_autoset_busy(ui))return;
    lv_obj_t *target=lv_event_get_target(e);
    int id=(int)(intptr_t)lv_obj_get_user_data(target);
    if(ui->replay_active&&id<2){
        int before=ui->timebase_index,next=before+(id?1:-1);
        if(next<0||next>=(int)timebase_count(ui))return;
        ui->timebase_index=next;double span=10*current_timebase(ui)*ui->adc_rate;
        if(span<1||span>ui->adc_count-1){ui->timebase_index=before;return;}
        double start=(ui->replay_meta.flags&SCOPE_RECORD_TRIGGERED)?ui->replay_meta.trigger_crossing_q16/65536.0-.2*span:ui->replay_meta.view_start_q16/65536.0;
        ui->adc_display_start=fmin(fmax(start,0),ui->adc_count-1-span);redraw(ui);return;
    }
    if(ui->adc_record&&ui->adc_channel<2&&id/2==2){
        int level=ui->trigger_level_mv+((id%2)?100:-100);if(level<-5000)level=-5000;if(level>5000)level=5000;
        (void)scope_ui_configure_trigger(ui,ui->trigger_normal,ui->trigger_rising,level);return;
    }
    int *v=id/2==0?&ui->timebase_index:id/2==1?&ui->scale_index:&ui->frequency_index;
    int max_index=id/2==0?(int)timebase_count(ui)-1:6;
    int before=*v;*v+=(id%2)?1:-1; if(*v<0)*v=0; if(*v>max_index)*v=max_index;
    if(id/2==0&&before!=*v&&(ui->adaptive_sampling||(ui->adc_record&&ui->adc_channel<2))){
        ui->capture_epoch++;ui->adc_triggered=false;
        ScopeTriggerResult result={0};
        /* A displayed record keeps its own sample rate and profile until a new
           capture arrives. Missing portions of a longer view stay empty. */
        bool found=!ui->adaptive_sampling&&align_record(ui,ui->adc_record,ui->adc_count,ui->adc_rate,&result)==1;
        ui->adc_triggered=found;ui->adc_display_start=found?result.display_start_sample:0;
        ui->adc_trigger_crossing=found?result.crossing_sample:0;
        if(ui->adaptive_sampling)ui->single_complete=false;
        if(!found&&ui->model.running&&ui->trigger_normal)ui->adc_waiting=true;
        if(ui->single_armed){
            ui->single_armed_after_id=ui->adc_last_seen_id;
            if(!single_capable(ui)){scope_ui_set_running(ui,false);}
        }
    }
    ui->model.frequency_hz=frequencies[ui->frequency_index]; redraw(ui);
}
static void settings_event(lv_event_t *e) {
    ScopeUI *ui=lv_event_get_user_data(e);if(scope_ui_autoset_busy(ui))return;
    if(lv_obj_has_flag(ui->settings,LV_OBJ_FLAG_HIDDEN)) lv_obj_remove_flag(ui->settings,LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(ui->settings,LV_OBJ_FLAG_HIDDEN);
}
static void grid_event(lv_event_t *e) {
    ScopeUI *ui=lv_event_get_user_data(e);if(scope_ui_autoset_busy(ui))return; ui->grid_on=!ui->grid_on; redraw(ui);
}
static void bars_event(lv_event_t *e) {
    ScopeUI *ui=lv_event_get_user_data(e);if(scope_ui_autoset_busy(ui))return; ui->bars_on=!ui->bars_on; redraw(ui);
}

void scope_ui_create(ScopeUI *ui) {
    memset(ui,0,sizeof(*ui)); scope_model_init(&ui->model);
    ui->instrument_calibration=*scope_calibration_default_profile();
    ui->record_calibration=ui->instrument_calibration;
    ui->conversion_hz=5000000;
    ui->spectrum_window=SCOPE_SPECTRUM_HANN;ui->spectrum_remove_mean=true;ui->spectrum_auto_range=true;
    ui->language=SCOPE_ZH_CN;
    ui->trigger_rising=true;ui->trigger_level_mv=500;
    ui->cursor_positions[0]=250;ui->cursor_positions[1]=750;ui->cursor_dragging=-1;
    ui->timebase_index=2; ui->scale_index=2; ui->frequency_index=2; ui->grid_on=true;
    ui->channel_scale_index[0]=ui->channel_scale_index[1]=ui->sim_scale_index=2;
    lv_obj_t *root=lv_screen_active(); lv_obj_remove_flag(root,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(root,color(BG),0);
    lv_obj_set_style_text_font(root,&scope_font_16,0);
    box(root,0,0,1024,64,0x111a28);
    ui->title_label=label(root,24,17,176,"",TEXT,&scope_font_24);
    ui->subtitle_label=label(root,304,25,140,"",MUTED,&scope_font_14);
    ui->memory_button=button(root,208,12,88,40,"",memory_event,ui);
    ui->autoset_button=button(root,452,12,108,40,"",autoset_event,ui);
    ui->measurement_button=button(root,568,12,104,40,"",measurement_event,ui);
    ui->settings_button=button(root,900,12,100,40,"",settings_event,ui);
    ui->cursor_button=button(root,680,12,116,40,"",cursor_event,ui);
    ui->single_button=button(root,804,12,88,40,"",single_event,ui);
    ui->trigger_button=button(root,300,76,148,36,"",trigger_event,ui);
    ui->trigger_source_button=button(root,452,76,148,36,"",trigger_source_event,ui);
    ui->channel_label=label(root,24,82,270,"",CYAN,&scope_font_16);
    ui->state_label=label(root,612,84,156,"",MUTED,&scope_font_14);
    ui->axes_label=label(root,24,113,744,"",MUTED,&scope_font_14);
    ui->channel_legend[0]=label(root,562,113,100,"CH1",channel_colors[0],&scope_font_14);
    ui->channel_legend[1]=label(root,668,113,100,"CH2",channel_colors[1],&scope_font_14);
    for(unsigned ch=0;ch<2;ch++){
        ui->zero_leader[ch]=lv_line_create(root);lv_obj_remove_style_all(ui->zero_leader[ch]);
        lv_obj_set_pos(ui->zero_leader[ch],0,0);
        lv_obj_set_style_line_color(ui->zero_leader[ch],color(channel_colors[ch]),0);
        lv_obj_set_style_line_width(ui->zero_leader[ch],1,0);
        lv_obj_remove_flag(ui->zero_leader[ch],LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
        ui->zero_marker[ch]=label(root,2,291,16,ch?"2":"1",BG,&scope_font_14);
        lv_obj_set_height(ui->zero_marker[ch],18);
        lv_obj_set_style_text_align(ui->zero_marker[ch],LV_TEXT_ALIGN_CENTER,0);
        lv_obj_set_style_bg_color(ui->zero_marker[ch],color(channel_colors[ch]),0);
        lv_obj_set_style_bg_opa(ui->zero_marker[ch],LV_OPA_COVER,0);
        lv_obj_set_style_radius(ui->zero_marker[ch],3,0);
        lv_obj_remove_flag(ui->zero_marker[ch],LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    }
    ui->chart=lv_chart_create(root); lv_obj_remove_style_all(ui->chart);
    lv_obj_set_pos(ui->chart,24,136); lv_obj_set_size(ui->chart,744,328);
    lv_obj_remove_flag(ui->chart,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui->chart,color(0x101925),0); lv_obj_set_style_bg_opa(ui->chart,255,0);
    lv_obj_set_style_border_width(ui->chart,1,0); lv_obj_set_style_border_color(ui->chart,color(BORDER),0);
    lv_obj_set_style_line_color(ui->chart,color(0x29384b),LV_PART_MAIN);
    lv_obj_set_style_line_width(ui->chart,1,LV_PART_MAIN);
    lv_obj_set_style_line_width(ui->chart,2,LV_PART_ITEMS);
    lv_obj_set_style_size(ui->chart,0,0,LV_PART_INDICATOR);
    lv_chart_set_type(ui->chart,LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(ui->chart,SCOPE_POINTS);
    lv_chart_set_range(ui->chart,LV_CHART_AXIS_PRIMARY_Y,-4000,4000);
    ui->series=lv_chart_add_series(ui->chart,color(0x40e0c0),LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_ext_y_array(ui->chart,ui->series,ui->samples);
    ui->secondary_series=lv_chart_add_series(ui->chart,color(channel_colors[1]),LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_ext_y_array(ui->chart,ui->secondary_series,ui->secondary_samples);
    lv_obj_add_flag(ui->chart,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(ui->chart,cursor_pointer,LV_EVENT_ALL,ui);
    const uint32_t cursor_colors[]={0x78a9ff,0xff86b7};
    for(unsigned p=0;p<2;p++){
        ui->cursor_line[p]=box(ui->chart,0,1,1,326,cursor_colors[p]);lv_obj_set_style_radius(ui->cursor_line[p],0,0);
        lv_obj_remove_flag(ui->cursor_line[p],LV_OBJ_FLAG_CLICKABLE);
        ui->cursor_tag[p]=label(ui->chart,0,2,16,p?"B":"A",cursor_colors[p],&scope_font_14);
        lv_obj_remove_flag(ui->cursor_tag[p],LV_OBJ_FLAG_CLICKABLE);
    }
    ui->bars=box(root,24,136,744,328,0); lv_obj_set_style_radius(ui->bars,0,0);
    const uint32_t barcolors[]={0xffffff,0xffff00,0x00ffff,0x00ff00,0xff00ff,0xff0000,0x0000ff,0};
    for(int i=0;i<8;i++){lv_obj_t *b=box(ui->bars,i*93,0,93,328,barcolors[i]);lv_obj_set_style_radius(b,0,0);}
    lv_obj_t **values[]={&ui->vpp_label,&ui->rms_label,&ui->readout_label};
    for(int i=0;i<3;i++) {
        lv_obj_t *p=box(root,24+i*252,484,240,76,PANEL);
        ui->metric_titles[i]=label(p,14,10,214,"",MUTED,&scope_font_14);
        *values[i]=label(p,14,32,214,"",i==0?CYAN:TEXT,&scope_font_24);
    }
    box(root,792,76,220,484,PANEL);
    ui->run_button=button(root,804,78,196,44,"",run_event,ui);
    ui->run_label=lv_obj_get_child(ui->run_button,0);
    ui->wave_title=label(root,804,142,196,"",MUTED,&scope_font_14);
    for(int i=0;i<3;i++) ui->wave_buttons[i]=button(root,804+i*68,172,60,40,"",wave_event,ui);
    ui->time_label=label(root,804,228,200,"",TEXT,&scope_font_14);
    ui->scale_label=label(root,804,312,200,"",TEXT,&scope_font_14);
    ui->frequency_label=label(root,804,398,200,"",TEXT,&scope_font_14);
    const int rows[]={254,338,424};
    for(int row=0;row<3;row++) for(int side=0;side<2;side++) {
        lv_obj_t *b=button(root,804+side*106,rows[row],90,40,side?"+":"-",adjust_event,ui);
        lv_obj_set_user_data(b,(void *)(intptr_t)(row*2+side));
        ui->adjust_buttons[row*2+side]=b;
    }
    ui->source_note=label(root,804,490,196,"",MUTED,&scope_font_14);
    ui->channel_panel_button=button(root,804,480,196,36,"",channel_panel_event,ui);
    ui->footer_label=label(root,24,578,970,"",MUTED,&scope_font_14);
    ui->settings=box(root,0,0,1024,600,0x000000);
    lv_obj_set_style_bg_opa(ui->settings,170,0);
    lv_obj_add_flag(ui->settings,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *dialog=box(ui->settings,288,110,448,364,0x1a2638);
    ui->settings_dialog=dialog;
    ui->settings_title=label(dialog,24,20,400,"",TEXT,&scope_font_24);
    ui->language_button=button(dialog,32,64,384,44,"",language_event,ui);
    ui->grid_button=button(dialog,32,122,384,44,"",grid_event,ui);
    ui->bars_button=button(dialog,32,180,384,44,"",bars_event,ui);
    ui->parameter_button=button(dialog,32,180,384,44,"",parameter_event,ui);
    ui->trigger_mode_button=button(dialog,32,238,384,44,"",trigger_mode_event,ui);
    ui->dual_button=button(dialog,32,296,384,44,"",dual_event,ui);
    ui->settings_note=label(dialog,32,238,384,"",MUTED,&scope_font_14);
    ui->close_button=button(dialog,322,290,98,44,"",settings_event,ui);
    lv_obj_add_flag(ui->settings,LV_OBJ_FLAG_HIDDEN);
    ui->channel_panel=box(root,0,0,1024,600,0);
    lv_obj_set_style_bg_opa(ui->channel_panel,170,0);lv_obj_add_flag(ui->channel_panel,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *panel=box(ui->channel_panel,288,100,448,390,0x1a2638);
    ui->channel_panel_title=label(panel,24,20,400,"",TEXT,&scope_font_24);
    for(unsigned ch=0;ch<2;ch++){
        ui->channel_choices[ch]=button(panel,32+ch*204,72,180,40,ch?"CH2":"CH1",channel_choice_event,ui);
        lv_obj_set_user_data(ui->channel_choices[ch],(void*)(uintptr_t)ch);
    }
    ui->channel_scale_label=label(panel,32,134,384,"",TEXT,&scope_font_16);
    ui->channel_position_label=label(panel,32,224,384,"",TEXT,&scope_font_16);
    for(unsigned i=0;i<4;i++){
        ui->channel_adjust[i]=button(panel,32+(i%2)*204,i<2?163:253,180,40,i%2?"+":"-",channel_adjust_event,ui);
        lv_obj_set_user_data(ui->channel_adjust[i],(void*)(uintptr_t)i);
    }
    ui->channel_zero_button=button(panel,32,315,180,44,"",channel_zero_event,ui);
    ui->channel_close_button=button(panel,318,315,98,44,"",channel_panel_event,ui);
    lv_obj_add_flag(ui->channel_panel,LV_OBJ_FLAG_HIDDEN);
    ui->measurement_panel=box(root,0,0,1024,600,0);
    lv_obj_set_style_bg_opa(ui->measurement_panel,170,0);lv_obj_add_flag(ui->measurement_panel,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *measure=box(ui->measurement_panel,132,70,760,460,0x1a2638);
    ui->measurement_title=label(measure,32,22,550,"",TEXT,&scope_font_24);
    ui->measurement_close_button=button(measure,630,20,98,44,"",measurement_event,ui);
    ui->spectrum_button=button(measure,400,72,200,40,"",spectrum_event,ui);
    ui->measurement_timing_button=button(measure,620,72,108,40,"",measurement_timing_event,ui);
    for(unsigned ch=0;ch<2;ch++){
        ui->measurement_choices[ch]=button(measure,32+ch*164,72,148,40,ch?"CH2":"CH1",measurement_choice_event,ui);
        lv_obj_set_user_data(ui->measurement_choices[ch],(void*)(uintptr_t)ch);
    }
    for(unsigned i=0;i<8;i++){
        int x=32+(int)(i/4)*356,y=136+(int)(i%4)*54;
        ui->measurement_names[i]=label(measure,x,y,166,"",MUTED,&scope_font_14);
        ui->measurement_values[i]=label(measure,x+170,y-5,170,"",TEXT,&scope_font_24);
    }
    for(unsigned i=0;i<6;i++){
        int x=32+(int)(i/3)*356,y=136+(int)(i%3)*54;
        ui->timing_names[i]=label(measure,x,y,166,"",MUTED,&scope_font_14);
        ui->timing_values[i]=label(measure,x+170,y-5,170,"",TEXT,&scope_font_24);
    }
    ui->measurement_note=label(measure,32,356,696,"",MUTED,&scope_font_14);
    lv_obj_add_flag(ui->measurement_panel,LV_OBJ_FLAG_HIDDEN);
    ui->autoset_panel=box(root,0,0,1024,600,0);
    lv_obj_set_style_bg_opa(ui->autoset_panel,170,0);lv_obj_add_flag(ui->autoset_panel,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *autoset=box(ui->autoset_panel,262,210,500,180,0x1a2638);
    ui->autoset_label=label(autoset,24,30,452,"",TEXT,&scope_font_16);
    ui->autoset_cancel_button=button(autoset,164,106,172,44,"",autoset_cancel_event,ui);
    lv_obj_add_flag(ui->autoset_panel,LV_OBJ_FLAG_HIDDEN);
    ui->memory_panel=box(root,0,0,1024,600,0);
    lv_obj_set_style_bg_opa(ui->memory_panel,170,0);lv_obj_add_flag(ui->memory_panel,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *memory=box(ui->memory_panel,212,88,600,424,0x1a2638);
    ui->memory_title=label(memory,28,20,420,"",TEXT,&scope_font_24);
    ui->memory_note=label(memory,28,66,544,"",MUTED,&scope_font_14);
    ui->memory_save_button=button(memory,28,134,544,44,"",memory_action_event,ui);
    ui->memory_load_button=button(memory,28,188,544,44,"",memory_action_event,ui);
    ui->memory_settings_button=button(memory,28,242,544,44,"",memory_action_event,ui);
    ui->tf_record_button=button(memory,380,242,192,44,"TF",storage_entry_event,ui);lv_obj_add_flag(ui->tf_record_button,LV_OBJ_FLAG_HIDDEN);
    ui->memory_live_button=button(memory,28,296,544,44,"",memory_action_event,ui);
    ui->memory_close_button=button(memory,444,358,128,44,"",memory_event,ui);
    lv_obj_add_flag(ui->memory_panel,LV_OBJ_FLAG_HIDDEN);
    ui->parameter_panel=box(root,0,0,1024,600,0);
    lv_obj_set_style_bg_opa(ui->parameter_panel,170,0);lv_obj_add_flag(ui->parameter_panel,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *parameters=box(ui->parameter_panel,212,100,600,400,0x1a2638);
    ui->parameter_title=label(parameters,28,20,544,"",TEXT,&scope_font_24);
    ui->parameter_note=label(parameters,28,66,544,"",MUTED,&scope_font_14);
    ui->parameter_save_button=button(parameters,28,130,544,44,"",parameter_action_event,ui);
    ui->parameter_load_button=button(parameters,28,184,544,44,"",parameter_action_event,ui);
    ui->parameter_default_button=button(parameters,28,238,544,44,"",parameter_action_event,ui);
    ui->tf_parameter_button=button(parameters,380,238,192,44,"TF",storage_entry_event,ui);lv_obj_add_flag(ui->tf_parameter_button,LV_OBJ_FLAG_HIDDEN);
    ui->parameter_close_button=button(parameters,444,330,128,44,"",parameter_event,ui);
    lv_obj_add_flag(ui->parameter_panel,LV_OBJ_FLAG_HIDDEN);
    ui->spectrum_panel=box(root,0,0,1024,600,0);
    lv_obj_set_style_bg_opa(ui->spectrum_panel,170,0);lv_obj_add_flag(ui->spectrum_panel,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *spectrum=box(ui->spectrum_panel,72,60,880,490,0x1a2638);
    ui->spectrum_title=label(spectrum,24,16,680,"",TEXT,&scope_font_24);
    ui->spectrum_close_button=button(spectrum,748,14,108,38,"",spectrum_event,ui);
    ui->spectrum_choices[0]=button(spectrum,24,58,80,36,"CH1",spectrum_action_event,ui);
    ui->spectrum_choices[1]=button(spectrum,112,58,80,36,"CH2",spectrum_action_event,ui);
    ui->spectrum_window_button=button(spectrum,208,58,176,36,"",spectrum_action_event,ui);
    ui->spectrum_mean_button=button(spectrum,396,58,192,36,"",spectrum_action_event,ui);
    ui->spectrum_range_button=button(spectrum,600,58,256,36,"",spectrum_action_event,ui);
    ui->spectrum_note=label(spectrum,24,102,832,"",MUTED,&scope_font_14);
    label(spectrum,12,160,48,"+20",MUTED,&scope_font_14);
    label(spectrum,12,180,48,"dBV",MUTED,&scope_font_14);
    label(spectrum,12,339,48,"-100",MUTED,&scope_font_14);
    ui->spectrum_chart=lv_chart_create(spectrum);lv_obj_remove_style_all(ui->spectrum_chart);
    lv_obj_set_pos(ui->spectrum_chart,64,160);lv_obj_set_size(ui->spectrum_chart,792,196);
    lv_obj_remove_flag(ui->spectrum_chart,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(ui->spectrum_chart,color(0x101925),0);lv_obj_set_style_bg_opa(ui->spectrum_chart,255,0);
    lv_obj_set_style_border_width(ui->spectrum_chart,1,0);lv_obj_set_style_border_color(ui->spectrum_chart,color(BORDER),0);
    lv_obj_set_style_line_color(ui->spectrum_chart,color(0x29384b),LV_PART_MAIN);lv_obj_set_style_line_width(ui->spectrum_chart,1,LV_PART_MAIN);
    lv_obj_set_style_line_width(ui->spectrum_chart,2,LV_PART_ITEMS);lv_obj_set_style_size(ui->spectrum_chart,0,0,LV_PART_INDICATOR);
    lv_chart_set_type(ui->spectrum_chart,LV_CHART_TYPE_LINE);lv_chart_set_point_count(ui->spectrum_chart,SCOPE_POINTS);
    lv_chart_set_range(ui->spectrum_chart,LV_CHART_AXIS_PRIMARY_Y,-100000,20000);
    ui->spectrum_series=lv_chart_add_series(ui->spectrum_chart,color(channel_colors[0]),LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_ext_y_array(ui->spectrum_chart,ui->spectrum_series,ui->spectrum_plot);
    for(unsigned i=0;i<SCOPE_POINTS;i++)ui->spectrum_plot[i]=-100000;
    for(unsigned i=0;i<5;i++){
        int x=i==0?64:i==4?736:64+(int)i*198-60;
        ui->spectrum_xlabels[i]=label(spectrum,x,362,120,"",MUTED,&scope_font_14);
        lv_obj_set_style_text_align(ui->spectrum_xlabels[i],i==0?LV_TEXT_ALIGN_LEFT:i==4?LV_TEXT_ALIGN_RIGHT:LV_TEXT_ALIGN_CENTER,0);
    }
    for(unsigned i=0;i<3;i++)ui->spectrum_values[i]=label(spectrum,24+(int)i*280,392,272,"",TEXT,&scope_font_16);
    ui->spectrum_update_button=button(spectrum,24,444,832,32,"",spectrum_action_event,ui);
    lv_obj_add_flag(ui->spectrum_panel,LV_OBJ_FLAG_HIDDEN);
    localize(ui);
    redraw(ui);
}
void scope_ui_step(ScopeUI *ui,double dt_s) {
    if(!scope_ui_autoset_busy(ui)&&ui->autoset_notice_s>0&&isfinite(dt_s)&&dt_s>0){
        ui->autoset_notice_s=fmax(0,ui->autoset_notice_s-dt_s);
        if(ui->autoset_notice_s==0)redraw(ui);
    }
    if(scope_ui_autoset_busy(ui)){
        if(!ui->model.running){autoset_restore(ui,SCOPE_AUTO_CANCELED,true);return;}
        if(isfinite(dt_s)&&dt_s>0)ui->autoset_elapsed_s+=dt_s;
        if(ui->autoset_elapsed_s>=ui->autoset_deadline_s)autoset_restore(ui,SCOPE_AUTO_TIMEOUT,false);
        return;
    }
    if(ui->model.running) {
        scope_advance(&ui->model,dt_s);
        ui->render_elapsed_s+=dt_s;
        if(ui->render_elapsed_s>=.033) {ui->render_elapsed_s=0;redraw(ui);}
    }
}

