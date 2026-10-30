#ifndef SCOPE_UI_H
#define SCOPE_UI_H
#include "lvgl.h"
#include "scope_model.h"
#include "scope_cursor.h"
#include "scope_acquisition.h"
#include "scope_measurement.h"
#include "scope_autoset.h"
#include "scope_record.h"
#include "scope_settings.h"
#include "scope_spectrum.h"
#define SCOPE_WIDTH 1024
#define SCOPE_HEIGHT 600
#define SCOPE_POINTS 720
typedef enum {SCOPE_ZH_CN, SCOPE_EN} ScopeLanguage;
typedef struct {double vpp_v,rms_v,frequency_hz;bool frequency_valid;} ScopeAdcMetrics;
typedef enum {
    SCOPE_AUTO_IDLE=0,SCOPE_AUTO_WAITING,SCOPE_AUTO_COMPLETE,SCOPE_AUTO_NO_RECORD,
    SCOPE_AUTO_NO_SIGNAL,SCOPE_AUTO_OVERRANGE,SCOPE_AUTO_UNSTABLE,
    SCOPE_AUTO_UNSUPPORTED,SCOPE_AUTO_CANCELED,SCOPE_AUTO_TIMEOUT
} ScopeAutosetState;
typedef struct {
    int timebase_index,scale_index,channel_scale_index[2],channel_position_mdiv[2];
    unsigned trigger_channel;
    bool trigger_normal,trigger_rising,running,single_complete,adc_triggered,adc_waiting;
    int trigger_level_mv;
    double adc_display_start,adc_trigger_crossing;
} ScopeAutosetBackup;

/* Live capture owns its DDR slot. Replay has separate storage and only backs
 * up view/capture state; LVGL objects and the volatile slot are never copied. */
typedef struct {
    const uint32_t *record;
    unsigned count,rate,id,channel,config_target;
    ScopeRecordInfo info;
    ScopeCalibration calibration;
    ScopeAdcMetrics metrics[2];
    ScopeMeasurement measurement[2];
    int timebase_index,scale_index,channel_scale_index[2],channel_position_mdiv[2];
    unsigned trigger_channel,cursor_positions[2];
    int trigger_level_mv;
    bool trigger_normal,trigger_rising,adc_triggered,adc_waiting;
    bool dual_display,cursors_on,grid_on,bars_on;
    double display_start,trigger_crossing;
} ScopeLiveViewBackup;
typedef struct {
    ScopeModel model;
    ScopeLanguage language;
    int timebase_index, scale_index, frequency_index;
    int channel_scale_index[2],channel_position_mdiv[2],sim_scale_index;
    unsigned channel_config_target;
    bool grid_on, bars_on, board_mode;
    double render_elapsed_s;
    lv_obj_t *chart, *bars, *settings, *run_button, *run_label, *state_label;
    lv_obj_t *wave_buttons[3], *scale_label, *time_label, *frequency_label;
    lv_obj_t *vpp_label, *rms_label, *readout_label, *grid_button, *bars_button;
    lv_obj_t *title_label, *subtitle_label, *channel_label, *axes_label, *wave_title;
    lv_obj_t *source_note, *footer_label, *metric_titles[3], *settings_button;
    lv_obj_t *settings_title, *settings_note, *close_button, *language_button;
    lv_chart_series_t *series;
    int32_t samples[SCOPE_POINTS];
    lv_chart_series_t *secondary_series;
    int32_t secondary_samples[SCOPE_POINTS];
    bool dual_display;
    lv_obj_t *dual_button,*channel_legend[2];
    bool cursors_on;
    unsigned cursor_positions[2];
    int cursor_dragging;
    ScopeCursorResult cursor_result;
    lv_obj_t *cursor_button,*cursor_line[2],*cursor_tag[2];
    const uint32_t *adc_record;
    unsigned adc_count,adc_rate,adc_id,adc_channel;
    bool adaptive_sampling,hardware_trigger;
    unsigned conversion_hz;
    ScopeRecordInfo adc_info;
    ScopeCalibration instrument_calibration,record_calibration;
    unsigned trigger_channel;
    ScopeAdcMetrics adc_metrics[2];
    ScopeMeasurement measurement[2];
    lv_obj_t *measurement_panel,*measurement_button,*measurement_title,*measurement_note;
    lv_obj_t *measurement_values[8],*measurement_names[8],*measurement_choices[2],*measurement_close_button;
    bool measurement_timing;
    lv_obj_t *measurement_timing_button,*timing_names[6],*timing_values[6];
    lv_obj_t *adjust_buttons[6];
    bool trigger_normal,trigger_rising,adc_triggered,adc_waiting;
    int trigger_level_mv;
    bool single_armed,single_complete;
    uint32_t capture_epoch,single_armed_after_id,adc_last_seen_id;
    double adc_display_start,adc_trigger_crossing;
    lv_obj_t *single_button,*trigger_button,*trigger_mode_button,*settings_dialog;
    lv_obj_t *trigger_source_button;
    lv_obj_t *channel_panel_button,*channel_panel,*channel_panel_title,*channel_choices[2];
    lv_obj_t *channel_scale_label,*channel_position_label,*channel_adjust[4];
    lv_obj_t *channel_zero_button,*channel_close_button,*zero_marker[2];
    lv_obj_t *zero_leader[2];
    lv_point_precise_t zero_leader_points[2][3];
    ScopeAutosetState autoset_state;
    ScopeAutosetPlan autoset_plan;
    ScopeAutosetBackup autoset_backup;
    double autoset_elapsed_s,autoset_deadline_s,autoset_notice_s;
    bool autoset_need_wider;
    const char *autoset_message;
    lv_obj_t *autoset_button,*autoset_panel,*autoset_label,*autoset_cancel_button;
    uint8_t memory_blob[SCOPE_RECORD_BLOB_BYTES];
    uint32_t replay_words[SCOPE_RECORD_WORDS];
    ScopeRecordMeta memory_meta;
    ScopeRecordMeta replay_meta;
    bool replay_from_tf;
    char replay_filename[13];
    void (*storage_open)(void *,unsigned);
    void *storage_context;
    lv_obj_t *tf_record_button,*tf_parameter_button;
    ScopeLiveViewBackup live_view_backup;
    bool memory_valid,replay_active;
    unsigned memory_notice;
    lv_obj_t *memory_panel,*memory_button,*memory_title,*memory_note;
    lv_obj_t *memory_save_button,*memory_load_button,*memory_settings_button,*memory_live_button,*memory_close_button;
    uint8_t settings_blob[SCOPE_SETTINGS_BLOB_BYTES];
    ScopeSettings settings_cache;
    bool settings_valid;
    unsigned settings_notice;
    lv_obj_t *parameter_button,*parameter_panel,*parameter_title,*parameter_note;
    lv_obj_t *parameter_save_button,*parameter_load_button,*parameter_default_button,*parameter_close_button;
    ScopeSpectrumWorkspace spectrum_workspace;
    ScopeSpectrumResult spectrum_result;
    ScopeCalibration spectrum_calibration;
    uint32_t spectrum_words[SCOPE_SPECTRUM_N];
    bool spectrum_valid,spectrum_remove_mean,spectrum_auto_range;
    ScopeSpectrumWindow spectrum_window;
    unsigned spectrum_channel,spectrum_id,spectrum_source_epoch,spectrum_conversion,spectrum_filter;
    unsigned spectrum_sequence,spectrum_elapsed_ms,spectrum_end_bin;
    uint32_t spectrum_raw_crc;
    int32_t spectrum_plot[SCOPE_POINTS];
    lv_obj_t *spectrum_button,*spectrum_panel,*spectrum_title,*spectrum_note,*spectrum_chart;
    lv_obj_t *spectrum_choices[2],*spectrum_window_button,*spectrum_mean_button,*spectrum_range_button;
    lv_obj_t *spectrum_update_button,*spectrum_close_button,*spectrum_values[3],*spectrum_xlabels[5];
    lv_chart_series_t *spectrum_series;
} ScopeUI;
/* Set once before the first ADC record. Existing/frozen/replayed records can
 * never be relabelled by changing the instrument voltage profile. */
bool scope_ui_set_calibration(ScopeUI *ui,const ScopeCalibration *profile);
bool scope_ui_show_spectrum(ScopeUI *ui,bool show);
bool scope_ui_spectrum_update(ScopeUI *ui);
bool scope_ui_spectrum_channel(ScopeUI *ui,unsigned channel);
bool scope_ui_spectrum_window(ScopeUI *ui,ScopeSpectrumWindow window);
bool scope_ui_spectrum_mean(ScopeUI *ui,bool remove_mean);
bool scope_ui_spectrum_range(ScopeUI *ui,bool auto_range);
bool scope_ui_settings_save(ScopeUI *ui);
bool scope_ui_settings_load(ScopeUI *ui);
bool scope_ui_settings_defaults(ScopeUI *ui);
bool scope_ui_show_parameters(ScopeUI *ui,bool show);
bool scope_ui_memory_save(ScopeUI *ui);
bool scope_ui_export_record(ScopeUI *ui,uint8_t *blob,size_t capacity);
bool scope_ui_export_settings(ScopeUI *ui,uint8_t *blob,size_t capacity);
bool scope_ui_import_record(ScopeUI *ui,const uint8_t *blob,size_t length,const char *filename);
bool scope_ui_import_settings(ScopeUI *ui,const uint8_t *blob,size_t length);
bool scope_ui_memory_load(ScopeUI *ui);
bool scope_ui_memory_restore_settings(ScopeUI *ui);
bool scope_ui_memory_return_live(ScopeUI *ui);
bool scope_ui_show_memory(ScopeUI *ui,bool show);
bool scope_ui_autoset_start(ScopeUI *ui);
void scope_ui_autoset_cancel(ScopeUI *ui);
bool scope_ui_autoset_busy(const ScopeUI *ui);
void scope_ui_create(ScopeUI *ui);
void scope_ui_step(ScopeUI *ui, double dt_s);
void scope_ui_set_board_mode(ScopeUI *ui,bool enabled);
bool scope_ui_set_adc_record(ScopeUI *ui,const uint32_t *record,unsigned count,unsigned rate,unsigned id,const ScopeAdcMetrics metrics[2]);
void scope_ui_enable_adaptive_sampling(ScopeUI *ui,bool enabled);
bool scope_ui_set_conversion_hz(ScopeUI *ui,unsigned conversion_hz);
int scope_ui_capture_plan(const ScopeUI *ui,ScopeCapturePlan *out);
bool scope_ui_set_adc_record_info(ScopeUI *ui,const uint32_t *record,unsigned count,unsigned rate,unsigned id,const ScopeAdcMetrics metrics[2],const ScopeRecordInfo *info);
int scope_ui_hardware_config(const ScopeUI *ui,ScopeHardwareConfig *out);
void scope_ui_enable_hardware_trigger(ScopeUI *ui,bool enabled);
bool scope_ui_set_hardware_adc_record(ScopeUI *ui,const uint32_t *record,unsigned count,unsigned rate,unsigned id,const ScopeAdcMetrics metrics[2],const ScopeRecordInfo *info,const ScopeHardwareTrigger *trigger);
bool scope_ui_arm_single(ScopeUI *ui);
void scope_ui_set_running(ScopeUI *ui,bool running);
bool scope_ui_configure_trigger(ScopeUI *ui,bool normal,bool rising,int level_mv);
bool scope_ui_select_source(ScopeUI *ui,unsigned source);
void scope_ui_set_dual_display(ScopeUI *ui,bool enabled);
void scope_ui_set_cursors(ScopeUI *ui,bool enabled);
const char *scope_ui_acquisition_error_text(const ScopeUI *ui);
bool scope_ui_move_cursors(ScopeUI *ui,unsigned a,unsigned b);
bool scope_ui_configure_channel(ScopeUI *ui,unsigned channel,unsigned scale_index,int position_mdiv);
bool scope_ui_select_trigger_source(ScopeUI *ui,unsigned channel);
bool scope_ui_show_measurements(ScopeUI *ui,bool show);
bool scope_ui_show_timing_measurements(ScopeUI *ui,bool timing);
bool scope_ui_measurement_frequency(const ScopeUI *ui,unsigned channel,double *frequency_hz);
#endif
