"""Every board staging entry must copy all shared UI translation units/headers."""
from pathlib import Path
import re
root=Path(__file__).resolve().parents[1]
required=set(re.findall(r'\$\{UI_ROOT\}/(scope_\w+\.c)',(root/'firmware/lvgl/CMakeLists.txt').read_text()))
required|={s[:-2]+'.h' for s in required}
missing=[]
for name in ['lvgl','touch','adc_live','acq_ui']:
    text=(root/f'firmware/{name}/build.ps1').read_text(encoding='utf-8-sig')
    copied=set(re.findall(r"'(scope_\w+\.[ch])'",text))
    if required-copied:missing.append((name,sorted(required-copied)))
assert not missing,missing
print('UI_BUILD_SOURCE_LISTS_PASS: 4 board entrypoints stage all shared UI sources/headers')
