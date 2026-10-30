"""Offline TF firmware gate: audited PS-only platform plus original PL,
frozen source/BSP, actual compiler arguments, active M7 profile and ELF bounds.
Does not access hardware. The older accepted-platform gate is kept unchanged.
"""
from pathlib import Path
import argparse,hashlib,json,re,struct,subprocess,sys,zipfile
import validate_calibration_build as calibration
ROOT=Path(__file__).resolve().parents[1]
def load(p):return json.loads(Path(p).read_text(encoding='utf-8-sig'))
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest().upper()
def check_tf_macro(argv):
    found=[];i=1
    while i<len(argv):
        a=argv[i];i+=1
        if a in ('-D','-U'):
            assert i<len(argv);option,value=a,argv[i];i+=1
        elif a.startswith(('-D','-U')):option,value=a[:2],a[2:]
        else:continue
        name,separator,value=value.partition('=')
        if name=='SCOPE_TF_STORAGE':
            assert option=='-D' and separator;found.append(value)
    assert found==['1'],'TF macro absent, overridden, or disabled'
def elf_ranges(data):
    assert len(data)>=52 and data[:6]==b'\x7fELF\x01\x01'
    assert struct.unpack_from('<H',data,18)[0]==40
    entry,offset=struct.unpack_from('<II',data,24);size,count=struct.unpack_from('<HH',data,42)
    assert entry==0x100000 and size==32 and 0<count<=32 and offset+size*count<=len(data)
    ranges=[]
    for i in range(count):
        kind,off,virt,phys,fsz,msz,flags,align=struct.unpack_from('<8I',data,offset+i*size)
        if kind!=1:continue
        assert virt==phys and 0x100000<=virt<virt+msz<=0x1000000
        assert fsz<=msz and off+fsz<=len(data)
        ranges.append([hex(virt),hex(virt+msz)])
    assert ranges
    return ranges
def check_artifacts(status,fw,run):
    for name in ('scope_lvgl_freertos.elf','scope_lvgl_freertos.map'):
        rows=[a for a in status['artifacts'] if a['file']==name]
        assert len(rows)==1,('missing or duplicate build artifact',name)
        expected=rows[0]['sha256']
        assert isinstance(expected,str) and re.fullmatch('[0-9A-F]{64}',expected)
        assert sha(fw/name)==expected==sha(run/'cmake'/name),('build/deliverable artifact changed',name)
def check_source_inventory(status,run):
    folders=('board','touch','adc_live','ui','legacy','lvgl','storage')
    actual={p.resolve() for folder in folders for p in (run/folder).rglob('*') if p.is_file()}
    declared=[Path(item['Path']).resolve() for item in status['source_hashes']]
    assert declared and len(declared)==len(set(declared)),'empty or duplicate source manifest'
    assert set(declared)==actual,'source manifest omits or adds staged files'
    required=('board/app.c','board/board.c','board/board.h','board/portable.c','board/portable.h',
        'ui/scope_ui.c','ui/scope_ui.h','ui/scope_storage.c','ui/scope_storage.h',
        'ui/scope_storage_ui.c','ui/scope_storage_ui.h','storage/board_storage.c','storage/board_storage.h')
    assert {(run/name).resolve() for name in required}<=actual,'required storage sources missing'
def main():
    p=argparse.ArgumentParser();p.add_argument('--firmware-directory',default='build/storage-ui-firmware/freertos');args=p.parse_args()
    fw=ROOT/args.firmware_directory;status=load(fw/'build-status.json');run=Path(status['run'])
    assert status['state']=='succeeded' and status['os']=='freertos' and status['tf_storage'] is True and status['engineering_calibration'] is True
    check_artifacts(status,fw,run)
    # Re-run the complete SD/old-QSPI/original-PL comparison rather than trusting
    # a stale report. Original implementation timing remains the original bit's.
    audit=Path('D:/FPGA_Project/SP2_Storage_Build/hardware')
    subprocess.run([sys.executable,str(ROOT/'scripts/audit_storage_platform.py')],cwd=ROOT,check=True)
    hardware=load(audit/'platform-audit.json');assert hardware['state']=='verified-offline-only'
    hw=ROOT/status['hardware_directory'];xsa=sha(hw/'sp2_adc_live.xsa');bit=sha(hw/'sp2_adc_live.bit')
    assert xsa==hardware['xsa_sha256'].upper()==status['xsa_sha256']==sha(audit/'storage.xsa')
    assert bit==hardware['compatible_accepted_bit_sha256']=='BE748A2A543047688010B3FD6179CDA517EB5EDBA76A27962D62EFBCA90A3FBE'
    with zipfile.ZipFile(hw/'sp2_adc_live.xsa') as z:assert z.read('ps7_init.tcl')==(hw/'ps7_init.tcl').read_bytes()
    folders={'board':'firmware/lvgl','touch':'firmware/touch','adc_live':'firmware/adc_live','ui':'src','legacy':'firmware/standalone','lvgl':'third_party/lvgl-9.2.2','storage':'firmware/storage'}
    check_source_inventory(status,run)
    for item in status['source_hashes']:
        file=Path(item['Path']);assert sha(file)==item['Hash'];relative=file.relative_to(run);kind=relative.parts[0];tail=Path(*relative.parts[1:])
        if kind=='board' and tail.name=='lscript.ld':continue
        original=ROOT/folders[kind]/tail
        if kind=='board' and tail.name=='Xilinx.spec':original=ROOT/'firmware/Xilinx.spec'
        assert sha(original)==item['Hash'],str(original)
    for item in status['storage_bsp_hashes']:assert sha(item['Path'])==item['Hash']
    bsp=load(status['storage_bsp_status']);assert bsp['state']=='succeeded-offline-only' and bsp['xsa_sha256']==xsa
    for item in bsp['source_bsp_hashes']:assert sha(item['Path'])==item['Hash']
    assert sha(status['storage_sdps'])==sha(bsp['sdps_library'])
    inc=Path(status['bsp_include']);parameters=(inc/'xparameters.h').read_text()
    for token in ('XPAR_PS7_SD_0_BASEADDR 0xE0100000','XPAR_PS7_SD_0_HAS_CD 1','XPAR_PS7_SD_0_HAS_WP 0','XPAR_PS7_UART_1_BASEADDR 0xE0001000'):
        assert token in parameters,token
    assert '#define FILE_SYSTEM_USE_MKFS' not in parameters and '#define FILE_SYSTEM_READ_ONLY' not in parameters
    calibration.check_compile_log((run/'compile.log').read_text(encoding='utf-8-sig'))
    commands=load(run/'cmake/compile_commands.json');calibration.check_compile_commands(commands,run)
    assert {Path(item['file']).resolve() for item in commands}<={Path(item['Path']).resolve() for item in status['source_hashes']},'compiled inputs missing source hashes'
    for name,folder in (('scope_storage.c','ui'),('scope_storage_ui.c','ui'),('board_storage.c','storage'),('app.c','board')):
        rows=[item for item in commands if Path(item['file']).name==name];assert len(rows)==1
        argv=calibration.windows_argv(rows[0]['command']);calibration.check_compiler_argv(argv,run/folder/name,run/'cmake');check_tf_macro(argv)
    linkmap=(fw/'scope_lvgl_freertos.map').read_text()
    assert 'libstorage_sdps.a(xsdps_card.o)' in linkmap,'patched identity/timeout driver not linked'
    assert 'libxilffs.a(diskio.o)' not in linkmap,'vendor diskio bypasses worker write gate'
    elf=fw/'scope_lvgl_freertos.elf';data=elf.read_bytes();ranges=elf_ranges(data)
    manifest=load(calibration.DEFAULT_PROFILE);datasets=calibration.check_manifest(manifest)
    expected=calibration.check_source(manifest,(run/'ui/scope_calibration_profile.c').read_text());calibration.check_elf(data,expected)
    nm=Path('D:/Xilinx_2020.2/Vitis/2020.2/gnu/aarch32/nt/gcc-arm-none-eabi/bin/arm-none-eabi-nm.exe')
    symbols=subprocess.check_output([str(nm),'-g',str(elf)],text=True)
    names={line.split()[-1] for line in symbols.splitlines() if len(line.split())>=3}
    assert {'board_storage_submit','board_storage_poll','scope_storage_save','scope_storage_load','disk_write','get_fattime'}<=names
    assert 'f_mkfs' not in names
    for token in (b'ADC_LIVE_READY',b'ABI=00070000',b'TF_STATUS ',b'TF_BLOB ',b'MEMORY_STATUS ',b'PARAM_STATUS ',b'FFT_STATUS ',b'MEASURE_TIMING '):assert token in data,token
    result=dict(state='verified-offline',board='not-assessed-by-offline-audit',elf_sha256=sha(elf),xsa_sha256=xsa,bit_sha256=bit,
        ps7_init_sha256=sha(hw/'ps7_init.tcl'),board_init_helpers_sha256=sha(ROOT/'firmware/board_init_helpers.tcl'),
        load_segments=ranges,conversion_hz=50000000,abi='00070000',tf_storage=True,calibration_id=f"{manifest['id']:08x}",
        calibration_flags=manifest['flags'],calibration_profile_bytes_le=expected.hex(),calibration_datasets=datasets,
        platform_audit_sha256=sha(audit/'platform-audit.json'),build_status_sha256=sha(fw/'build-status.json'),
        storage_sdps_sha256=sha(status['storage_sdps']),validator_sha256=sha(Path(__file__)),
        file_time='fixed 2000-01-01 placeholder; board has no configured RTC',
        precision_boundary='M7 engineering calibration unchanged; PVT/temperature/ripple not newly assessed')
    (fw/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
    print('STORAGE_BUILD_PROOF_PASS',run.name,'profile',result['calibration_id'],'ELF',result['elf_sha256'])
if __name__=='__main__':main()
