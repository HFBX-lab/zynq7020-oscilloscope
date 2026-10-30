"""Production parameter diagnostics must remain complete in the 1024B logger."""
from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parents[1]
folder = Path('D:/FPGA_Project/SP2_Settings_Build/log-test')
folder.mkdir(parents=True, exist_ok=True)
source = (root / 'firmware/lvgl/app.c').read_text(encoding='utf-8')
for name in ('PARAM_STATUS', 'PARAM_CONFIG', 'PARAM_END'):
    match = re.search(r'board_log\(("' + name + r' [^"]+")', source)
    assert match, 'production parameter diagnostic missing: ' + name
    specs = re.findall(r'%(?:08)?([uxd])', match.group(1))
    args = ','.join('INT_MIN' if spec == 'd' else 'UINT32_MAX' for spec in specs)
    code = r'''#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>
int main(void){char text[1024];int (*format)(char*,size_t,const char*,...)=snprintf;
int n=format(text,sizeof text,FORMAT,ARGS);
if(n<2||n>=1024||strcmp(text+n-2,"\r\n"))return 1;
printf("NAME_LOG_PASS: %d/1024 bytes\n",n);return 0;}
'''.replace('FORMAT', match.group(1)).replace('ARGS', args).replace('NAME', name)
    c_file = folder / (name + '.c')
    executable = folder / (name + '.exe')
    c_file.write_text(code, encoding='utf-8')
    subprocess.run(['D:/msys64/ucrt64/bin/gcc.exe', '-std=c11', '-Wall', '-Wextra',
                    '-Werror', str(c_file), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
