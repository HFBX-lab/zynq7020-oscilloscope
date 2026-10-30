"""Offline portable entry points. No network, target, serial or flash access."""
from pathlib import Path
import argparse, hashlib, json, os, shutil, subprocess, sys

ROOT=Path(__file__).resolve().parents[1]

def run(command,cwd,env):
    print(' '.join(map(str,command)),flush=True)
    subprocess.run(list(map(str,command)),cwd=cwd,env=env,check=True)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('mode',choices=('host','application','fsbl','boot','verify','pl','platform'))
    p.add_argument('--output',type=Path,help='Independent output root; required except verify')
    p.add_argument('--cmake',default='cmake')
    p.add_argument('--ninja',default='ninja')
    p.add_argument('--gcc',default='gcc')
    p.add_argument('--arm-gcc',default='arm-none-eabi-gcc')
    p.add_argument('--bootgen',default='bootgen')
    p.add_argument('--vivado',default='vivado')
    p.add_argument('--jobs',type=int,default=4)
    a=p.parse_args()
    if a.mode=='verify':
        for name,item in json.loads((ROOT/'release/SHA256SUMS.json').read_text()).items():
            f=ROOT/'release'/name
            assert f.stat().st_size==item['bytes'] and hashlib.sha256(f.read_bytes()).hexdigest().upper()==item['sha256'],name
        print('Accepted release hashes: PASS');return
    if not a.output:p.error('--output is required; build products never default to the repository')
    out=a.output.resolve()
    if out==ROOT or ROOT in out.parents:p.error('Choose an output directory outside the source repository')
    if a.mode in ('application','fsbl','boot','pl','platform'):
        for path in (ROOT,out):
            if not str(path).isascii() or any(c.isspace() for c in str(path)):
                p.error('Vitis/Vivado 2020.2 requires ASCII paths without spaces; extract source and output e.g. under D:/FPGA_Competition')
    out.mkdir(parents=True,exist_ok=True)
    env=os.environ.copy()
    # Relative temporary names avoid GCC/binutils non-ASCII path disagreement.
    env.update(TEMP='.',TMP='.',TMPDIR='.')
    tool_dirs=[]
    for tool in (a.cmake,a.ninja,a.gcc,a.arm_gcc,a.bootgen,a.vivado):
        if Path(tool).is_absolute():tool_dirs.append(str(Path(tool).parent))
    env['PATH']=os.pathsep.join(tool_dirs+[env.get('PATH','')])
    if a.mode=='boot':
        for name in ('fsbl.elf','platform.bit','application.elf','boot.bif'):
            shutil.copy2(ROOT/'release'/name,out/name)
        run([a.bootgen,'-arch','zynq','-image','boot.bif','-o','BOOT.bin','-w','on'],out,env)
        print('BOOT generated offline. Compare against release/SHA256SUMS.json; this command never programs flash.');return
    if a.mode in ('pl','platform'):
        work=out/'work';work.mkdir(exist_ok=True)
        if a.mode=='pl':
            dst=out/'hardware'
            if not dst.exists():shutil.copytree(ROOT/'hardware',dst)
            script=dst/'adc_live/build.tcl'; extra=[]
        else:
            script=ROOT/'tools/rebuild_platform.tcl';extra=['-tclargs',str(ROOT),str(out)]
        run([a.vivado,'-mode','batch','-source',script,*extra],work,env)
        print('Offline hardware build ended. Regenerated artifacts require separate validation.');return
    if a.mode=='host':
        source=ROOT;defines=[f'-DCMAKE_C_COMPILER={a.gcc}','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_C_FLAGS=-pipe -O2']
    else:
        defines=['-DCMAKE_SYSTEM_NAME=Generic','-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY',f'-DCMAKE_C_COMPILER={a.arm_gcc}']
        if a.mode=='fsbl':source=ROOT/'repro/fsbl'
        else:
            app=ROOT/'repro/application';source=app/'board'
            values={'LVGL_ROOT':ROOT/'third_party/lvgl-9.2.2','UI_ROOT':app/'ui','LEGACY_ROOT':app/'legacy',
                    'BSP_INCLUDE':app/'bsp/include','BSP_LIB':app/'bsp/lib','SCOPE_TOUCH_ROOT':app/'touch',
                    'SCOPE_ADC_ROOT':app/'adc_live','SCOPE_STORAGE_ROOT':app/'storage',
                    'SCOPE_STORAGE_SDPS_LIB':app/'bsp/lib/libstorage_sdps.a','SCOPE_OS':'freertos','SCOPE_ENGINEERING_CALIBRATION':'1'}
            defines.extend(f'-D{k}={v}' for k,v in values.items())
    build=out/a.mode
    run([a.cmake,'-S',source,'-B',build,'-G','Ninja',f'-DCMAKE_MAKE_PROGRAM={a.ninja}',*defines],out,env)
    run([a.cmake,'--build',build,'--parallel',str(a.jobs)],out,env)
    if a.mode=='host':
        ctest=Path(a.cmake).with_name('ctest.exe' if os.name=='nt' else 'ctest') if Path(a.cmake).is_absolute() else 'ctest'
        run([ctest,'--test-dir',build,'--output-on-failure'],out,env)
    print('Build complete; relocated builds may differ in debug paths and are not automatically accepted hardware releases.')

if __name__=='__main__':main()
