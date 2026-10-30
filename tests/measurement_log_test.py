"""Compile the production measurement format with worst supported values."""
from pathlib import Path
import re
import subprocess

root=Path(__file__).resolve().parents[1]
folder=Path('D:/FPGA_Project/SP2_Timing_Build/log-test')
folder.mkdir(parents=True,exist_ok=True)
source=(root/'firmware/lvgl/app.c').read_text(encoding='utf-8')
match=re.search(r'board_log\(("MEASURE_CHANNEL [^"]+")',source)
assert match, 'production measurement diagnostic does not exist'
body=r'''
#include <stdio.h>
#include <string.h>
int main(void){
 char text[1024];
 int size=snprintf(text,sizeof text,FORMAT,2u,1u,1u,-5.,5.,5.,10.,5.,5.,1u,12500000.,5000000000.);
 if(size<2 || size>=1024 || strcmp(text+size-2,"\r\n"))return 1;
 printf("MEASUREMENT_LOG_PASS: %d/1024 bytes\n",size);
 return 0;
}
'''.replace('FORMAT',match.group(1))
(folder/'log.c').write_text(body)
subprocess.run(['D:/msys64/ucrt64/bin/gcc.exe','-std=c11','-Wall','-Wextra','-Werror',str(folder/'log.c'),'-o',str(folder/'log.exe')],check=True)
subprocess.run([str(folder/'log.exe')],check=True)

# Extract the production literal, so a future field addition tests its actual
# length rather than an independent imitation of the UART line.
for name in ('MEASURE_STATUS', 'MEASURE_TIMING'):
    match=re.search(r'board_log\(("'+name+r' [^"]+")',source)
    assert match, 'production timing diagnostic missing: '+name
    if name=='MEASURE_STATUS':
        assert ' timing=%u' in match.group(1)
    else:
        fields=re.findall(r'(\w+)=%',match.group(1))
        assert fields==['ch','pulse_valid','rise_valid','fall_valid','cycles',
                        'duty_percent','positive_us','negative_us','period_us',
                        'rise_us','fall_us','low_v','mid_v','high_v']
    specs=re.findall(r'%(?:\.\d+)?([uf])',match.group(1))
    args=','.join('UINT32_MAX' if spec=='u' else '-4095000000.123456' for spec in specs)
    code=r'''#include <stdint.h>
#include <stdio.h>
#include <string.h>
int main(void){char b[1024];int n=snprintf(b,sizeof b,FORMAT,ARGS);
if(n<2||n>=1024||strcmp(b+n-2,"\r\n"))return 1;
printf("NAME_LOG_PASS: %d/1024 bytes\n",n);return 0;}
'''.replace('FORMAT',match.group(1)).replace('ARGS',args).replace('NAME',name)
    c=folder/(name+'.c');exe=folder/(name+'.exe');c.write_text(code)
    subprocess.run(['D:/msys64/ucrt64/bin/gcc.exe','-std=c11','-Wall','-Wextra','-Werror',str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
