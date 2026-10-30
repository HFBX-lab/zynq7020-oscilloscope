"""Keep autoset diagnostics independent of the existing UI_STATUS line."""
from pathlib import Path
import re
import subprocess

root=Path(__file__).resolve().parents[1]
folder=Path('D:/FPGA_Project/SP2_Autoset_Build/log-test');folder.mkdir(parents=True,exist_ok=True)
source=(root/'firmware/lvgl/app.c').read_text(encoding='utf-8')
match=re.search(r'board_log\(("AUTOSET_STATUS [^"]+")',source)
assert match,'production autoset diagnostic missing'
specs=re.findall(r'%(?:\.\d+)?([udf])',match.group(1))
args=','.join('UINT32_MAX' if s=='u' else 'INT_MIN' if s=='d' else '12500000.123456' for s in specs)
code=r'''
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
int main(void){char text[1024];int n=snprintf(text,sizeof text,FORMAT,ARGS);
if(n<2||n>=1024||strcmp(text+n-2,"\r\n"))return 1;
printf("AUTOSET_LOG_PASS: %d/1024 bytes\n",n);return 0;}
'''.replace('FORMAT',match.group(1)).replace('ARGS',args)
(folder/'test.c').write_text(code)
subprocess.run(['D:/msys64/ucrt64/bin/gcc.exe','-Wall','-Wextra','-Werror',str(folder/'test.c'),'-o',str(folder/'test.exe')],check=True)
subprocess.run([str(folder/'test.exe')],check=True)
