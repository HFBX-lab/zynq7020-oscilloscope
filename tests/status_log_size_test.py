"""Format the actual UI_STATUS with wide bounded values and the board capacity."""
from pathlib import Path
import re,subprocess
root=Path(__file__).resolve().parents[1]
out=Path('D:/FPGA_Project/SP2_Channel_Build/host/status-log');out.mkdir(parents=True,exist_ok=True)
literal=re.search(r'board_log\(("UI_STATUS [^"]+")',(root/'firmware/lvgl/app.c').read_text()).group(1)
specs=re.findall(r'%(?:\.\d+)?([sudf])',literal)
strings=iter(['"freertos"','"zh-CN"','"ADC2"'])
args=[next(strings) if s=='s' else 'UINT32_MAX' if s=='u' else 'INT_MIN' if s=='d' else '-1000000000.123456' for s in specs]
code='''#include "board.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>
int main(void){
 char text[SCOPE_LOG_CAPACITY];
 int (*format_line)(char*,size_t,const char*,...)=snprintf;
 int n=format_line(text,sizeof(text),FORMAT,ARGS);
 if(n<0||(size_t)n>=sizeof(text)||n<2||strcmp(text+n-2,"\\r\\n")){fprintf(stderr,"UI_STATUS_LOG_TOO_LONG: %d bytes / %u capacity\\n",n,(unsigned)sizeof(text));return 1;}
 printf("UI_STATUS_LOG_SIZE_PASS: %d bytes / %u capacity, complete CRLF\\n",n,(unsigned)sizeof(text));
}
'''.replace('FORMAT',literal).replace('ARGS',','.join(args))
(out/'status_log_test.c').write_text(code)
subprocess.run(['D:/msys64/ucrt64/bin/gcc.exe','-std=c11','-pipe','-Wall','-Wextra','-Werror','-I',str(root/'firmware/lvgl'),str(out/'status_log_test.c'),'-o',str(out/'status_log_test.exe')],check=True,cwd=out)
subprocess.run([str(out/'status_log_test.exe')],check=True)
