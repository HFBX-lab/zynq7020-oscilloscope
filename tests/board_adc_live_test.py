"""Run the real board poller against isolated MMIO and UI doubles on Windows.

Only ARM barriers are replaced in the temporary host translation unit. No board
port or serial/JTAG access is linked. Build products stay on the D drive.
"""
from pathlib import Path
import subprocess
import sys
import re

ROOT = Path(__file__).resolve().parents[1]
BUILD = Path(sys.argv[1] if len(sys.argv) > 1 else
             "D:/FPGA_Project/SP2_Highspeed_Build/50m/host_board")
BUILD.mkdir(parents=True, exist_ok=True)
STUBS = {
    "scope_ui.h": r'''
#include <stdbool.h>
#include <stdint.h>
#include "scope_acquisition.h"
typedef struct {double vpp_v,rms_v,frequency_hz;bool frequency_valid;} ScopeAdcMetrics;
typedef struct {struct {bool running;} model;uint32_t capture_epoch;int timebase_index;unsigned conversion_hz;
 const uint32_t *adc_record;unsigned adc_id,adc_rate;void *run_button,*state_label;
 bool adaptive_sampling,hardware_trigger;ScopeRecordInfo adc_info;} ScopeUI;
#define LV_STATE_DISABLED 1
void lv_obj_add_state(void *,unsigned);
void lv_label_set_text(void *,const char *);
const char *scope_ui_acquisition_error_text(const ScopeUI *);
bool scope_ui_set_adc_record(ScopeUI *,const uint32_t *,unsigned,unsigned,unsigned,const ScopeAdcMetrics[2]);
bool scope_ui_set_adc_record_info(ScopeUI *,const uint32_t *,unsigned,unsigned,unsigned,const ScopeAdcMetrics[2],const ScopeRecordInfo *);
void scope_ui_enable_adaptive_sampling(ScopeUI *,bool);
bool scope_ui_set_conversion_hz(ScopeUI *,unsigned);
int scope_ui_capture_plan(const ScopeUI *,ScopeCapturePlan *);
void scope_ui_enable_hardware_trigger(ScopeUI *,bool);
int scope_ui_hardware_config(const ScopeUI *,ScopeHardwareConfig *);
bool scope_ui_set_hardware_adc_record(ScopeUI *,const uint32_t *,unsigned,unsigned,unsigned,const ScopeAdcMetrics[2],const ScopeRecordInfo *,const ScopeHardwareTrigger *);
int scope_acquisition_plan(unsigned,ScopeCapturePlan *);
''',
    "board.h": "#include <stdint.h>\nuint32_t board_millis(void);\nvoid board_delay(unsigned);\nvoid board_log(const char *,...) __attribute__((format(printf,1,2)));\n",
    "xparameters.h": "",
    "xil_io.h": "#include <stdint.h>\nuint32_t Xil_In32(uintptr_t);\nvoid Xil_Out32(uintptr_t,uint32_t);\n",
    "xil_cache.h": "#include <stdint.h>\nvoid Xil_DCacheFlushRange(uintptr_t,unsigned);\n",
    "frame_source.h": "#include <stdint.h>\nuint32_t screen_crc32(const uint8_t *,unsigned);\n",
}
for name, content in STUBS.items():
    (BUILD / name).write_text(content, encoding="utf-8")
source = (ROOT / "firmware/adc_live/board_adc_live.c").read_text(encoding="utf-8")
barrier = '__asm__ volatile("dsb sy":::"memory");'
assert source.count(barrier) == 2, "Review ARM barrier substitution if poller changes"
(BUILD / "board_adc_live_host.c").write_text(source.replace(barrier, "(void)0;"), encoding="utf-8")
exe = BUILD / "board_adc_live_test.exe"
cmd = ["D:/msys64/ucrt64/bin/gcc.exe", "-std=c11", "-Wall", "-Wextra", "-Werror",
       "-I" + str(BUILD), "-I" + str(ROOT / "firmware/adc_live"), "-I" + str(ROOT / "src"),
       str(BUILD / "board_adc_live_host.c"), str(ROOT / "firmware/adc_live/record_stats.c"),
       str(ROOT / "src/scope_acquisition.c"), str(ROOT / "src/scope_measurement.c"),
       str(ROOT / "tests/board_adc_live_test.c"), "-lm", "-o", str(exe)]
subprocess.run(cmd, check=True)
subprocess.run([str(exe)], check=True)

# Exercise the exact production format with widest uint32 fields, rather than
# assuming the normal small IDs/ring indices establish the logger bound.
fmt = re.search(r'board_log\(("ADC_LIVE_STATUS [^"]+")', source).group(1)
arguments = ",".join("UINT32_MAX" for _ in re.finditer(r"%(?:0\d+)?[ux]", fmt))
log_test = r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
int main(void){
 char line[1024];int n=snprintf(line,sizeof line,FORMAT,ARGUMENTS);
 if(n<2||(unsigned)n>=sizeof line||strcmp(line+n-2,"\r\n"))return 1;
 printf("ADC_LIVE_STATUS widest fields: %d / %u bytes PASS\n",n,(unsigned)sizeof line);return 0;
}
'''.replace("FORMAT", fmt).replace("ARGUMENTS", arguments)
(BUILD / "adc_status_size_test.c").write_text(log_test, encoding="utf-8")
log_exe = BUILD / "adc_status_size_test.exe"
subprocess.run(["D:/msys64/ucrt64/bin/gcc.exe", "-std=c11", "-Wall", "-Wextra", "-Werror",
                str(BUILD / "adc_status_size_test.c"), "-o", str(log_exe)], check=True)
subprocess.run([str(log_exe)], check=True)
