"""New storage diagnostics must fit the production1024-byte logger."""
from pathlib import Path
import re
import subprocess
root=Path(__file__).resolve().parents[1]
folder=Path('D:/FPGA_Project/SP2_Memory_Build/log-test');folder.mkdir(parents=True,exist_ok=True)
source=(root/'firmware/lvgl/app.c').read_text()
for name in ('MEMORY_STATUS','MEMORY_VIEW'):
    match=re.search(r'board_log\(("'+name+r' [^"]+")',source)
    assert match,'production record diagnostics missing: '+name
    spec=re.findall(r'%(?:08|\.\d+)?([uxdf])',match.group(1))
    args=','.join('INT_MIN' if s=='d' else '-4095.123456' if s=='f' else 'UINT32_MAX' for s in spec)
    code=r'''#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>
int main(void){char b[1024];int (*format)(char*,size_t,const char*,...)=snprintf;
int n=format(b,sizeof b,FORMAT,ARGS);if(n<2||n>=1024||strcmp(b+n-2,"\r\n"))return 1;
printf("NAME_LOG_PASS: %d/1024 bytes\n",n);return 0;}
'''.replace('FORMAT',match.group(1)).replace('ARGS',args).replace('NAME',name)
    c=folder/(name+'.c');exe=folder/(name+'.exe');c.write_text(code)
    subprocess.run(['D:/msys64/ucrt64/bin/gcc.exe','-Wall','-Wextra','-Werror',str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
