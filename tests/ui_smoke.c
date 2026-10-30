#include "scope_ui.h"
#include "win32_host.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "src/misc/lv_text_private.h"

static void check_labels(lv_obj_t *obj,bool hidden) {
    hidden=hidden || lv_obj_has_flag(obj,LV_OBJ_FLAG_HIDDEN);
    if(lv_obj_check_type(obj,&lv_label_class)) {
        const char *text=lv_label_get_text(obj);
        const lv_font_t *font=lv_obj_get_style_text_font(obj,LV_PART_MAIN);
        uint32_t i=0;
        while(text[i]) {
            uint32_t code=lv_text_encoded_next(text,&i);
            if(code=='\n' || code=='\r')continue;
            lv_font_glyph_dsc_t glyph={0};
            assert(lv_font_get_glyph_dsc(font,&glyph,code,0));
            assert(!glyph.is_placeholder);
        }
        if(!hidden) {
            lv_obj_t *parent=lv_obj_get_parent(obj);
            assert(lv_obj_get_x(obj)>=0 && lv_obj_get_y(obj)>=0);
            assert(lv_obj_get_x(obj)+lv_obj_get_width(obj)<=lv_obj_get_width(parent));
            assert(lv_obj_get_y(obj)+lv_obj_get_height(obj)<=lv_obj_get_height(parent));
        }
    }
    for(uint32_t i=0;i<lv_obj_get_child_count(obj);i++)check_labels(lv_obj_get_child(obj,i),hidden);
}
static void check_language_layout(ScopeUI *ui) {
    lv_obj_update_layout(lv_screen_active());
    check_labels(lv_screen_active(),false);
    assert(lv_obj_get_height(ui->time_label)<=26);
    assert(lv_obj_get_height(ui->scale_label)<=26);
    assert(lv_obj_get_height(ui->frequency_label)<=26);
    assert(lv_obj_get_height(ui->footer_label)<=22);
}

int ui_smoke_run(ScopeUI *ui) {
    host_test_pump(120);
    assert(host_test_can_maximize());
    host_test_resize_client(2048,1200);
    host_test_click(1812,200); /* Exact 2x scaling: pause. */
    assert(!ui->model.running);
    host_test_quick_click(1812,200);
    assert(ui->model.running);
    host_test_resize_client(1600,900); /* 32px left/right bars, content scale 1.5. */
    host_test_click(10,150);
    assert(ui->model.running); /* Letterbox cannot trigger the UI. */
    host_test_click(1391,150);
    assert(!ui->model.running);
    host_test_resize_client(1280,1000); /* 125px top/bottom bars, scale 1.25. */
    host_test_click(1132,30);
    assert(!ui->model.running);
    host_test_click(1132,250);
    assert(ui->model.running);
    host_test_resize_client(1024,600);
    assert(strcmp(lv_label_get_text(ui->run_label),"停止")==0);
    assert(ui->language==SCOPE_ZH_CN);
    check_language_layout(ui);
    host_save_frame("artifacts/preview-zh.bmp");
    host_test_click(970,32);
    host_test_click(500,196);
    assert(ui->language==SCOPE_EN);
    assert(strcmp(lv_label_get_text(ui->run_label),"STOP")==0);
    check_language_layout(ui);
    host_save_frame("artifacts/settings-en.bmp");
    host_test_click(650,422);
    host_save_frame("artifacts/preview-en.bmp");
    host_test_click(970,32);
    host_test_click(500,196);
    host_save_frame("artifacts/settings-zh.bmp");
    host_test_click(650,422);
    assert(ui->language==SCOPE_ZH_CN);
    assert(ui->model.running);
    host_test_quick_click(906,100);
    assert(!ui->model.running); /* DOWN + UP both before next LVGL read. */
    host_test_quick_click(906,100);
    assert(ui->model.running);
    host_test_click(906, 100); /* RUN / STOP */
    assert(!ui->model.running);
    int32_t saved_samples[SCOPE_POINTS];
    for(int i=0;i<SCOPE_POINTS;i++) saved_samples[i]=ui->samples[i];
    double stopped = ui->model.elapsed_s;
    host_test_pump(150);
    assert(ui->model.elapsed_s == stopped);
    for(int i=0;i<SCOPE_POINTS;i++) assert(saved_samples[i]==ui->samples[i]);
    host_save_frame("artifacts/stopped.bmp");
    host_test_click(900, 192); /* square */
    assert(ui->model.wave == SCOPE_SQUARE);
    host_test_click(976, 192); /* triangle */
    assert(ui->model.wave == SCOPE_TRIANGLE);
    host_test_click(938, 272); /* timebase + */
    assert(ui->timebase_index == 3);
    bool changed=false;
    for(int i=0;i<SCOPE_POINTS;i++) if(saved_samples[i]!=ui->samples[i])changed=true;
    assert(changed);
    host_test_click(938, 356); /* scale + */
    assert(ui->scale_index == 3);
    host_test_click(938, 442); /* frequency + */
    assert(ui->frequency_index == 3);
    host_test_click(970, 32); /* settings */
    assert(!lv_obj_has_flag(ui->settings, LV_OBJ_FLAG_HIDDEN));
    host_test_click(382, 254); /* grid toggle */
    assert(!ui->grid_on);
    host_test_click(382, 312); /* colorbars */
    assert(ui->bars_on);
    /* Switching languages must preserve the stopped signal and every control. */
    double frozen=ui->model.elapsed_s;
    int32_t before_language[SCOPE_POINTS];
    memcpy(before_language,ui->samples,sizeof(before_language));
    lv_obj_t *chart=ui->chart;
    host_test_click(500,196);
    assert(ui->language==SCOPE_EN);
    assert(strcmp(lv_label_get_text(ui->run_label),"RUN")==0);
    assert(strcmp(lv_label_get_text(ui->state_label),"DEMO STOP")==0);
    assert(strcmp(lv_label_get_text(ui->settings_title),"INSTRUMENT SETUP")==0);
    check_language_layout(ui);
    host_test_click(500,196);
    assert(ui->language==SCOPE_ZH_CN);
    assert(strcmp(lv_label_get_text(ui->run_label),"运行")==0);
    assert(strcmp(lv_label_get_text(ui->settings_title),"仪器设置")==0);
    assert(ui->model.elapsed_s==frozen && !ui->model.running);
    assert(ui->model.wave==SCOPE_TRIANGLE && ui->model.frequency_hz==2000);
    assert(ui->timebase_index==3 && ui->scale_index==3 && ui->frequency_index==3);
    assert(!ui->grid_on && ui->bars_on && ui->chart==chart);
    assert(memcmp(before_language,ui->samples,sizeof(before_language))==0);
    assert(!lv_obj_has_flag(ui->settings,LV_OBJ_FLAG_HIDDEN));
    check_language_layout(ui);
    host_save_frame("artifacts/settings.bmp");
    host_test_click(650, 422); /* close */
    assert(lv_obj_has_flag(ui->settings, LV_OBJ_FLAG_HIDDEN));
    host_save_frame("artifacts/colorbars.bmp");
    host_test_click(970, 32);
    host_test_click(382, 312); /* return to waveform */
    host_test_click(382, 254); /* restore grid */
    host_test_click(650, 422);
    host_test_click(906, 100);
    assert(ui->model.running);
    host_test_pump(150);
    assert(ui->model.elapsed_s > stopped);
    /* Both ends of every control are bounded. */
    for (int i=0; i<12; ++i) host_test_click(938, 272);
    assert(ui->timebase_index == 6);
    for (int i=0; i<12; ++i) host_test_click(830, 272);
    assert(ui->timebase_index == 0);
    for(int i=0;i<12;i++) host_test_click(938,356);
    assert(ui->scale_index==6);
    for(int i=0;i<12;i++) host_test_click(830,356);
    assert(ui->scale_index==0);
    for(int i=0;i<12;i++) host_test_click(938,442);
    assert(ui->frequency_index==6);
    for(int i=0;i<12;i++) host_test_click(830,442);
    assert(ui->frequency_index==0);
    host_save_frame("artifacts/bounds.bmp");
    check_language_layout(ui);
    puts("PASS: default Chinese, bilingual pointer switching preserves state, fonts cover all labels, text fits");
    puts("PASS: real Win32 pointer messages -> LVGL hit testing -> controls/menu/render");
    return 0;
}
