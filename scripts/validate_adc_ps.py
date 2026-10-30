"""Audit the new internal-digital ADC PS candidate. Never connects to JTAG."""
from pathlib import Path
import hashlib,json,re,struct,zipfile
import xml.etree.ElementTree as ET

root=Path(__file__).resolve().parents[1]
hw=root/'build/adc-soc';fw=root/'build/adc-firmware';test=root/'build/adc-ps-test'


def read(path):return json.loads(path.read_text(encoding='utf-8-sig'))
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest().upper()
def params(module):return {p.get('NAME'):p.get('VALUE') for p in module.findall('./PARAMETERS/PARAMETER')}


hs=read(hw/'build-status.json');ss=read(fw/'build-status.json');ts=read(test/'status.json')
assert hs['state']==ss['state']==ts['state']=='succeeded'
assert ts['rtl']==ts['netlist']=='passed' and hs['board']==ts['board']=='not-run'
for base,state in [(hw,hs),(fw,ss)]:
    for item in state['artifacts']:assert sha(base/item['file'])==item['sha256']
for item in hs['source_hashes']:
    p=Path(item['Path']);relative=p.relative_to(Path(hs['run'])/'hardware')
    assert sha(p)==sha(root/'hardware'/relative)==item['Hash'],relative
for item in ts['sources']:
    p=Path(item['Path']);relative=p.relative_to(Path(ts['run'])/'source')
    original=root/'hardware/adc/rtl/adc_snapshot_core.v' if str(relative)=='adc_snapshot_core.v' else root/'hardware/adc_ps'/relative
    assert sha(p)==sha(original)==item['Hash'],original
for name in ('rtl4','rtl12','netlist'):
    log=(Path(ts['run'])/(name+'-sim.log')).read_text(encoding='utf-8-sig')
    assert 'PASS: ADC AXI' in log and not re.search(r'^Fatal:|^ERROR:',log,re.M)
    assert (test/(name+'-sim.log')).read_bytes()==(Path(ts['run'])/(name+'-sim.log')).read_bytes()

xsa=hw/'sp2_adc_internal.xsa';bit=hw/'sp2_adc_internal.bit'
assert ss['xsa_sha256']==sha(xsa) and ss['bit_sha256']==sha(bit)
with zipfile.ZipFile(root/'build/ps-screen/sp2_ps_screen.xsa') as old:
    baseline=ET.fromstring(old.read('screen_system.hwh'))
    oldmods={m.get('INSTANCE'):m for m in baseline.findall('.//MODULE')}
with zipfile.ZipFile(xsa) as z:
    bits=[n for n in z.namelist() if n.endswith('.bit')]
    assert len(bits)==1 and z.read(bits[0])==bit.read_bytes()
    hwh=ET.fromstring(z.read('screen_system.hwh'))
    modules={m.get('INSTANCE'):m for m in hwh.findall('.//MODULE')}
    for module in ('ps7','vdma','vtc'):
        before,after=params(oldmods[module]),params(modules[module])
        keys=[k for k in before if k.startswith('PCW_')] if module=='ps7' else list(before)
        # VDMA/VTC instance metadata may include expanded bus addressing; their
        # video/data-path configuration must stay identical.
        if module=='vdma':keys=['C_NUM_FSTORES','C_M_AXI_MM2S_DATA_WIDTH','C_M_AXIS_MM2S_TDATA_WIDTH','C_USE_FSYNC']
        if module=='vtc':keys=['GEN_HACTIVE_SIZE','GEN_VACTIVE_SIZE','GEN_HFRAME_SIZE','GEN_F0_VFRAME_SIZE']
        for k in keys:assert before[k]==after[k],(module,k,before[k],after.get(k))
    addresses={m.get('INSTANCE'):m.attrib for m in hwh.findall('.//MEMRANGE')}
    for name,address in [('status_gpio','0x41200000'),('vdma','0x43000000'),('vtc','0x43C00000'),('adc_capture','0x43C10000')]:
        assert addresses[name]['BASEVALUE']==address,(name,addresses.get(name))
    assert addresses['adc_capture']['HIGHVALUE']=='0x43C1FFFF'
    ap=params(modules['adc_capture'])
    assert ap['ADDR_BITS']=='12' and ap['TIMEOUT_CYCLES']=='1000000'
    for reset in ('rst_axi','rst_pixel'):
        p={v.get('NAME'):v.attrib for v in modules[reset].findall('./PORTS/PORT')}
        assert p['aux_reset_in']['SIGNAME']=='const1_dout' and p['aux_reset_in']['POLARITY']=='ACTIVE_LOW'
        assert p['dcm_locked']['SIGNAME']==('const1_dout' if reset=='rst_axi' else 'rgb_io_pixel_locked')
    # Preserve generated init for the concrete board-test candidate, never run it.
    for n in ('ps7_init.tcl','ps7_init.c','ps7_init.h'):(hw/n).write_bytes(z.read(n))

for item in ss['source_hashes']:
    p=Path(item['Path']);assert sha(p)==item['Hash']
    name=p.name
    if name=='lscript.ld':continue
    if name in ('board.c','board.h','portable.c','portable.h'):original=root/'firmware/lvgl'/name
    elif name in ('frame_source.c','frame_source.h','memory_probe.c','memory_probe.h'):original=root/'firmware/standalone'/name
    elif name=='Xilinx.spec':original=root/'firmware/Xilinx.spec'
    else:original=root/'firmware/adc'/name
    assert sha(original)==item['Hash'],original
for item in ss['bsp_hashes']:assert sha(Path(item['Path']))==item['Hash']
linker=(root/'firmware/linker_template.ld').read_text().replace('LENGTH = 0x1FF00000','LENGTH = 0x00F00000').replace('_STACK_SIZE = DEFINED(_STACK_SIZE) ? _STACK_SIZE : 0x2000;','_STACK_SIZE = 0x20000;')
assert linker.strip()==(fw/'lscript.ld').read_text().strip()
data=(fw/'adc_internal_selftest.elf').read_bytes()
assert data[:6]==b'\x7fELF\x01\x01' and struct.unpack_from('<H',data,18)[0]==40
entry,offset=struct.unpack_from('<II',data,24);size,count=struct.unpack_from('<HH',data,42)
assert entry==0x100000
segments=[]
for i in range(count):
    kind,off,virt,phys,fs,ms,flags,align=struct.unpack_from('<8I',data,offset+i*size)
    if kind==1:
        assert virt==phys and 0x100000<=virt<virt+ms<=0x1000000
        segments.append([hex(virt),hex(virt+ms)])
assert segments and b'ADC_INTERNAL_SELFTEST_PASS:' in data

host=read(Path('D:/FPGA_Project/SP2_ADC_Firmware/host/build-status.json'))
assert host['state']=='succeeded'
for item in host['source_hashes']:
    p=Path(item['Path']);original=root/'tests/adc_format_test.c' if p.name=='adc_format_test.c' else root/'firmware/adc'/p.name
    assert sha(p)==sha(original)==item['Hash']
assert sha(Path(host['run'])/'adc_format_test.exe')==host['exe_sha256']
assert 'ADC_FORMAT_PASS:' in (Path(host['run'])/'test.log').read_text(encoding='utf-8-sig')

timing=(hw/'timing_summary.rpt').read_text()
match=re.search(r'WNS\(ns\).*?\n\s*-+.*?\n\s*([-\d.]+)\s+[-\d.]+\s+\d+\s+\d+\s+([-\d.]+)',timing,re.S)
assert match,'Missing final timing summary'
wns,whs=map(float,match.groups());assert wns>=0 and whs>=0
result=dict(state='offline-candidate-ready-for-authorized-jtag',hardware_run=hs['run'],firmware_run=ss['run'],
    axi_rtl='passed-16-and-4096',axi_functional_netlist='passed-4096',host_record_protocol='passed',
    bit_sha256=sha(bit),xsa_sha256=sha(xsa),elf_sha256=sha(fw/'adc_internal_selftest.elf'),
    adc_base='0x43C10000',load_segments=segments,wns_ns=wns,whs_ns=whs,
    ps7_init_sha256=sha(hw/'ps7_init.tcl'),board_init_helpers_sha256=sha(root/'firmware/board_init_helpers.tcl'),
    board='not-assessed-by-offline-audit',adc_module='absent',real_adc_sampling='not-run',lvgl_live_record='not-implemented')
(hw/'validation.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
print('PASS: AXI simulation/netlist, host format, current source/bit/XSA/ELF hashes, baseline PS/video parameters and memory/timing checks')
print(json.dumps(result))
