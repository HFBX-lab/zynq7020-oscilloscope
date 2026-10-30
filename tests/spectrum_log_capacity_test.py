"""The complete production FFT diagnostic must fit each logger line."""
from pathlib import Path
import re
import subprocess

root=Path(__file__).resolve().parents[1]
out=Path('D:/FPGA_Project/SP2_Spectrum_Build/log-tests')
out.mkdir(parents=True,exist_ok=True)
source=(root/'firmware/lvgl/app.c').read_text(encoding='utf-8')
for name in ('FFT_STATUS','FFT_VALUES','FFT_END'):
    match=re.search(r'board_log\(("'+name+r' [^"]+")',source)
    assert match, 'missing production '+name
    specs=re.findall(r'%(?:08|\.\d+)?([uxdf])',match.group(1))
    arguments=','.join('999999999.999999' if s=='f' else 'INT_MIN' if s=='d' else 'UINT32_MAX' for s in specs)
    code=r'''#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>
int main(void){char text[1024];int (*f)(char*,size_t,const char*,...)=snprintf;
int n=f(text,sizeof text,FORMAT,ARGS);if(n<2||n>=1024||strcmp(text+n-2,"\r\n"))return 1;
printf("NAME_LOG_PASS %d/1024\n",n);return 0;}
'''.replace('FORMAT',match.group(1)).replace('ARGS',arguments).replace('NAME',name)
    c=out/(name+'.c');exe=out/(name+'.exe');c.write_text(code,encoding='utf-8')
    subprocess.run(['D:/msys64/ucrt64/bin/gcc.exe','-std=c11','-Wall','-Wextra','-Werror',str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
