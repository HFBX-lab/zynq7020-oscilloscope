"""Offline touch platform/firmware audit. No serial/JTAG/hardware access."""
from pathlib import Path
import hashlib,json,struct,zipfile
import xml.etree.ElementTree as ET

root=Path(__file__).resolve().parents[1]
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest().upper()
def artifact(state,folder,name):
    a=[a for a in state['artifacts'] if a['file']==name]
    assert len(a)==1 and sha(folder/name)==a[0]['sha256'],name
    return a[0]['sha256']
hw=root/'build/touch-soc';hs=read(hw/'build-status.json');assert hs['state']=='succeeded'
hr=Path(hs['run'])
for item in hs['source_hashes']:
    p=Path(item['Path']);assert sha(p)==item['Hash']
    assert sha(root/'hardware'/p.relative_to(hr/'hardware'))==item['Hash'],p
bit=artifact(hs,hw,'sp2_touch.bit');xsa=artifact(hs,hw,'sp2_touch.xsa')
baseline=root/'build/ps-screen'
assert (hw/'ps7_readback.txt').read_bytes()==(baseline/'ps7_readback.txt').read_bytes(),'PS7 parameters changed'
def modules(path):
    with zipfile.ZipFile(path) as z:
        names=[n for n in z.namelist() if n.endswith('.hwh')];assert len(names)==1
        doc=ET.fromstring(z.read(names[0]))
    return {m.get('INSTANCE'):m for m in doc.findall('.//MODULE')}
old=modules(baseline/'sp2_ps_screen.xsa');new=modules(hw/'sp2_touch.xsa')
for name in ['ps7','vdma','vtc','video_out','status_gpio']:
    def params(m):return {p.get('NAME'):p.get('VALUE') for p in m.findall('./PARAMETERS/PARAMETER')}
    assert params(old[name])==params(new[name]),f'{name} parameters changed'
tg=params(new['touch_gpio']);assert tg['C_BASEADDR']=='0x41210000',tg
assert tg['C_GPIO_WIDTH']=='4' and tg['C_IS_DUAL']=='0'
assert 'adc_capture' not in new
pins=(hw/'pin_assignments.rpt').read_text()
for name,pad in [('tp_sda','AB1'),('tp_scl','U5'),('tp_int','U4'),('tp_reset_n','U6'),('lcd_reset_n','W5')]:
    rows=[l for l in pins.splitlines() if name in l];assert len(rows)==1 and pad in rows[0],rows
v5=[l.split('|') for l in pins.splitlines() if '| V5 ' in l]
assert len(v5)==1 and not v5[0][2].strip(),'NC allocated'
used={c[1].strip() for l in pins.splitlines() if len(c:=l.split('|'))>3 and c[2].strip() and c[1].strip()}
adc=read(root/'hardware/adc/pinmap.json')
assert not used.intersection(s['package_pin'] for s in adc['banks']['33']['signals']),'BANK33 AD pin conflict'
timing=(hw/'timing_summary.rpt').read_text()
assert 'All user specified timing constraints are met.' in timing
with zipfile.ZipFile(hw/'sp2_touch.xsa') as z:
    init=z.read('ps7_init.tcl');assert init
assert init==(baseline/'ps7_init.tcl').read_bytes(),'PS init changed'
assert (hw/'ps7_init.tcl').read_bytes()==init

fw=root/'build/touch-firmware/freertos';fs=read(fw/'build-status.json')
assert fs['state']=='succeeded' and fs['xsa_sha256']==xsa
fr=Path(fs['run'])
for item in fs['source_hashes']:
    p=Path(item['Path']);assert sha(p)==item['Hash']
    rel=p.relative_to(fr);kind=rel.parts[0];tail=Path(*rel.parts[1:])
    if kind=='board' and tail.name=='lscript.ld':continue
    if kind=='board':original=root/('firmware' if tail.name=='Xilinx.spec' else 'firmware/lvgl')/tail
    elif kind=='touch':original=root/'firmware/touch'/tail
    elif kind=='ui':original=root/'src'/tail
    elif kind=='legacy':original=root/'firmware/standalone'/tail
    else:original=root/'third_party/lvgl-9.2.2'/tail
    assert sha(original)==item['Hash'],original
elf=artifact(fs,fw,'scope_lvgl_freertos.elf');data=(fw/'scope_lvgl_freertos.elf').read_bytes()
assert data[:6]==b'\x7fELF\x01\x01' and struct.unpack_from('<H',data,18)[0]==40
entry,offset=struct.unpack_from('<II',data,24);size,count=struct.unpack_from('<HH',data,42)
assert entry==0x100000
loads=[]
for i in range(count):
    kind,off,virt,phys,fsz,msz,flags,align=struct.unpack_from('<8I',data,offset+i*size)
    if kind==1:
        assert phys==virt and 0x100000<=virt and virt+msz<=0x1000000
        loads.append([hex(virt),hex(virt+msz)])
assert loads and b'PANEL_I2C_AND_UART' in data and b'TOUCH_STATUS' in data
assert '0x41210000' in (Path(fs['bsp_include'])/'xparameters.h').read_text().lower()
result=dict(state='verified-offline',board='not-assessed-by-offline-audit',bit_sha256=bit,
            xsa_sha256=xsa,elf_sha256=elf,ps7_init_sha256=sha(hw/'ps7_init.tcl'),
            load_segments=loads,board_init_helpers_sha256=sha(root/'firmware/board_init_helpers.tcl'))
(hw/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
print('PASS: touch platform/ELF hashes, PS7/video unchanged, GPIO41210000, physical pins, timing, low-DDR loads')
