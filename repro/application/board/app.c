#include "board.h"
#include "portable.h"
#include "scope_ui.h"
#ifdef SCOPE_TF_STORAGE
#include "scope_storage_ui.h"
#include "board_storage.h"
static ScopeStorageUI storage_panel;
#endif
#include "sleep.h"
#ifdef SCOPE_PHYSICAL_ADC
#include "board_adc_live.h"
#endif
#include <stdio.h>
#include <string.h>
#ifdef SCOPE_PHYSICAL_TOUCH
#include "board_touch.h"
#endif
#ifdef SCOPE_FREERTOS
#include "FreeRTOS.h"
#include "task.h"
#include <reent.h>
#define OS_NAME "freertos"
#else
#define OS_NAME "standalone"
#endif

static ScopeUI ui;
static ScopeTouchQueue touch_queue;
static int render_frozen,app_failed;
static volatile uint32_t heartbeat;
#ifdef SCOPE_FREERTOS
static TaskHandle_t ui_task_handle;
#endif
static char command[128];
static unsigned command_used;
static int command_overflow;

void board_delay(unsigned ms) {
#ifdef SCOPE_FREERTOS
    TickType_t ticks=pdMS_TO_TICKS(ms);
    vTaskDelay(ticks?ticks:1);
#else
    usleep(ms*1000u);
#endif
}
void board_lv_assert(const char *file,int line) {
    (void)line;board_panic("LVGL_ASSERT",file);
}
static void flush(lv_display_t *display,const lv_area_t *area,uint8_t *pixels) {
    if(area->x1!=0 || area->y1!=0 || area->x2!=1023 || area->y2!=599 ||
       board_video_present(pixels))app_failed=1;
    lv_display_flush_ready(display);
}
static void read_touch(lv_indev_t *device,lv_indev_data_t *data) {
    (void)device;ScopeTouchEvent event;bool more;
    scope_touch_next(&touch_queue,&event,&more);
    data->point=(lv_point_t){event.x,event.y};
    data->state=event.pressed?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;
    data->continue_reading=more;
}
#ifdef SCOPE_PHYSICAL_TOUCH
static void read_panel_touch(lv_indev_t *device,lv_indev_data_t *data) {
    (void)device;TouchPoint point=board_touch_point();
    data->point=(lv_point_t){point.x,point.y};
    data->state=point.pressed?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;
    data->continue_reading=false;
}
#endif
static void status(void) {
    board_log("UI_STATUS os=%s lang=%s running=%u wave=%u time=%d scale=%d freq=%u grid=%u bars=%u frozen=%u frames=%u touch_pending=%u source=%s display_id=%u seen_id=%u trig_normal=%u trig_rising=%u level_mv=%d triggered=%u view_start=%.3f crossing=%.3f waiting=%u single_armed=%u single_complete=%u epoch=%u dual=%u dual_visible=%u cursors=%u cursor_a=%u cursor_b=%u cursor_dt_us=%.6f cursor_dv=%.6f cursor_valid=%u cursor_inv_hz=%.6f ch1_scale=%d ch2_scale=%d ch1_pos=%d ch2_pos=%d channel_panel=%u config_ch=%u trig_channel=%u\r\n",
        OS_NAME,ui.language==SCOPE_ZH_CN?"zh-CN":"en",ui.model.running,(unsigned)ui.model.wave,
        ui.timebase_index,ui.scale_index,(unsigned)ui.model.frequency_hz,ui.grid_on,ui.bars_on,
        render_frozen,board_video_frames(),touch_queue.count,
        ui.adc_record?(ui.adc_channel==0?"ADC1":ui.adc_channel==1?"ADC2":"SIM"):"SIM",
        ui.adc_id,(unsigned)ui.adc_last_seen_id,ui.trigger_normal,ui.trigger_rising,ui.trigger_level_mv,
        ui.adc_triggered,ui.adc_display_start,ui.adc_trigger_crossing,ui.adc_waiting,ui.single_armed,ui.single_complete,(unsigned)ui.capture_epoch,
        ui.dual_display,ui.dual_display&&ui.adc_record&&ui.adc_channel<2,
        ui.cursors_on,ui.cursor_positions[0],ui.cursor_positions[1],1e6*ui.cursor_result.delta_s,
        ui.cursor_result.delta_v[ui.adc_channel<2?ui.adc_channel:0],ui.cursor_result.delta_valid[ui.adc_channel<2?ui.adc_channel:0],ui.cursor_result.reciprocal_hz,
        ui.channel_scale_index[0],ui.channel_scale_index[1],ui.channel_position_mdiv[0],ui.channel_position_mdiv[1],
        !lv_obj_has_flag(ui.channel_panel,LV_OBJ_FLAG_HIDDEN),ui.channel_config_target+1,ui.trigger_channel+1);
}
#ifdef SCOPE_PHYSICAL_ADC
static uint32_t calibration_bits(float value){
    uint32_t bits;memcpy(&bits,&value,sizeof(bits));return bits;
}
static void calibration_status(void){
    lv_lock();
    const ScopeCalibration *profiles[]={&ui.instrument_calibration,&ui.record_calibration,&ui.spectrum_calibration};
    const char *names[]={"instrument","record","spectrum"};
    board_log("CALIBRATION_STATUS display_id=%u epoch=%u instrument_flags=%u instrument_id=%08x record_flags=%u record_id=%08x spectrum_valid=%u spectrum_flags=%u spectrum_id=%08x\r\n",
        ui.adc_id,(unsigned)ui.capture_epoch,(unsigned)profiles[0]->flags,(unsigned)profiles[0]->id,
        (unsigned)profiles[1]->flags,(unsigned)profiles[1]->id,ui.spectrum_valid,
        (unsigned)profiles[2]->flags,(unsigned)profiles[2]->id);
    for(unsigned i=0;i<3;i++)board_log("CALIBRATION_COEFFICIENTS profile=%s slope1_bits=%08x offset1_bits=%08x slope2_bits=%08x offset2_bits=%08x\r\n",
        names[i],(unsigned)calibration_bits(profiles[i]->slope_v_per_code[0]),
        (unsigned)calibration_bits(profiles[i]->offset_v[0]),
        (unsigned)calibration_bits(profiles[i]->slope_v_per_code[1]),
        (unsigned)calibration_bits(profiles[i]->offset_v[1]));
    board_log("CALIBRATION_END display_id=%u epoch=%u\r\n",ui.adc_id,(unsigned)ui.capture_epoch);
    lv_unlock();
}
static void spectrum_status(void) {
    lv_lock();
    const ScopeSpectrumResult *s=&ui.spectrum_result;
    unsigned valid=ui.spectrum_valid;
    uint32_t rawcrc=valid?scope_record_crc32(ui.spectrum_words,sizeof(ui.spectrum_words)):0;
    uint32_t binscrc=valid?scope_record_crc32(s->vrms,sizeof(s->vrms)):0;
    uint32_t plotcrc=valid?scope_record_crc32(ui.spectrum_plot,sizeof(ui.spectrum_plot)):0;
    board_log("FFT_STATUS valid=%u panel=%u seq=%u channel=%u window=%u remove_mean=%u auto_range=%u end_bin=%u count=%u rate=%u id=%u source_epoch=%u conversion=%u actual_conversion=%u filter=%u raw_address=%08x raw_bytes=%u raw_crc=%08x bins_address=%08x bins_bytes=%u bins_crc=%08x plot_address=%08x plot_bytes=%u plot_crc=%08x elapsed_ms=%u\r\n",
        valid,!lv_obj_has_flag(ui.spectrum_panel,LV_OBJ_FLAG_HIDDEN),ui.spectrum_sequence,ui.spectrum_channel,
        (unsigned)ui.spectrum_window,ui.spectrum_remove_mean,ui.spectrum_auto_range,ui.spectrum_end_bin,
        s->count,s->rate_hz,ui.spectrum_id,ui.spectrum_source_epoch,ui.spectrum_conversion,ui.conversion_hz,ui.spectrum_filter,
        (unsigned)(uintptr_t)ui.spectrum_words,(unsigned)sizeof(ui.spectrum_words),(unsigned)rawcrc,
        (unsigned)(uintptr_t)s->vrms,(unsigned)sizeof(s->vrms),(unsigned)binscrc,
        (unsigned)(uintptr_t)ui.spectrum_plot,(unsigned)sizeof(ui.spectrum_plot),(unsigned)plotcrc,ui.spectrum_elapsed_ms);
    board_log("FFT_VALUES bin_hz=%.9f mean_v=%.9f gain=%.9f peak_valid=%u peak_bin=%.9f peak_hz=%.9f peak_vrms=%.9f peak_dbv=%.6f overrange=%u ui_epoch=%u running=%u display_id=%u live_rate=%u replay=%u wave_crc=%08x param_crc=%08x\r\n",
        s->bin_hz,s->mean_v,s->coherent_gain,s->peak_valid,s->peak_bin,s->peak_hz,s->peak_vrms,s->peak_dbv,s->overrange,
        ui.capture_epoch,ui.model.running,ui.adc_id,ui.adc_rate,ui.replay_active,
        (unsigned)(ui.memory_valid?scope_record_crc32(ui.memory_blob,sizeof(ui.memory_blob)):0),
        (unsigned)(ui.settings_valid?scope_record_crc32(ui.settings_blob,sizeof(ui.settings_blob)):0));
    board_log("FFT_END seq=%u\r\n",ui.spectrum_sequence);
    lv_unlock();
}
static void parameter_status(void) {
    /* Keep both independent RAM slots and the displayed record coherent while
       reporting them. Saved preferences never describe the live ADC clock. */
    lv_lock();
    const ScopeSettings *s=&ui.settings_cache;
    uint32_t crc=ui.settings_valid?scope_record_crc32(ui.settings_blob,sizeof(ui.settings_blob)):0;
    uint32_t rawcrc=ui.adc_record?scope_record_crc32(ui.adc_record,ui.adc_count*4u):0;
    uint32_t wavecrc=ui.memory_valid?scope_record_crc32(ui.memory_blob,sizeof(ui.memory_blob)):0;
    board_log("PARAM_STATUS valid=%u replay=%u panel=%u notice=%u blob_address=%08x blob_bytes=%u blob_crc=%08x epoch=%u running=%u display_id=%u rate=%u record_address=%08x record_bytes=%u record_crc=%08x actual_conversion=%u wave_valid=%u wave_crc=%08x\r\n",
        ui.settings_valid,ui.replay_active,!lv_obj_has_flag(ui.parameter_panel,LV_OBJ_FLAG_HIDDEN),ui.settings_notice,
        (unsigned)(uintptr_t)ui.settings_blob,(unsigned)sizeof(ui.settings_blob),(unsigned)crc,
        (unsigned)ui.capture_epoch,ui.model.running,ui.adc_id,ui.adc_rate,
        (unsigned)(uintptr_t)ui.adc_record,ui.adc_count*4u,(unsigned)rawcrc,ui.conversion_hz,
        ui.memory_valid,(unsigned)wavecrc);
    board_log("PARAM_CONFIG time_ns=%u origin_conversion=%u selected_channel=%u trigger_channel=%u level_mv=%d flags=%u ch1_scale=%u ch2_scale=%u ch1_pos=%d ch2_pos=%d cursor_a=%u cursor_b=%u language=%u\r\n",
        (unsigned)s->timebase_ns,(unsigned)s->origin_conversion_hz,(unsigned)s->selected_channel,
        (unsigned)s->trigger_channel,(int)s->trigger_level_mv,(unsigned)s->flags,
        (unsigned)s->scale_index[0],(unsigned)s->scale_index[1],(int)s->position_mdiv[0],(int)s->position_mdiv[1],
        (unsigned)s->cursor[0],(unsigned)s->cursor[1],(unsigned)s->language);
    board_log("PARAM_END epoch=%u\r\n",(unsigned)ui.capture_epoch);
    lv_unlock();
}
static void memory_status(void) {
    lv_lock();
    const ScopeRecordMeta *m=&ui.memory_meta;
    uint32_t crc=ui.memory_valid?scope_record_crc32(ui.memory_blob,sizeof(ui.memory_blob)):0;
    uint32_t rawcrc=ui.adc_record?scope_record_crc32(ui.adc_record,ui.adc_count*4u):0;
    board_log("MEMORY_STATUS valid=%u replay=%u panel=%u notice=%u blob_address=%08x blob_bytes=%u blob_crc=%08x saved_id=%u saved_rate=%u saved_conversion=%u saved_filter=%u saved_epoch=%u saved_time_ns=%u saved_flags=%u\r\n",
        ui.memory_valid,ui.replay_active,!lv_obj_has_flag(ui.memory_panel,LV_OBJ_FLAG_HIDDEN),ui.memory_notice,
        (unsigned)(uintptr_t)ui.memory_blob,(unsigned)sizeof(ui.memory_blob),(unsigned)crc,
        (unsigned)m->record_id,(unsigned)m->rate_hz,(unsigned)m->conversion_hz,(unsigned)m->filter_kind,
        (unsigned)m->capture_epoch,(unsigned)m->timebase_ns,(unsigned)m->flags);
    board_log("MEMORY_VIEW replay=%u record_address=%08x record_bytes=%u record_crc=%08x display_id=%u seen_id=%u rate=%u conversion=%u actual_conversion=%u filter=%u display_epoch=%u epoch=%u time=%d start=%.6f crossing=%.6f running=%u single_complete=%u\r\n",
        ui.replay_active,(unsigned)(uintptr_t)ui.adc_record,ui.adc_count*4u,(unsigned)rawcrc,
        ui.adc_id,ui.adc_last_seen_id,ui.adc_rate,ui.adc_info.conversion_hz,ui.conversion_hz,
        ui.adc_info.filter_kind,(unsigned)ui.adc_info.capture_epoch,(unsigned)ui.capture_epoch,ui.timebase_index,
        ui.adc_display_start,ui.adc_trigger_crossing,ui.model.running,ui.single_complete);
    board_log("MEMORY_END display_id=%u\r\n",ui.adc_id);
    lv_unlock();
}
static void autoset_status(void) {
    lv_lock();
    board_log("AUTOSET_STATUS state=%u busy=%u channel=%u epoch=%u display_id=%u seen_id=%u display_rate=%u time=%d target_time=%u target_mask=%u target_frequency_hz=%.6f trigger_channel=%u level_mv=%d running=%u single_complete=%u\r\n",
        (unsigned)ui.autoset_state,scope_ui_autoset_busy(&ui),ui.adc_channel+1,
        (unsigned)ui.capture_epoch,ui.adc_id,ui.adc_last_seen_id,ui.adc_rate,ui.timebase_index,
        ui.autoset_plan.timebase_index,ui.autoset_plan.channel_mask,ui.autoset_plan.frequency_hz,
        ui.trigger_channel+1,ui.trigger_level_mv,ui.model.running,ui.single_complete);
    for(unsigned ch=0;ch<2;ch++)board_log("AUTOSET_CHANNEL ch=%u scale=%d position=%d\r\n",ch+1,ui.channel_scale_index[ch],ui.channel_position_mdiv[ch]);
    board_log("AUTOSET_END epoch=%u\r\n",(unsigned)ui.capture_epoch);
    lv_unlock();
}
static void measurement_status(void) {
    /* UI task owns both the displayed record and its cached statistics. Keep
       this short diagnostic coherent; never append it to the main status. */
    lv_lock();
    board_log("MEASURE_STATUS panel=%u channel=%u display_id=%u rate=%u count=%u epoch=%u running=%u timing=%u\r\n",
        !lv_obj_has_flag(ui.measurement_panel,LV_OBJ_FLAG_HIDDEN),ui.adc_channel+1,
        ui.adc_id,ui.adc_rate,ui.adc_count,(unsigned)ui.capture_epoch,ui.model.running,ui.measurement_timing);
    for(unsigned ch=0;ch<2;ch++){
        const ScopeMeasurement *m=&ui.measurement[ch];double frequency=0;
        bool valid=scope_ui_measurement_frequency(&ui,ch,&frequency);
        double period_us=valid?1000000.0/frequency:0;
        board_log("MEASURE_CHANNEL ch=%u valid=%u overrange=%u min_v=%.6f max_v=%.6f mean_v=%.6f vpp_v=%.6f rms_total_v=%.6f rms_ac_v=%.6f frequency_valid=%u frequency_hz=%.6f period_us=%.6f\r\n",
            ch+1,m->valid,m->overrange,m->vmin_v,m->vmax_v,m->vmean_v,m->vpp_v,
            m->rms_total_v,m->rms_ac_v,valid,valid?frequency:0,period_us);
        const ScopeTimingMeasurement *t=&m->timing;
        board_log("MEASURE_TIMING ch=%u pulse_valid=%u rise_valid=%u fall_valid=%u cycles=%u duty_percent=%.6f positive_us=%.6f negative_us=%.6f period_us=%.6f rise_us=%.6f fall_us=%.6f low_v=%.6f mid_v=%.6f high_v=%.6f\r\n",
            ch+1,t->pulse_valid,t->rise_valid,t->fall_valid,t->cycles,t->duty_percent,
            t->positive_s*1e6,t->negative_s*1e6,t->period_s*1e6,t->rise_s*1e6,t->fall_s*1e6,
            t->low_v,t->mid_v,t->high_v);
    }
    board_log("MEASURE_END display_id=%u\r\n",ui.adc_id);
    lv_unlock();
}
#endif
static void run_command(void) {
    int x,y;char extra;
#ifdef SCOPE_TF_STORAGE
    if(!strcmp(command,"tfstatus")){board_storage_status();return;}
    unsigned tfkind=0,tfid=0,tfpage=0;
    if(sscanf(command,"tfopen %u %c",&tfkind,&extra)==1){
        lv_lock();bool ok=tfkind<2&&scope_storage_ui_open(&storage_panel,(ScopeStoreKind)tfkind);lv_unlock();
        board_log("TF_COMMAND tfopen %s seq=%u\r\n",ok?"QUEUED":"REJECTED",storage_panel.sequence);return;
    }
    if(sscanf(command,"tflist %u %u %c",&tfkind,&tfpage,&extra)==2||sscanf(command,"tfsave %u %c",&tfkind,&extra)==1||sscanf(command,"tfload %u %u %c",&tfkind,&tfid,&extra)==2){
        ScopeTfAction action=!strncmp(command,"tfsave ",7)?SCOPE_TF_SAVE:!strncmp(command,"tfload ",7)?SCOPE_TF_LOAD:SCOPE_TF_LIST;
        if(action!=SCOPE_TF_LOAD)tfid=0;
        if(action!=SCOPE_TF_LIST)tfpage=0;
        lv_lock();bool ok=false;if(tfkind<2&&!storage_panel.busy){storage_panel.kind=(ScopeStoreKind)tfkind;ok=scope_storage_ui_request(&storage_panel,action,tfid,tfpage);}lv_unlock();
        board_log("TF_COMMAND %s %s seq=%u\r\n",command,ok?"QUEUED":"REJECTED",storage_panel.sequence);return;
    }
    if(!strcmp(command,"tfclose")){
        lv_lock();if(!storage_panel.busy)lv_obj_add_flag(storage_panel.overlay,LV_OBJ_FLAG_HIDDEN);lv_unlock();board_log("TF_CLOSED busy=%u\r\n",storage_panel.busy);return;
    }
#endif
#ifdef SCOPE_PHYSICAL_ADC
    if(!strcmp(command,"adcstatus")){board_adc_live_status();return;}
    if(!strcmp(command,"calstatus")){calibration_status();return;}
    if(!strcmp(command,"fftstatus")){spectrum_status();return;}
    unsigned fftarg;char fftname[8];
    if(sscanf(command,"fft %u %c",&fftarg,&extra)==1){
        lv_lock();bool valid=fftarg<=1&&scope_ui_show_spectrum(&ui,fftarg!=0);lv_unlock();
        board_log("FFT_COMMAND fft %s\r\n",valid?"OK":"REJECTED");status();return;
    }
    if(!strcmp(command,"fftupdate")){
        lv_lock();bool valid=scope_ui_spectrum_update(&ui);lv_unlock();
        board_log("FFT_COMMAND fftupdate %s\r\n",valid?"OK":"REJECTED");status();return;
    }
    if(sscanf(command,"fftchannel %u %c",&fftarg,&extra)==1){
        lv_lock();bool valid=fftarg>=1&&fftarg<=2&&scope_ui_spectrum_channel(&ui,fftarg-1);lv_unlock();
        board_log("FFT_COMMAND fftchannel %s\r\n",valid?"OK":"REJECTED");status();return;
    }
    if(sscanf(command,"fftmean %u %c",&fftarg,&extra)==1){
        lv_lock();bool valid=fftarg<=1&&scope_ui_spectrum_mean(&ui,fftarg!=0);lv_unlock();
        board_log("FFT_COMMAND fftmean %s\r\n",valid?"OK":"REJECTED");status();return;
    }
    if(sscanf(command,"fftwindow %7s %c",fftname,&extra)==1){
        bool known=!strcmp(fftname,"rect")||!strcmp(fftname,"hann");
        lv_lock();bool valid=known&&scope_ui_spectrum_window(&ui,!strcmp(fftname,"hann")?SCOPE_SPECTRUM_HANN:SCOPE_SPECTRUM_RECT);lv_unlock();
        board_log("FFT_COMMAND fftwindow %s\r\n",valid?"OK":"REJECTED");status();return;
    }
    if(sscanf(command,"fftrange %7s %c",fftname,&extra)==1){
        bool known=!strcmp(fftname,"auto")||!strcmp(fftname,"full");
        lv_lock();bool valid=known&&scope_ui_spectrum_range(&ui,!strcmp(fftname,"auto"));lv_unlock();
        board_log("FFT_COMMAND fftrange %s\r\n",valid?"OK":"REJECTED");status();return;
    }
    if(!strcmp(command,"paramstatus")){parameter_status();return;}
    unsigned showParameters;
    if(sscanf(command,"parameters %u %c",&showParameters,&extra)==1){
        lv_lock();bool valid=showParameters<=1&&scope_ui_show_parameters(&ui,showParameters!=0);lv_unlock();
        if(!valid){board_log("PARAMETERS_REJECTED\r\n");}
        status();return;
    }
    if(!strcmp(command,"paramsave")||!strcmp(command,"paramload")||!strcmp(command,"paramdefault")){
        lv_lock();bool valid=!strcmp(command,"paramsave")?scope_ui_settings_save(&ui):
            !strcmp(command,"paramload")?scope_ui_settings_load(&ui):scope_ui_settings_defaults(&ui);
        lv_unlock();board_log("PARAM_COMMAND %s %s\r\n",command,valid?"OK":"REJECTED");status();return;
    }
    if(!strcmp(command,"recordstatus")){memory_status();return;}
    unsigned showMemory;
    if(sscanf(command,"records %u %c",&showMemory,&extra)==1){
        lv_lock();bool valid=showMemory<=1&&scope_ui_show_memory(&ui,showMemory!=0);lv_unlock();
        if(!valid){board_log("RECORDS_REJECTED\r\n");}
        status();return;
    }
    if(!strcmp(command,"memsave")||!strcmp(command,"memload")||!strcmp(command,"memsettings")||!strcmp(command,"memlive")){
        lv_lock();bool valid=!strcmp(command,"memsave")?scope_ui_memory_save(&ui):
            !strcmp(command,"memload")?scope_ui_memory_load(&ui):
            !strcmp(command,"memsettings")?scope_ui_memory_restore_settings(&ui):scope_ui_memory_return_live(&ui);
        lv_unlock();board_log("MEMORY_COMMAND %s %s\r\n",command,valid?"OK":"REJECTED");status();return;
    }
    if(!strcmp(command,"autostatus")){autoset_status();return;}
    if(!strcmp(command,"autoset")){
        lv_lock();bool started=scope_ui_autoset_start(&ui);lv_unlock();
        board_log("AUTOSET_COMMAND %s\r\n",started?"STARTED":"REJECTED");status();return;
    }
    if(!strcmp(command,"autocancel")){
        lv_lock();scope_ui_autoset_cancel(&ui);lv_unlock();
        board_log("AUTOSET_CANCEL_DONE\r\n");status();return;
    }
    if(!strcmp(command,"measurestatus")){measurement_status();return;}
    unsigned showTiming;
    if(sscanf(command,"timing %u %c",&showTiming,&extra)==1){
        lv_lock();bool valid=showTiming<=1&&scope_ui_show_timing_measurements(&ui,showTiming!=0);lv_unlock();
        board_log("TIMING_COMMAND %s\r\n",valid?"OK":"REJECTED");status();return;
    }
    unsigned showMeasurements;
    if(sscanf(command,"measurements %u %c",&showMeasurements,&extra)==1){
        lv_lock();bool valid=showMeasurements<=1&&scope_ui_show_measurements(&ui,showMeasurements!=0);lv_unlock();
        if(!valid)board_log("MEASUREMENTS_REJECTED\r\n");
        status();return;
    }
    unsigned channelIndex,channelScale;int channelPosition;
    if(sscanf(command,"trigsource %u %c",&channelIndex,&extra)==1){
        lv_lock();bool valid=channelIndex>=1&&channelIndex<=2&&scope_ui_select_trigger_source(&ui,channelIndex-1);lv_unlock();
        if(!valid)board_log("TRIGGER_SOURCE_REJECTED\r\n");
        status();return;
    }
    if(sscanf(command,"vertical %u %u %d %c",&channelIndex,&channelScale,&channelPosition,&extra)==3){
        lv_lock();bool valid=channelIndex>=1&&channelIndex<=2&&scope_ui_configure_channel(&ui,channelIndex-1,channelScale,channelPosition);lv_unlock();
        if(!valid)board_log("VERTICAL_REJECTED\r\n");
        status();return;
    }
    unsigned cursorA,cursorB;
    if(sscanf(command,"cursors %u %c",&cursorA,&extra)==1){
        lv_lock();if(cursorA<=1)scope_ui_set_cursors(&ui,cursorA!=0);lv_unlock();
        if(cursorA>1)board_log("CURSORS_REJECTED\r\n");
        status();return;
    }
    if(sscanf(command,"cursor %u %u %c",&cursorA,&cursorB,&extra)==2){
        lv_lock();bool valid=scope_ui_move_cursors(&ui,cursorA,cursorB);lv_unlock();
        if(!valid)board_log("CURSOR_POSITION_REJECTED\r\n");
        status();return;
    }
    unsigned dual;
    if(sscanf(command,"dual %u %c",&dual,&extra)==1){
        lv_lock();if(dual<=1)scope_ui_set_dual_display(&ui,dual!=0);lv_unlock();
        if(dual>1)board_log("DUAL_REJECTED\r\n");
        status();return;
    }
    if(!strcmp(command,"single")){
        lv_lock();bool armed=scope_ui_arm_single(&ui);lv_unlock();
        board_log("SINGLE_COMMAND %s\r\n",armed?"ARMED":"REJECTED");status();return;
    }
    if(!strcmp(command,"acqrun")||!strcmp(command,"acqstop")){
        lv_lock();scope_ui_set_running(&ui,!strcmp(command,"acqrun"));lv_unlock();status();return;
    }
    unsigned source;int millivolts;char edge[8],mode[8];
    if(sscanf(command,"source %u %c",&source,&extra)==1){
        lv_lock();bool changed=source>=1&&source<=3&&scope_ui_select_source(&ui,source-1);lv_unlock();
        if(!changed){board_log("SOURCE_REJECTED\r\n");}status();return;
    }
    if(sscanf(command,"trigger %7s %d %c",edge,&millivolts,&extra)==2){
        bool valid=!strcmp(edge,"rise")||!strcmp(edge,"fall");
        lv_lock();bool changed=valid&&scope_ui_configure_trigger(&ui,ui.trigger_normal,!strcmp(edge,"rise"),millivolts);lv_unlock();
        if(!changed){board_log("TRIGGER_REJECTED\r\n");}status();return;
    }
    if(sscanf(command,"triggermode %7s %c",mode,&extra)==1){
        bool valid=!strcmp(mode,"auto")||!strcmp(mode,"normal");
        lv_lock();bool changed=valid&&scope_ui_configure_trigger(&ui,!strcmp(mode,"normal"),ui.trigger_rising,ui.trigger_level_mv);lv_unlock();
        if(!changed){board_log("TRIGGERMODE_REJECTED\r\n");}status();return;
    }
#endif
#ifdef SCOPE_PHYSICAL_TOUCH
    unsigned swap,ix,iy;
    if(!strcmp(command,"touchstatus")){board_touch_status();return;}
    if(sscanf(command,"touchmap %u %u %u %c",&swap,&ix,&iy,&extra)==3){
        if(board_touch_mapping(swap,ix,iy))board_log("TOUCHMAP_REJECTED\r\n");
        return;
    }
#endif
    if(!strcmp(command,"status"))status();
    else if(!strcmp(command,"snapshot")) {
        lv_lock();lv_refr_now(lv_display_get_default());lv_unlock();
        if(app_failed)board_halt("Snapshot refresh failed");
        render_frozen=1;
        board_log("SNAPSHOT_READY addr=%08x bytes=1843200 format=BGR888 crc=%08x frames=%u\r\n",
            (unsigned)board_video_front_address(),(unsigned)board_video_crc(),board_video_frames());
    } else if(!strcmp(command,"resume")) {
        render_frozen=0;board_log("RENDER_RESUMED\r\n");
    } else if(sscanf(command,"tap %d %d %c",&x,&y,&extra)==2) {
        if(render_frozen || !scope_touch_tap(&touch_queue,x,y))board_log("TAP_REJECTED\r\n");
        else board_log("TAP_QUEUED x=%d y=%d\r\n",x,y);
    } else board_log("COMMANDS: status | tap x y | snapshot | resume\r\n");
}
static void poll_commands(void) {
    int ch;
    while((ch=board_uart_get())>=0) {
        if(ch=='\r')continue;
        if(ch=='\n') {
            command[command_used]=0;
            if(command_overflow)board_log("COMMAND_REJECTED: line too long\r\n");
            else if(command_used)run_command();
            command_used=0;command_overflow=0;
        } else if(!command_overflow) {
            if(command_used+1<sizeof(command))command[command_used++]=(char)ch;
            else command_overflow=1;
        }
    }
}
static void diagnose(void) {
    uint32_t signals=board_video_signals(),errors=board_video_errors();
#ifdef SCOPE_FREERTOS
    TaskStatus_t tasks[8];
    UBaseType_t count=uxTaskGetSystemState(tasks,8,NULL);
    unsigned ui_stack_words=0;
    for(UBaseType_t i=0;i<count;i++)if(tasks[i].xHandle==ui_task_handle)
        ui_stack_words=tasks[i].usStackHighWaterMark;
    board_log("APP_DIAG os=%s ms=%u frames=%u locked=%u underflow_sample=%u overflow_sample=%u pixel_locked=%u dma_errors=%08x heartbeat=%u tick=%u tasks=%u rtos_heap=%u ui_stack_words=%u\r\n",
        OS_NAME,(unsigned)board_millis(),board_video_frames(),signals&1,(signals>>1)&1,(signals>>2)&1,(signals>>3)&1,
        (unsigned)errors,(unsigned)heartbeat,(unsigned)xTaskGetTickCount(),(unsigned)uxTaskGetNumberOfTasks(),
        (unsigned)xPortGetFreeHeapSize(),ui_stack_words);
#else
    board_log("APP_DIAG os=%s ms=%u frames=%u locked=%u underflow_sample=%u overflow_sample=%u pixel_locked=%u dma_errors=%08x\r\n",
        OS_NAME,(unsigned)board_millis(),board_video_frames(),signals&1,(signals>>1)&1,(signals>>2)&1,(signals>>3)&1,
        (unsigned)errors);
#endif
    if(errors || (signals&9u)!=9u) {app_failed=1;board_video_stop();}
}
static void ui_run(void *unused) {
    (void)unused;
#ifdef SCOPE_FREERTOS
    vPortTaskUsesFPU();
#endif
    if(board_video_init())board_halt("Video initialization failed");
    lv_init();lv_tick_set_cb(board_millis);
    lv_display_t *display=lv_display_create(SCOPE_WIDTH,SCOPE_HEIGHT);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(display,(void*)SCOPE_XRGB_ADDRESS,NULL,SCOPE_XRGB_BYTES,LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display,flush);
    lv_indev_t *input=lv_indev_create();lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(input,read_touch);
#ifdef SCOPE_PHYSICAL_TOUCH
    (void)board_touch_init(); /* A missing controller must not take down video. */
    lv_indev_t *panel=lv_indev_create();lv_indev_set_type(panel,LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(panel,read_panel_touch);
#endif
    scope_ui_create(&ui);scope_ui_set_board_mode(&ui,true);
#ifdef SCOPE_TF_STORAGE
    scope_storage_ui_create(&storage_panel,&ui,board_storage_submit,NULL);
    if(board_storage_start(&storage_panel))board_halt("TF task initialization failed");
#endif
#ifdef SCOPE_PHYSICAL_ADC
    (void)board_adc_live_init();
#endif
    lv_refr_now(display);
    if(app_failed || !board_video_frames())board_halt("Initial LVGL frame failed");
    board_log("LVGL_READY os=%s version=9.2.2 lang=zh-CN input=%s frames=%u\r\n",OS_NAME,
#ifdef SCOPE_PHYSICAL_TOUCH
        "PANEL_I2C_AND_UART",
#else
        "UART_SIMULATED_TOUCH",
#endif
        board_video_frames());
    uint32_t previous=board_millis(),report=previous;
#ifdef SCOPE_PHYSICAL_TOUCH
    int panel_was_down=0;
#endif
    for(;;) {
        uint32_t now=board_millis(),dt=now-previous;previous=now;
        poll_commands();
#ifdef SCOPE_PHYSICAL_TOUCH
        board_touch_poll();
#endif
        if(!render_frozen && !app_failed) {
#ifdef SCOPE_TF_STORAGE
            lv_lock();board_storage_poll();lv_unlock();
#endif
#ifdef SCOPE_PHYSICAL_ADC
            lv_lock();board_adc_live_poll(&ui);lv_unlock();
#endif
            lv_lock();scope_ui_step(&ui,(dt>100?100:dt)/1000.0);lv_unlock();
            lv_timer_handler();
#ifdef SCOPE_PHYSICAL_TOUCH
            int panel_down=board_touch_point().pressed;
            if(panel_was_down&&!panel_down)status();
            panel_was_down=panel_down;
#endif
        }
        if(app_failed)board_halt("Display update failed; frame slots frozen");
        if((uint32_t)(now-report)>=1000) {report=now;diagnose();}
        board_delay(5);
    }
}
#ifdef SCOPE_FREERTOS
static void heartbeat_task(void *unused) {
    (void)unused;
    for(;;){heartbeat++;vTaskDelay(pdMS_TO_TICKS(100));}
}
void __malloc_lock(struct _reent *reent) {
    (void)reent;
    if(xTaskGetSchedulerState()!=taskSCHEDULER_NOT_STARTED)vTaskSuspendAll();
}
void __malloc_unlock(struct _reent *reent) {
    (void)reent;
    if(xTaskGetSchedulerState()!=taskSCHEDULER_NOT_STARTED)(void)xTaskResumeAll();
}
void vApplicationMallocFailedHook(void) {board_halt("FreeRTOS heap allocation failed");}
void vApplicationStackOverflowHook(TaskHandle_t task,char *name) {
    (void)task;board_panic("STACK_OVERFLOW",name);
}
#endif
int main(void) {
    if(board_init())for(;;)__asm__ volatile("wfi");
    board_log("APP_START os=%s lvgl=9.2.2 native_freertos_adapter=%u\r\n",OS_NAME,
#ifdef SCOPE_FREERTOS
        1u
#else
        0u
#endif
    );
#ifdef SCOPE_FREERTOS
    if(xTaskCreate(ui_run,"ScopeUI",8192,NULL,2,&ui_task_handle)!=pdPASS ||
       xTaskCreate(heartbeat_task,"Heartbeat",1024,NULL,1,NULL)!=pdPASS)board_halt("Task create failed");
    vTaskStartScheduler();board_halt("Scheduler returned");
#else
    ui_run(NULL);
#endif
    return 0;
}
