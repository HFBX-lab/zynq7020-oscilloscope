"""Offline provenance/timing audit for the external ADC + touch platform."""
from pathlib import Path
import hashlib,json,re,struct,zipfile
import xml.etree.ElementTree as ET
root=Path(__file__).resolve().parents[1]
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest().upper()
def art(state,folder,name):
    entries=[a for a in state['artifacts'] if a['file']==name]
    assert len(entries)==1 and sha(folder/name)==entries[0]['sha256'],name
    return entries[0]['sha256']
hw=root/'build/adc-live-soc';fw=root/'build/adc-live-firmware/freertos'
hs=read(hw/'build-status.json');fs=read(fw/'build-status.json');ts=read(root/'build/adc-live-test/status.json')
assert hs['state']==fs['state']==ts['state']=='succeeded'
assert ts['rtl']==ts['netlist']=='passed'
hr=Path(hs['run']);tr=Path(ts['run'])
test_sources={Path(a['Path']).relative_to(tr/'source').as_posix():a['Hash'] for a in ts['sources']}
for a in hs['source_hashes']:
    p=Path(a['Path']);assert sha(p)==a['Hash'];rel=p.relative_to(hr/'hardware')
    current=root/'hardware'/rel
    if sha(current)!=a['Hash']:
        # This script is NOT executed by Vivado's SoC build. Its filename fix
        # was independently rerun and is pinned by the passing test snapshot.
        assert rel.as_posix()=='adc_live/run-sim.ps1'
        assert sha(current)==test_sources['run-sim.ps1']
for a in ts['sources']:
    p=Path(a['Path']);assert sha(p)==a['Hash'];rel=p.relative_to(tr/'source')
    original=root/'hardware/adc/rtl/adc_snapshot_core.v' if rel.as_posix()=='adc_snapshot_core.v' else root/'hardware/adc_live'/rel
    assert sha(original)==a['Hash'],original
for name in ['rtl4','rtl12','netlist']:
    text=(tr/(name+'-sim.log')).read_text(encoding='utf-8-sig')
    assert 'PASS: ADC LIVE AXI' in text and not re.search(r'^Fatal:|^ERROR:',text,re.M)
bit=art(hs,hw,'sp2_adc_live.bit');xsa=art(hs,hw,'sp2_adc_live.xsa')
assert fs['xsa_sha256']==xsa
def hwh(path):
    with zipfile.ZipFile(path) as z:
        doc=ET.fromstring(z.read('screen_system.hwh'))
    return {m.get('INSTANCE'):m for m in doc.findall('.//MODULE')},doc
old,_=hwh(root/'build/touch-soc/sp2_touch.xsa');new,doc=hwh(hw/'sp2_adc_live.xsa')
def params(m):return {p.get('NAME'):p.get('VALUE') for p in m.findall('./PARAMETERS/PARAMETER')}
for name in ['ps7','vdma','vtc','video_out','status_gpio','touch_gpio']:
    assert params(old[name])==params(new[name]),name
ranges={m.get('INSTANCE'):m.attrib for m in doc.findall('.//MEMRANGE')}
for name,value in [('status_gpio','0x41200000'),('touch_gpio','0x41210000'),('vdma','0x43000000'),('vtc','0x43C00000'),('adc_capture','0x43C10000')]:
    assert ranges[name]['BASEVALUE']==value,(name,ranges[name])
assert ranges['adc_capture']['HIGHVALUE']=='0x43C1FFFF'
with zipfile.ZipFile(hw/'sp2_adc_live.xsa') as z:
    bits=[n for n in z.namelist() if n.endswith('.bit')]
    assert len(bits)==1 and z.read(bits[0])==(hw/'sp2_adc_live.bit').read_bytes()
    assert z.read('ps7_init.tcl')==(hw/'ps7_init.tcl').read_bytes()==(root/'build/ps-screen/ps7_init.tcl').read_bytes()
pins=(hw/'pin_assignments.rpt').read_text()
for s in read(root/'hardware/adc/pinmap.json')['banks']['33']['signals']:
    rows=[l.split('|') for l in pins.splitlines() if '| '+s['package_pin']+' ' in l]
    assert len(rows)==1 and rows[0][2].strip()==s['port'],s
for port,pad in [('tp_sda','AB1'),('tp_scl','U5'),('tp_int','U4'),('tp_reset_n','U6'),('lcd_reset_n','W5')]:
    rows=[l for l in pins.splitlines() if port in l];assert len(rows)==1 and pad in rows[0]
assert not next(l.split('|')[2].strip() for l in pins.splitlines() if '| V5 ' in l)
assert 'All user specified timing constraints are met.' in (hw/'timing_summary.rpt').read_text()
input_slacks={}
for kind in ['setup','hold']:
    text=(hw/f'adc_input_{kind}.rpt').read_text()
    sources=re.findall(r'  Source:\s+(ad_\S+)',text)
    slacks=[float(x) for x in re.findall(r'Slack \(MET\)\s*:\s+([\d.]+)ns',text)]
    assert len(set(sources))==len(slacks)==22 and min(slacks)>0,(kind,len(sources),slacks)
    input_slacks[kind]=min(slacks)
placement=Path('D:/FPGA_Project/SP2_ADC_Live_Build/io-audit/capture-placements.csv')
rows=[l.split(',') for l in placement.read_text().splitlines()]
assert len(rows)==22 and all(r[1].startswith('ILOGIC_') and r[2]=='ILOGICE2.IFF' and r[3]=='TRUE' for r in rows)
assert {int(re.search(r'\[(\d+)\]',r[0])[1]) for r in rows}==set(range(22))
fr=Path(fs['run'])
for a in fs['source_hashes']:
    p=Path(a['Path']);assert sha(p)==a['Hash'];rel=p.relative_to(fr);kind=rel.parts[0];tail=Path(*rel.parts[1:])
    if kind=='board' and tail.name=='lscript.ld':continue
    folders={'touch':'firmware/touch','adc_live':'firmware/adc_live','ui':'src','legacy':'firmware/standalone','lvgl':'third_party/lvgl-9.2.2'}
    original=root/('firmware' if tail.name=='Xilinx.spec' else 'firmware/lvgl')/tail if kind=='board' else root/folders[kind]/tail
    assert sha(original)==a['Hash'],original
elf=art(fs,fw,'scope_lvgl_freertos.elf');data=(fw/'scope_lvgl_freertos.elf').read_bytes()
assert data[:6]==b'\x7fELF\x01\x01' and struct.unpack_from('<H',data,18)[0]==40
entry,offset=struct.unpack_from('<II',data,24);size,count=struct.unpack_from('<HH',data,42)
assert entry==0x100000;loads=[]
for i in range(count):
    kind,off,virt,phys,fsz,msz,flags,align=struct.unpack_from('<8I',data,offset+i*size)
    if kind==1:
        assert virt==phys and 0x100000<=virt<virt+msz<=0x1000000
        loads.append([hex(virt),hex(virt+msz)])
assert loads and b'ADC_LIVE_READY' in data and b'EXTERNAL BANK33/J6' in data
inc=(Path(fs['bsp_include'])/'xparameters.h').read_text().lower()
assert '0x43c10000' in inc and '0x41210000' in inc
result=dict(state='verified-offline',board='not-assessed-by-offline-audit',bit_sha256=bit,xsa_sha256=xsa,elf_sha256=elf,
    ps7_init_sha256=sha(hw/'ps7_init.tcl'),board_init_helpers_sha256=sha(root/'firmware/board_init_helpers.tcl'),
    load_segments=loads,conversion_hz=5000000,record_hz=625000,input_slacks_ns=input_slacks,
    input_delay_budget='candidate 0..70ns; not a complete PVT specification',iob_count=22,placement_sha256=sha(placement))
(hw/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
print('PASS: external ADC bit/XSA/ELF, PS7/video/touch unchanged, BANK33 pins, 22 IOBs, input setup/hold and DDR bounds')
