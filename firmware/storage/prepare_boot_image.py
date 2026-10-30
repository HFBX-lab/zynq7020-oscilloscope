"""Reproducible offline FSBL/BSP/BOOT build. Never accesses a target."""
from pathlib import Path
import argparse
import datetime
import hashlib
import importlib.util
import json
import os
import re
import shutil
import subprocess
import zipfile

VITIS = Path('D:/Xilinx_2020.2/Vitis/2020.2')
GCC = VITIS/'gnu/aarch32/nt/gcc-arm-none-eabi/bin/arm-none-eabi-gcc.exe'
CMAKE = Path('D:/msys64/ucrt64/bin/cmake.exe')
NINJA = Path('D:/Python/Scripts/ninja.exe')
XSA_SHA = '820B9017A5D13D679BE8CFF9E759E1433A2566DFB13BCF9E42EF011F34A6FE02'
APP_SHA = '6917E229BE48D990EB7255ADA4E5BDDD052CEBF4B6E97BABD105C69C4B9C5A4C'
BIT_SHA = 'BE748A2A543047688010B3FD6179CDA517EB5EDBA76A27962D62EFBCA90A3FBE'
CAST_BEFORE = b'while ((*addr < delay))'
CAST_AFTER = b'while ((*addr < (unsigned long)delay))'
CMAKE_TEXT = '''cmake_minimum_required(VERSION 3.20)
project(SP2StorageBootFSBL C ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
file(GLOB SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/source/*.c" "${CMAKE_SOURCE_DIR}/source/*.S")
add_executable(fsbl ${SOURCES})
target_include_directories(fsbl PRIVATE source bsp/include)
target_compile_definitions(fsbl PRIVATE FSBL_DEBUG_INFO)
target_compile_options(fsbl PRIVATE -O2 -g -Wall -Wextra -Werror -mcpu=cortex-a9 -mfpu=vfpv3 -mfloat-abi=hard -ffunction-sections -fdata-sections)
target_link_directories(fsbl PRIVATE "${CMAKE_SOURCE_DIR}/bsp/lib")
target_link_options(fsbl PRIVATE -mcpu=cortex-a9 -mfpu=vfpv3 -mfloat-abi=hard
 "-specs=${CMAKE_SOURCE_DIR}/source/Xilinx.spec" "-Wl,-T,${CMAKE_SOURCE_DIR}/source/lscript.ld"
 "-Wl,-Map,${CMAKE_BINARY_DIR}/fsbl.map" -Wl,--gc-sections)
target_link_libraries(fsbl PRIVATE -Wl,--start-group xilffs xil gcc c m -Wl,--end-group)
set_target_properties(fsbl PROPERTIES OUTPUT_NAME "fsbl" SUFFIX ".elf")
'''

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest().upper()

def save(path, value):
    Path(path).write_text(json.dumps(value, indent=2)+'\n', encoding='utf-8')

def run(command, cwd, log, env):
    result = subprocess.run([str(p) for p in command], cwd=cwd, env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    Path(log).write_bytes(result.stdout)
    if result.returncode:
        raise ValueError(f'Offline command failed ({result.returncode}); inspect {log}')

def build(args):
    project = Path(__file__).resolve().parents[2]
    source_dir = Path(__file__).resolve().parent
    root = args.root.resolve()
    root.mkdir(parents=True, exist_ok=True)
    for path, expected in ((args.xsa, XSA_SHA), (args.app, APP_SHA), (args.bit, BIT_SHA)):
        if sha(path) != expected:
            raise ValueError(f'Frozen input identity mismatch: {path}')
    base = args.resume.resolve() if args.resume else root/'runs'/datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    if root not in base.parents:
        raise ValueError('Build run must remain within its explicit D-drive output root')
    if not args.resume:
        base.mkdir(parents=True, exist_ok=False)
    elif not (base/'bsp.log').is_file() or 'M5_BOOT_SOURCE_AND_BSP_READY' not in (base/'bsp.log').read_text(errors='replace'):
        raise ValueError('Resume requires a completed offline source/BSP generation')
    status = dict(state='building-offline-only', hardware_accessed=False, flash_written=False, sd_written=False)
    save(base/'status.json', status)
    try:
        tmp = base/'tmp'
        tmp.mkdir(exist_ok=True)
        env = os.environ.copy()
        env.update(TEMP=str(tmp), TMP=str(tmp), TMPDIR=str(tmp))
        env['PATH'] = str(GCC.parent)+os.pathsep+str(CMAKE.parent)+os.pathsep+env['PATH']
        for name in ('build_boot_image.ps1', 'build_boot_image.tcl', 'prepare_boot_image.py'):
            shutil.copy2(source_dir/name, base/name)
        shutil.copy2(project/'scripts/audit_storage_boot.py', base/'audit_storage_boot.py')
        shutil.copy2(args.xsa, base/'hardware.xsa')
        if not args.resume:
            run([VITIS/'bin/xsct.bat', base/'build_boot_image.tcl', base/'hardware.xsa', base/'workspace'],
                base, base/'bsp.log', env)
        if 'M5_BOOT_SOURCE_AND_BSP_READY' not in (base/'bsp.log').read_text(errors='replace'):
            raise ValueError('Offline generation success marker absent')
        platform = base/'workspace/sp2_storage_boot'
        generated_bsp = platform/'ps7_cortexa9_0/standalone_domain/bsp/ps7_cortexa9_0'
        exported = list((platform/'export/sp2_storage_boot/hw').glob('*.xsa'))
        if len(exported) != 1 or sha(exported[0]) != XSA_SHA:
            raise ValueError('Generated/exported XSA differs from frozen storage platform')
        native = base/'native'
        native.mkdir(exist_ok=False)
        shutil.copytree(base/'workspace/sp2_storage_fsbl/src', native/'source')
        shutil.copytree(generated_bsp, native/'bsp')
        (native/'vendor-generated').mkdir()
        ps_init = native/'source/ps7_init.c'
        original = ps_init.read_bytes()
        with zipfile.ZipFile(base/'hardware.xsa') as archive:
            if original != archive.read('ps7_init.c'):
                raise ValueError('FSBL PS initialization is not exactly from frozen XSA')
        if original.count(CAST_BEFORE) != 1:
            raise ValueError('Unexpected vendor PSinit comparison; explicit cast patch not applied')
        (native/'vendor-generated/ps7_init.c').write_bytes(original)
        ps_init.write_bytes(original.replace(CAST_BEFORE, CAST_AFTER))
        save(native/'ps-init-cast.json', dict(before=CAST_BEFORE.decode(), after=CAST_AFTER.decode(),
            rationale='C already converts int delay to unsigned long; explicit cast only, no register/control change',
            original_sha256=sha(native/'vendor-generated/ps7_init.c'), patched_sha256=sha(ps_init)))
        (native/'CMakeLists.txt').write_text(CMAKE_TEXT, encoding='utf-8')
        run([CMAKE, '-S', native, '-B', native/'build', '-G', 'Ninja',
             '-DCMAKE_SYSTEM_NAME=Generic', '-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY',
             f'-DCMAKE_C_COMPILER={GCC.as_posix()}', f'-DCMAKE_ASM_COMPILER={GCC.as_posix()}',
             f'-DCMAKE_MAKE_PROGRAM={NINJA.as_posix()}'], base, native/'configure.log', env)
        run([CMAKE, '--build', native/'build', '--verbose'], base, native/'compile-final.log', env)
        if re.search(r'warning:|error:', (native/'compile-final.log').read_text(errors='replace'), re.I):
            raise ValueError('Native FSBL build emitted warning/error')
        # Ninja's command stream includes the actual link invocation as well.
        run([NINJA, '-C', native/'build', '-t', 'commands'], base, native/'actual-commands.log', env)
        image = base/'boot-image'
        image.mkdir()
        for old, name in ((native/'build/fsbl.elf', 'fsbl.elf'), (args.bit, 'platform.bit'), (args.app, 'application.elf')):
            shutil.copy2(old, image/name)
        (image/'boot.bif').write_text('// Ordinary unencrypted/unauthenticated Zynq-7000 image.\nthe_ROM_image:\n{\n [bootloader] fsbl.elf\n platform.bit\n application.elf\n}\n', encoding='ascii')
        run([VITIS/'bin/bootgen.bat', '-arch', 'zynq', '-image', 'boot.bif', '-o', 'BOOT.bin', '-w', 'on'],
            image, image/'bootgen.log', env)
        tool_paths = dict(xsct=str(VITIS/'bin/xsct.bat'), arm_gcc=str(GCC), cmake=str(CMAKE), ninja=str(NINJA),
                          bootgen=str(VITIS/'bin/bootgen.bat'))
        tracked = [p for p in base.rglob('*') if p.is_file() and (
            p.is_relative_to(native/'source') or p.is_relative_to(native/'bsp') or
            p.is_relative_to(native/'vendor-generated') or p.is_relative_to(image) or
            p in [base/'hardware.xsa', base/'bsp.log', base/'build_boot_image.ps1', base/'build_boot_image.tcl',
                  base/'prepare_boot_image.py', base/'audit_storage_boot.py', native/'CMakeLists.txt',
                  native/'ps-init-cast.json', native/'build/fsbl.elf', native/'build/fsbl.map',
                  native/'build/compile_commands.json', native/'compile-final.log', native/'configure.log',
                  native/'actual-commands.log'] or
            p.suffix in ('.mss', '.spr', '.prj') or p == exported[0])]
        manifest = dict(state='candidate-offline-only', xsa_sha256=XSA_SHA, application_sha256=APP_SHA,
                        bitstream_sha256=BIT_SHA, fsbl_sha256=sha(image/'fsbl.elf'), tools=tool_paths,
                        tool_sha256={k:sha(v) for k,v in tool_paths.items()},
                        files=[dict(path=p.relative_to(base).as_posix(), bytes=p.stat().st_size, sha256=sha(p)) for p in sorted(tracked)],
                        hardware_accessed=False, flash_written=False, sd_written=False,
                        remaining=['independent specification/quality reviews', 'root-only flash backup/program/readback', 'physical QSPI cold boot'])
        save(base/'manifest.json', manifest)
        spec = importlib.util.spec_from_file_location('audit_storage_boot', base/'audit_storage_boot.py')
        auditor = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(auditor)
        report = auditor.audit_run(base, expected_fsbl_sha=manifest['fsbl_sha256'])
        save(base/'validation.json', report)
        status.update(state='verified-offline-only', fsbl=str(image/'fsbl.elf'), fsbl_sha256=manifest['fsbl_sha256'],
                      boot=str(image/'BOOT.bin'), boot_sha256=sha(image/'BOOT.bin'), boot_bytes=(image/'BOOT.bin').stat().st_size,
                      xsa_sha256=XSA_SHA, manifest_sha256=sha(base/'manifest.json'), run=str(base))
        save(root/'latest.json', status)
        print('M5_BOOT_OFFLINE_PASS', json.dumps(status))
    except Exception as error:
        status.update(state='failed-offline-only', error=str(error))
        raise
    finally:
        save(base/'status.json', status)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--xsa', type=Path, required=True)
    parser.add_argument('--app', type=Path, required=True)
    parser.add_argument('--bit', type=Path, required=True)
    parser.add_argument('--resume', type=Path)
    build(parser.parse_args())
