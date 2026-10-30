"""Offline provenance/timing audit for the external ADC + touch platform."""
from pathlib import Path
import argparse,hashlib,json,re,struct,zipfile
import xml.etree.ElementTree as ET
root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument('--firmware-directory',default='build/acq-ui-firmware/freertos')
parser.add_argument('--hardware-directory',default='build/adc-live-soc')
parser.add_argument('--rtl-directory',default='build/adc-live-test')
parser.add_argument('--placement-file',default='D:/FPGA_Project/SP2_ADC_Live_Build/io-audit/capture-placements.csv')
args=parser.parse_args()
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest().upper()
def art(state,folder,name):
    entries=[a for a in state['artifacts'] if a['file']==name]
    assert len(entries)==1 and sha(folder/name)==entries[0]['sha256'],name
    return entries[0]['sha256']
hw=root/args.hardware_directory;fw=root/args.firmware_directory
highspeed_10m=hw.name=='highspeed-10m-soc'
highspeed_25m=hw.name=='highspeed-25m-soc'
highspeed_50m=hw.name=='highspeed-50m-soc'
highspeed=highspeed_10m or highspeed_25m or highspeed_50m
hardware_trigger=hw.name in ('hardware-trigger-soc','highspeed-10m-soc','highspeed-25m-soc','highspeed-50m-soc')
adaptive=hw.name in ('long-timebase-soc','hardware-trigger-soc','highspeed-10m-soc','highspeed-25m-soc','highspeed-50m-soc')
hs=read(hw/'build-status.json');fs=read(fw/'build-status.json');ts=read(root/args.rtl_directory/'status.json')
if 'bsp_reuse' in fs:
    reuse=fs['bsp_reuse'];accepted=Path(reuse['status_file'])
    assert reuse['state']=='verified-same-XSA-and-BSP-config' and sha(accepted)==reuse['status_sha256']
    prior=read(accepted)
    assert prior['state']=='succeeded' and prior['os']==fs['os']=='freertos' and prior['xsa_sha256']==fs['xsa_sha256']
    assert sha(root/'firmware/lvgl/bsp.tcl')==reuse['configuration_sha256']
    assert reuse['dependencies']
    for dep in reuse['dependencies']:assert sha(Path(dep['Path']))==dep['Hash'],dep['Path']
assert hs['state']==fs['state']==ts['state']=='succeeded'
assert ts['rtl']==ts['netlist']=='passed'
hr=Path(hs['run']);tr=Path(ts['run'])
test_sources={Path(a['Path']).relative_to(tr/'source').as_posix():a['Hash'] for a in ts['sources']}
for a in hs['source_hashes']:
    p=Path(a['Path']);assert sha(p)==a['Hash'];rel=p.relative_to(hr/'hardware')
    current=root/'hardware'/rel
    if adaptive and p.suffix in ('.ps1','.sv'):continue # Not compiled by the SoC build.
    if sha(current)!=a['Hash']:
        # This script is NOT executed by Vivado's SoC build. Its filename fix
        # was independently rerun and is pinned by the passing test snapshot.
        assert rel.as_posix()=='adc_live/run-sim.ps1'
        assert sha(current)==test_sources['run-sim.ps1']
for a in ts['sources']:
    p=Path(a['Path']);assert sha(p)==a['Hash'];rel=p.relative_to(tr/'source')
    if adaptive and rel.as_posix() not in {'adc_snapshot_core.v','adc_cic_decimator.v','adc_trigger_ring_core.v','adc_live_axi.v','sim/tb_adc_live_axi.sv','synth.tcl','run.tcl'}:continue
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
placement=Path(args.placement_file)
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
if any(name in fw.as_posix() for name in ('measurement-ui-firmware','autoset-ui-firmware','memory-ui-firmware','settings-ui-firmware')):
    staged={Path(item['Path']).relative_to(fr).as_posix() for item in fs['source_hashes']}
    assert {'ui/scope_measurement.c','ui/scope_measurement.h'} <= staged
    assert b'scope_measurement_analyze' in data and b'MEASURE_CHANNEL ' in data and b'MEASURE_END ' in data
    assert b'scope_ui_show_measurements' in data and b'scope_ui_measurement_frequency' in data
if any(name in fw.as_posix() for name in ('autoset-ui-firmware','memory-ui-firmware','settings-ui-firmware')):
    assert {'ui/scope_autoset.c','ui/scope_autoset.h'} <= staged
    assert b'scope_autoset_plan' in data and b'scope_ui_autoset_start' in data
    assert b'AUTOSET_STATUS ' in data and b'AUTOSET_COMMAND ' in data
if any(name in fw.as_posix() for name in ('memory-ui-firmware','settings-ui-firmware')):
    assert {'ui/scope_record.c','ui/scope_record.h'} <= staged
    assert b'scope_record_encode' in data and b'scope_record_decode' in data
    assert b'scope_ui_memory_load' in data and b'scope_ui_memory_return_live' in data
    assert b'MEMORY_STATUS ' in data and b'MEMORY_VIEW ' in data
if 'settings-ui-firmware' in fw.as_posix():
    assert {'ui/scope_settings.c','ui/scope_settings.h'} <= staged
    assert b'scope_settings_encode' in data and b'scope_settings_decode' in data
    assert b'scope_ui_settings_load' in data and b'scope_ui_settings_defaults' in data
    assert b'PARAM_STATUS ' in data and b'PARAM_CONFIG ' in data and b'PARAM_END ' in data
if 'settings-ui-firmware-spectrum' in fw.as_posix():
    assert {'ui/scope_spectrum.c','ui/scope_spectrum.h'} <= staged
    assert b'scope_spectrum_analyze' in data and b'scope_ui_spectrum_update' in data
    assert b'FFT_STATUS ' in data and b'FFT_VALUES ' in data and b'FFT_END ' in data
assert data[:6]==b'\x7fELF\x01\x01' and struct.unpack_from('<H',data,18)[0]==40
entry,offset=struct.unpack_from('<II',data,24);size,count=struct.unpack_from('<HH',data,42)
assert entry==0x100000;loads=[]
for i in range(count):
    kind,off,virt,phys,fsz,msz,flags,align=struct.unpack_from('<8I',data,offset+i*size)
    if kind==1:
        assert virt==phys and 0x100000<=virt<virt+msz<=0x1000000
        loads.append([hex(virt),hex(virt+msz)])
assert loads and b'ADC_LIVE_READY' in data and b'EXTERNAL BANK33/J6' in data
assert b'SINGLE_COMMAND' in data and b'ADC_ACQ_REARM' in data and b'display_crc=' in data and b'single_complete=' in data
assert b'scope_trigger_find' in data
if 'dual-ui-firmware' in fw.as_posix():
    assert b'scope_ui_set_dual_display' in data and b'dual_visible=' in data
if any(name in fw.as_posix() for name in ('cursor-ui-firmware','professional-ui-firmware','channel-ui-firmware','trigger-source-ui-firmware')):
    assert b'scope_cursor_measure' in data and b'cursor_dt_us=' in data and b'dual_visible=' in data
if 'channel-ui-firmware' in fw.as_posix():
    assert b'scope_ui_configure_channel' in data and b'ch1_scale=' in data and b'ch2_pos=' in data
if 'trigger-source-ui-firmware' in fw.as_posix():
    assert b'scope_ui_select_trigger_source' in data and b'trig_channel=' in data
if adaptive:
    assert b'scope_ui_set_adc_record_info' in data and b'scope_acquisition_plan' in data
    assert (b'ABI=00070000' if highspeed_50m else b'ABI=00060000' if highspeed_25m else b'ABI=00050000' if highspeed_10m else b'ABI=00040000' if hardware_trigger else b'ABI=00030000') in data and b'display_rate=' in data
    if hardware_trigger:assert b'scope_ui_set_hardware_adc_record' in data and b'hw_progress=' in data
inc=(Path(fs['bsp_include'])/'xparameters.h').read_text().lower()
assert '0x43c10000' in inc and '0x41210000' in inc
result=dict(state='verified-offline',board='not-assessed-by-offline-audit',bit_sha256=bit,xsa_sha256=xsa,elf_sha256=elf,
    ps7_init_sha256=sha(hw/'ps7_init.tcl'),board_init_helpers_sha256=sha(root/'firmware/board_init_helpers.tcl'),
    load_segments=loads,conversion_hz=5000000,record_hz=625000,input_slacks_ns=input_slacks,
    input_delay_budget='candidate 0..70ns; not a complete PVT specification',iob_count=22,placement_sha256=sha(placement))
if adaptive:result.update(supported_record_hz=([50000000,6250000] if highspeed_50m else [25000000,6250000] if highspeed_25m else [10000000] if highspeed_10m else [])+[625000,62500,6250,625],abi='00070000' if highspeed_50m else '00060000' if highspeed_25m else '00050000' if highspeed_10m else '00040000' if hardware_trigger else '00030000',filter_kind=3)
if highspeed:
    conversion=50000000 if highspeed_50m else 25000000 if highspeed_25m else 10000000
    period=1e9/conversion
    drive_phase=20.0*195/360 if highspeed_50m else 35.0 if highspeed_25m else 12.5
    assert params(new['adc_capture'])['CONVERSION_HZ']==str(conversion)
    clockfile=placement.parent/'sampling-clocks.csv'
    clocks={p[0]:p for p in (line.split(',') for line in clockfile.read_text().splitlines())}
    assert set(clocks)=={'capture','drive'}
    assert all(abs(float(v[2])-period)<.001 for v in clocks.values())
    assert abs(float(clocks['drive'][3])-float(clocks['capture'][3])-drive_phase)<.001
    for kind in ['setup','hold']:
        text=(hw/f'adc_input_{kind}.rpt').read_text()
        assert re.search(r'User Uncertainty[^\n]*1\.000ns',text),kind
    result.update(conversion_hz=conversion,raw_filter_kind=0,sampling_clock_audit_sha256=sha(clockfile),drive_phase_ns=drive_phase)
    if highspeed_25m or highspeed_50m:
        requirements_expected=[('setup',29.167),('hold',9.167)] if highspeed_50m else [('setup',45.0),('hold',5.0)]
        for kind,required in requirements_expected:
            text=(hw/f'adc_input_{kind}.rpt').read_text()
            requirements=[float(v) for v in re.findall(r'Requirement:\s+(-?[\d.]+)ns',text)]
            assert len(requirements)==22 and all(abs(v-required)<.001 for v in requirements),(kind,requirements)
        result.update(input_delay_budget='EXPERIMENTAL 15..35ns assumption around25ns typical, not measured min/max or PVT guarantee',input_latency_cycles=2,phase_trials='required-before-board-stage-acceptance')
        if highspeed_50m:
            pulse=(hw/'pulse_width_violations.rpt').read_text()
            assert 'Pulse Width Report' in pulse and not re.search(r'slack|violated',pulse,re.I)
            result.update(input_delay_budget='POST-EYE10..20ns effective period-normalized phase hypothesis; not absolute ADCtOD min/max,notPVT; old20..30ns model rejected by data',internal_pulse_width='no-violations',external_ADC_pulse_width='not-measured;minimum9.5ns requires physical verification')
(fw/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
print('PASS: acquisition UI ELF/source/XSA, matched ADC hardware, protected DDR bounds, trigger/single markers')
