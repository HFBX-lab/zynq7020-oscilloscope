#include "scope_ui.h"
#include "scope_calibration_profile.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static uint32_t pixels[1024*60],first[4096],second[4096];
static void flush(lv_display_t *display,const lv_area_t *area,uint8_t *data){
    (void)area;(void)data;lv_display_flush_ready(display);
}
static bool feed(ScopeUI *ui,uint32_t words[4096],unsigned id){
    ScopeCapturePlan plan;ScopeHardwareConfig config;
    assert(!scope_ui_capture_plan(ui,&plan)&&!scope_ui_hardware_config(ui,&config));
    for(unsigned i=0;i<4096;i++){
        double phase=6.283185307179586*50000*i/plan.rate_hz;
        words[i]=(unsigned)lround(505+50*sin(phase))|
            ((unsigned)lround(510+60*cos(phase))<<16);
    }
    double q=config.level_q8/256.0;
    unsigned shift=ui->trigger_channel*16;
    uint32_t mask=UINT32_C(0x3ff)<<shift;
    words[config.pre_samples-1]=(words[config.pre_samples-1]&~mask)|((unsigned)floor(q-2)<<shift);
    words[config.pre_samples]=(words[config.pre_samples]&~mask)|((unsigned)ceil(q+2)<<shift);
    ScopeRecordInfo info={ui->capture_epoch,plan.rate_index,plan.conversion_hz,plan.filter_kind};
    ScopeHardwareTrigger trigger={true,false,config.pre_samples};
    const ScopeAdcMetrics metrics[2]={{1,.5,50000,true},{1,.5,50000,true}};
    return scope_ui_set_hardware_adc_record(ui,words,4096,plan.rate_hz,id,metrics,&info,&trigger);
}
int main(void){
    ScopeCalibration profile={.flags=0x103,.id=321,
        .slope_v_per_code={.009f,.011f},.offset_v={-4.55f,-5.3f}};
    lv_init();lv_display_t *display=lv_display_create(1024,600);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(display,pixels,NULL,sizeof(pixels),LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display,flush);
    static ScopeUI ui;scope_ui_create(&ui);
    assert(!ui.instrument_calibration.flags);
    assert(scope_ui_set_calibration(&ui,&profile));
    scope_ui_enable_adaptive_sampling(&ui,true);scope_ui_enable_hardware_trigger(&ui,true);
    assert(scope_ui_set_conversion_hz(&ui,50000000));ui.timebase_index=6;
    assert(scope_ui_select_source(&ui,0)&&scope_ui_select_trigger_source(&ui,0));
    assert(scope_ui_configure_trigger(&ui,false,true,0));
    ScopeHardwareConfig hardware;uint32_t expected_q8;
    assert(!scope_ui_hardware_config(&ui,&hardware));
    assert(!scope_calibration_trigger_q8(&profile,0,0,&expected_q8));
    assert(hardware.level_q8==expected_q8&&hardware.level_q8!=130944);
    assert(feed(&ui,first,1));
    assert(scope_ui_show_measurements(&ui,true));
    assert(strstr(lv_label_get_text(ui.measurement_note),"源设定"));
    assert(scope_ui_show_measurements(&ui,false));
    assert(!memcmp(&ui.record_calibration,&profile,sizeof(profile)));
    assert(!scope_ui_set_calibration(&ui,NULL));
    for(unsigned ch=0;ch<2;ch++){
        assert(fabs(ui.adc_metrics[ch].vpp_v-ui.measurement[ch].vpp_v)<1e-12);
        assert(fabs(ui.adc_metrics[ch].rms_v-ui.measurement[ch].rms_total_v)<1e-12);
    }
    scope_ui_set_running(&ui,false);scope_ui_set_cursors(&ui,true);
    assert(scope_ui_move_cursors(&ui,100,900));
    assert(scope_ui_memory_save(&ui));
    assert(ui.memory_blob[4]==2&&ui.memory_meta.calibration.id==321);
    assert(ui.memory_meta.trigger_level_q8==expected_q8);
    uint32_t old_crc=scope_record_crc32(first,sizeof(first));
    assert(scope_ui_spectrum_update(&ui));
    assert(ui.spectrum_calibration.id==321);
    unsigned frozen_id=ui.adc_id;
    assert(scope_ui_select_source(&ui,1)&&ui.adc_id==frozen_id&&!ui.model.running);
    assert(!memcmp(&ui.record_calibration,&profile,sizeof(profile)));
    scope_ui_set_running(&ui,true);ui.timebase_index=5;
    assert(feed(&ui,second,2));
    assert(scope_ui_memory_load(&ui)&&ui.replay_active&&ui.adc_id==1);
    assert(ui.adc_rate==6250000&&ui.record_calibration.id==321);
    assert(scope_ui_memory_return_live(&ui)&&ui.adc_id==2&&ui.adc_rate==50000000);
    assert(ui.record_calibration.id==321&&scope_record_crc32(first,sizeof(first))==old_crc);
    /* Import a valid legacy record. Its volts stay nominal on a calibrated instrument. */
    ScopeRecordMeta legacy=ui.memory_meta;
    memset(&legacy.calibration,0,sizeof(legacy.calibration));legacy.trigger_level_q8=0;
    legacy.flags&=~SCOPE_RECORD_TRIGGERED;legacy.trigger_after_index=0;
    legacy.view_start_q16=0;legacy.trigger_crossing_q16=0;
    size_t written;
    assert(!scope_record_encode(&legacy,first,ui.memory_blob,sizeof(ui.memory_blob),&written));
    assert(scope_ui_memory_load(&ui)&&!ui.record_calibration.flags&&ui.instrument_calibration.id==321);
    assert(scope_ui_show_measurements(&ui,true));
    assert(strstr(lv_label_get_text(ui.measurement_note),"未校准"));
    assert(scope_ui_show_measurements(&ui,false));
    ScopeMeasurement nominal[2];assert(!scope_measurement_analyze(first,4096,6250000,nominal));
    assert(fabs(ui.measurement[0].vmean_v-nominal[0].vmean_v)<1e-12);
    assert(scope_ui_spectrum_update(&ui)&&!ui.spectrum_calibration.flags);
    assert(scope_ui_memory_return_live(&ui)&&ui.record_calibration.id==321);
    assert(scope_ui_settings_save(&ui)&&scope_ui_settings_defaults(&ui));
    assert(ui.instrument_calibration.id==321&&ui.record_calibration.id==321);
    assert(scope_ui_settings_load(&ui)&&ui.instrument_calibration.id==321);
    lv_deinit();puts("CALIBRATION_UI_PASS: trigger,stats,freeze,FFT,v2,v1-replay,live,settings");
    return 0;
}
