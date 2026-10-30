"""Compile actual calibration diagnostic formats at worst field sizes (offline)."""
from pathlib import Path
import re,subprocess
root=Path(__file__).resolve().parents[1]
out=Path('D:/FPGA_Project/SP2_Calibration_Build/log-tests');out.mkdir(exist_ok=True,parents=True)
source=(root/'firmware/lvgl/app.c').read_text(encoding='utf-8')
lengths=[]
for name,multiplicity in [('CALIBRATION_STATUS',1),('CALIBRATION_COEFFICIENTS',3),('CALIBRATION_END',1)]:
    match=re.search(r'board_log\(("'+name+r' [^"]+")',source)
    assert match,'missing production '+name
    specs=re.findall(r'%(?:08)?([uxs])',match.group(1))
    args=','.join('"instrument"' if s=='s' else 'UINT32_MAX' for s in specs)
    code='''#include <stdio.h>
#include <stdint.h>
#include <string.h>
int main(void){char b[1024];int n=snprintf(b,sizeof b,FORMAT,ARGS);
if(n<2||n>=1024||strcmp(b+n-2,"\\r\\n"))return 1;
printf("%d",n);return 0;}
'''.replace('FORMAT',match.group(1)).replace('ARGS',args)
    c=out/(name+'.c');exe=out/(name+'.exe');c.write_text(code)
    subprocess.run(['D:/msys64/ucrt64/bin/gcc.exe','-std=c11','-Wall','-Wextra','-Werror',str(c),'-o',str(exe)],check=True)
    length=int(subprocess.check_output([str(exe)]));lengths.append(length*multiplicity)
    print(name,length,'/1024')
assert sum(lengths)<1024
assert 'if(!strcmp(command,"calstatus"))' in source
print('CALIBRATION_LOG_PASS',sum(lengths),'/1024 total; no output mutation')
