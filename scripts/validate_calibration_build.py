"""Offline M7 ARM provenance gate; this module never accesses board or source.

The generic accepted-platform validator remains the authority for hardware,
BSP, source snapshots, ELF identity and DDR bounds. This adds an explicit
engineering-profile binding from the captured DC words through float32 bytes.
"""
from pathlib import Path
import argparse
import ctypes
import hashlib
import json
import math
import re
import struct
import subprocess
import sys
import zlib

ROOT=Path(__file__).resolve().parents[1]
DEFAULT_PROFILE=Path('D:/FPGA_Project/SP2_Calibration_Build/calibration-profile-candidate.json')
DEFAULT_OUTPUT=Path('D:/FPGA_Project/SP2_Calibration_Build/build-proof')
UNITS=frozenset(('app.c','scope_ui.c','scope_calibration.c','scope_calibration_profile.c',
    'scope_record.c','scope_measurement.c','scope_cursor.c','scope_spectrum.c','scope_autoset.c'))

def load(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def finite(value):
    return isinstance(value,(int,float)) and not isinstance(value,bool) and math.isfinite(value)

def canonical(binding):
    return json.dumps(binding,ensure_ascii=True,sort_keys=True,separators=(',',':')).encode()

def check_manifest(manifest):
    assert manifest['flags']==0x103,'both engineering channels must be enabled'
    binding=manifest['binding'];data=canonical(binding)
    assert manifest['id']==zlib.crc32(data),'calibration id does not bind the manifest'
    assert manifest['binding_sha256']==hashlib.sha256(data).hexdigest(),'binding SHA256 mismatch'
    assert binding['schema']==1
    assert binding['reference']=='SDG2122X-CH2-generator-setpoints-no-independent-voltage-reference'
    channels=binding['channels']
    assert [c['channel'] for c in channels]==[1,2],'two ordered unique channels required'
    references=[]
    for c in channels:
        channel=c['channel'];run=Path(c['run'])
        assert sha(run/'session.json')==c['session_sha256'],'session SHA256 mismatch'
        assert sha(run/'characterization-analysis.json')==c['analysis_sha256'],'analysis SHA256 mismatch'
        session=load(run/'session.json');analysis=load(run/'characterization-analysis.json')
        assert session['state']==f'verified-in{channel}-characterization-captured-CRC-uncalibrated'
        assert session['source']['adc_input']==analysis['adc_input']==channel
        assert analysis['characterization_raw_records']==77 and analysis['verified_raw_records']==78
        assert analysis['verified_source_points']==33 and analysis['verified_frames']==4
        assert analysis['source']['reference_policy']=='generator-setpoints'
        assert analysis['source']['module_input_voltage_independently_measured'] is False
        recorded={r['name']:r for r in session['long_records']}
        assert len(recorded)==77
        dc=[]
        for r in analysis['records']:
            if r['point']['waveform']!='DC':continue
            name=r['name'];capture=recorded[name]
            assert capture['point']==r['point'],'analysis point differs from capture'
            assert int(capture['rate'])==50000000
            words=(run/(name+'.bin')).read_bytes()
            assert len(words)==16384
            actual_sha=hashlib.sha256(words).hexdigest()
            assert actual_sha==capture['sha256'].lower()==r['sha256'].lower()
            assert f'{zlib.crc32(words):08x}'==capture['crc32'].lower()==r['crc32'].lower()
            raw=struct.unpack('<4096I',words)
            assert not any(w&0xf800f800 for w in raw)
            shift=16*(channel-1)
            codes=[(w>>shift)&1023 for w in raw]
            assert not any(code in (0,1023) for code in codes)
            assert not any(w&(1<<(shift+10)) for w in raw)
            mean=math.fsum(codes)/4096
            assert mean==r['mean_code'],'stored mean does not match raw'
            volts=r['point']['offset_v'];assert finite(volts) and -2<=volts<=2
            dc.append(dict(name=name,setpoint_v=volts,mean_code=mean,
                training=r['point']['training'],sha256=actual_sha))
        train=[r for r in dc if r['training'] is True]
        hold=[r for r in dc if r['training'] is False]
        assert len(dc)==33 and len(train)==21 and len(hold)==12
        xs=[r['setpoint_v'] for r in train];ys=[r['mean_code'] for r in train]
        mean_x=math.fsum(xs)/len(xs);mean_y=math.fsum(ys)/len(ys)
        denom=math.fsum((x-mean_x)**2 for x in xs);assert denom>0
        forward=math.fsum((x-mean_x)*(y-mean_y) for x,y in zip(xs,ys))/denom
        zero=mean_y-forward*mean_x
        slope,offset=1/forward,-zero/forward
        fit=analysis['calibration']
        assert math.isclose(forward,fit['codes_per_set_volt'],rel_tol=1e-12,abs_tol=1e-12)
        assert math.isclose(zero,fit['zero_code'],rel_tol=1e-12,abs_tol=1e-12)
        assert struct.pack('<2f',slope,offset)==struct.pack('<2f',fit['volts_per_code'],fit['intercept_v'])
        pair=struct.pack('<2f',slope,offset)
        assert pair.hex()==c['float32_le_hex'],'DC fit float32 bytes differ from manifest'
        assert all(finite(c[k]) for k in ('slope_v_per_code','offset_v'))
        assert pair==struct.pack('<2f',c['slope_v_per_code'],c['offset_v'])
        # Manifest numeric values themselves must be the promoted float32s.
        scale,bias=struct.unpack('<2f',pair)
        assert (scale,bias)==(c['slope_v_per_code'],c['offset_v'])
        assert .005<=scale<=.015 and 256<=-bias/scale<=768
        errors=[r['mean_code']*scale+bias-r['setpoint_v'] for r in hold]
        assert errors==c['holdout_errors_float32_v']
        worst=max(map(abs,errors));assert worst<.010 and worst==c['holdout_max_float32_error_v']
        references.append(dict(channel=channel,run=str(run),session_sha256=c['session_sha256'],
            analysis_sha256=c['analysis_sha256'],float32_le_hex=pair.hex(),
            holdout_max_float32_error_v=worst,dc_records=dc))
    return references

def check_source(manifest,source):
    assert manifest['binding_sha256'] in source,'source binding SHA missing'
    assert '#ifdef SCOPE_ENGINEERING_CALIBRATION' in source,'explicit profile guard missing'
    assert re.search(r'#else\s+static const ScopeCalibration profile=\{0\};',source)
    flags=re.findall(r'\.flags\s*=\s*UINT32_C\((0x[0-9a-fA-F]+|[0-9]+)\)',source)
    ids=re.findall(r'\.id\s*=\s*UINT32_C\((0x[0-9a-fA-F]+|[0-9]+)\)',source)
    assert len(flags)==len(ids)==1
    assert int(flags[0],0)==manifest['flags'] and int(ids[0],0)==manifest['id']
    arrays=[]
    for field in ('slope_v_per_code','offset_v'):
        matches=re.findall(r'\.'+field+r'\s*=\s*\{([^}]+)\}',source)
        assert len(matches)==1
        tokens=[x.strip() for x in matches[0].split(',')]
        assert len(tokens)==2
        assert all(re.fullmatch(r'-?\d+(?:\.\d*)?(?:[eE][+-]?\d+)?f',t) for t in tokens)
        values=[float(t[:-1]) for t in tokens];assert all(map(math.isfinite,values))
        arrays.append(values)
    channels=manifest['binding']['channels']
    expected=[channels[0]['slope_v_per_code'],channels[1]['slope_v_per_code'],
              channels[0]['offset_v'],channels[1]['offset_v']]
    actual=arrays[0]+arrays[1]
    assert struct.pack('<4f',*actual)==struct.pack('<4f',*expected),'profile C coefficients mismatch'
    return struct.pack('<II4f',manifest['flags'],manifest['id'],*expected)

def check_elf(data,profile_bytes):
    assert data[:6]==b'\x7fELF\x01\x01','not little-endian ELF32'
    assert profile_bytes in data,'active profile bytes absent from ELF'
    for symbol in ('scope_calibration_default_profile','scope_calibration_trigger_q8',
        'scope_measurement_analyze_calibrated','scope_cursor_measure_calibrated',
        'scope_spectrum_analyze_calibrated','scope_autoset_plan_calibrated',
        'scope_ui_memory_load','scope_record_encode','scope_record_decode',
        'CALIBRATION_STATUS ','CALIBRATION_COEFFICIENTS ','CALIBRATION_END '):
        assert symbol.encode() in data,('required calibration implementation absent',symbol)

def windows_argv(text):
    """Use Windows' native quoting/backslash rules, never POSIX shlex rules."""
    assert isinstance(text,str) and text and not any(c in text for c in '\x00\r\n')
    assert sys.platform=='win32','this Windows build proof requires Windows argument parsing'
    shell=ctypes.WinDLL('shell32',use_last_error=True)
    parse=shell.CommandLineToArgvW
    parse.argtypes=[ctypes.c_wchar_p,ctypes.POINTER(ctypes.c_int)]
    parse.restype=ctypes.POINTER(ctypes.c_wchar_p)
    free=ctypes.WinDLL('kernel32',use_last_error=True).LocalFree
    free.argtypes=[ctypes.c_void_p];free.restype=ctypes.c_void_p
    count=ctypes.c_int()
    native=parse(text,ctypes.byref(count))
    if not native:raise ctypes.WinError(ctypes.get_last_error())
    try:
        assert 0<count.value<=4096
        return [native[i] for i in range(count.value)]
    finally:
        free(ctypes.cast(native,ctypes.c_void_p))

def command_path(value,directory):
    path=Path(value)
    # A drive-relative path has per-drive current-directory semantics; no
    # current CMake command uses it, so do not guess that external state.
    assert path.is_absolute() or not path.drive,'drive-relative compiler path'
    return (path if path.is_absolute() else directory/path).resolve()

def check_compiler_argv(argv,expected,directory):
    macro='SCOPE_ENGINEERING_CALIBRATION'
    # GCC expands response files before interpreting options, so this must
    # cover option values too. Reject shell metacharacters before any skips.
    assert not any(arg.startswith('@') for arg in argv),'response files are not audited compiler arguments'
    assert not any(re.search(r'[&|<>]',arg) or arg in (';','(',')') for arg in argv),'shell operator in compiler argv'
    assert argv and Path(argv[0]).name.lower()=='arm-none-eabi-gcc.exe'
    assert '-mcpu=cortex-a9' in argv
    assert argv.count('-c')==1,'exactly one compile input switch required'
    source_index=argv.index('-c')+1
    assert source_index<len(argv) and not argv[source_index].startswith('-'),'compile input missing'
    assert command_path(argv[source_index],directory)==expected.resolve(),'actual -c source differs from snapshot'
    definitions=[];i=1
    while i<len(argv):
        argument=argv[i]
        if i==source_index or argument=='-c':i+=1;continue
        if argument in ('-o','-I'):
            assert i+1<len(argv) and not argv[i+1].startswith('-'),'missing compiler option value'
            i+=2;continue
        if argument in ('-D','-U'):
            assert i+1<len(argv) and not argv[i+1].startswith('-'),'missing macro argument'
            option,value=argument,argv[i+1];i+=2
        elif argument.startswith(('-D','-U')):
            option,value=argument[:2],argument[2:];i+=1
        else:
            # Extra positional inputs, response files and shell operators
            # could compile something not represented by JSON's file field.
            assert argument.startswith('-') and argument!='--',('extra compiler input/command',argument)
            assert macro not in argument,'unparsed profile override option'
            i+=1;continue
        name,separator,definition=value.partition('=')
        if name!=macro:continue
        assert option!='-U','engineering profile undefined in actual argv'
        assert separator,'engineering profile requires explicit value'
        definitions.append(definition)
    assert definitions==['1'],'engineering profile macro is absent/ambiguous/disabled'

def check_compile_commands(commands,run):
    seen={name:[] for name in UNITS}
    for item in commands:
        name=Path(item['file']).name
        if name not in seen:continue
        seen[name].append(item)
        expected=run/('board' if name=='app.c' else 'ui')/name
        assert Path(item['file']).resolve()==expected.resolve(),('unexpected source unit',name)
        assert Path(item['directory']).resolve()==(run/'cmake').resolve(),('unexpected compile directory',name)
        check_compiler_argv(windows_argv(item['command']),expected,run/'cmake')
    assert all(len(rows)==1 for rows in seen.values()),'required translation unit missing/duplicated'

def check_compile_log(text):
    assert text.strip() and 'Linking C executable scope_lvgl_freertos.elf' in text,'ARM build log incomplete'
    assert not re.search(r'(?im)^.*?\b(?:warning|error):|^CMake (?:Warning|Error)',text),'ARM diagnostic warning/error'

def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware-directory',default='build/calibration-ui-firmware/freertos')
    parser.add_argument('--hardware-directory',default='build/highspeed-50m-soc')
    parser.add_argument('--rtl-directory',default='build/highspeed-50m-rtl')
    parser.add_argument('--placement-file',default='D:/FPGA_Project/SP2_Highspeed_Build/50m/io-audit/capture-placements.csv')
    parser.add_argument('--profile',type=Path,default=DEFAULT_PROFILE)
    parser.add_argument('--output-directory',type=Path,default=DEFAULT_OUTPUT)
    args=parser.parse_args(argv)
    # Never provide a flag to bypass the accepted hardware/source provenance gate.
    subprocess.run([sys.executable,'-B',str(ROOT/'scripts/validate_acq_ui_build.py'),
        '--firmware-directory',args.firmware_directory,'--hardware-directory',args.hardware_directory,
        '--rtl-directory',args.rtl_directory,'--placement-file',args.placement_file],cwd=ROOT,check=True)
    fw=ROOT/args.firmware_directory;status=load(fw/'build-status.json');run=Path(status['run'])
    assert status['state']=='succeeded' and status['os']=='freertos'
    assert status['engineering_calibration'] is True,'engineering build switch was not enabled'
    staged={Path(x['Path']).relative_to(run).as_posix() for x in status['source_hashes']}
    required={f'ui/{name}' for name in UNITS if name!='app.c'}
    required|={p[:-2]+'.h' for p in required};required.add('board/app.c')
    assert required<=staged,('missing staged calibration sources',required-staged)
    check_compile_log((run/'compile.log').read_text(encoding='utf-8-sig'))
    check_compile_commands(load(run/'cmake/compile_commands.json'),run)
    manifest=load(args.profile);datasets=check_manifest(manifest)
    expected=check_source(manifest,(run/'ui/scope_calibration_profile.c').read_text())
    check_elf((fw/'scope_lvgl_freertos.elf').read_bytes(),expected)
    proof=load(fw/'validation.json')
    assert proof['state']=='verified-offline' and proof['elf_sha256']==sha(fw/'scope_lvgl_freertos.elf').upper()
    extra=dict(engineering_calibration=True,calibration_id=f"{manifest['id']:08x}",
        calibration_flags=manifest['flags'],profile_manifest=str(args.profile),
        profile_manifest_sha256=sha(args.profile).upper(),profile_binding_sha256=manifest['binding_sha256'],
        calibration_reference=manifest['binding']['reference'],calibration_datasets=datasets,
        calibration_profile_bytes_le=expected.hex(),calibration_compile_log_sha256=sha(run/'compile.log').upper(),
        calibration_compile_commands_sha256=sha(run/'cmake/compile_commands.json').upper(),
        calibration_validator_sha256=sha(Path(__file__)).upper(),
        calibration_base_validator_sha256=sha(ROOT/'scripts/validate_acq_ui_build.py').upper(),
        calibration_profile_source_sha256=sha(run/'ui/scope_calibration_profile.c').upper(),
        calibration_build_status_sha256=sha(fw/'build-status.json').upper(),
        calibration_board_state='not-assessed-by-offline-audit')
    proof.update(extra)
    args.output_directory.mkdir(parents=True,exist_ok=True)
    payload=json.dumps(proof,indent=2)+'\n'
    (args.output_directory/(run.name+'-validation.json')).write_text(payload)
    (fw/'validation.json').write_text(payload)
    print('CALIBRATION_BUILD_PROOF_PASS',run.name,'profile',extra['calibration_id'],'no-hardware-access')
    return 0

if __name__=='__main__':
    raise SystemExit(main())
