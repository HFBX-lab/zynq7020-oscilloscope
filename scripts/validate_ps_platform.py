"""Validate offline PS hardware/ELF provenance. Never opens a debug connection."""
from pathlib import Path
import hashlib
import json
import re
import struct
import xml.etree.ElementTree as ET
import zipfile

root=Path(__file__).resolve().parents[1]
hardware=root/'build/ps-screen'
software=root/'build/ps-firmware'
stage=Path(r'D:\FPGA_Project\SP2_PS_Screen_Build\hardware')
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest().upper()
hs=json.loads((hardware/'build-status.json').read_text(encoding='utf-8-sig'))
ss=json.loads((software/'build-status.json').read_text(encoding='utf-8-sig'))
assert hs['state']==ss['state']=='succeeded','Latest hardware/software build did not succeed'
for base,state in [(hardware,hs),(software,ss)]:
    for artifact in state['artifacts']:
        assert sha(base/artifact['file'])==artifact['sha256'],artifact['file']
for entry in hs['source_hashes']:
    original=root/'hardware'/Path(entry['Path']).relative_to(stage)
    assert sha(original)==entry['Hash'],f'Hardware source changed: {original}'
for entry in ss['source_hashes']:
    name=Path(entry['Path']).name
    if name=='build_driver.ps1': original=root/'firmware/build.ps1'
    elif name in ['build.tcl','linker_template.ld','Xilinx.spec']: original=root/'firmware'/name
    else: original=root/'firmware/standalone'/name
    assert sha(original)==entry['Hash'],f'Firmware source changed: {original}'
xsa=hardware/'sp2_ps_screen.xsa'
assert ss['xsa_sha256']==sha(xsa),'ELFs were not built against the final XSA'
with zipfile.ZipFile(xsa) as z:
    bitnames=[name for name in z.namelist() if name.endswith('.bit')]
    assert len(bitnames)==1 and z.read(bitnames[0])==(hardware/'sp2_ps_screen.bit').read_bytes()
    hwh=ET.fromstring(z.read('screen_system.hwh'))
    modules={m.get('INSTANCE'):m for m in hwh.findall('.//MODULE')}
    expected={
        'ps7':{'PCW_UIPARAM_DDR_BUS_WIDTH':'16 Bit','PCW_UIPARAM_DDR_DRAM_WIDTH':'16 Bits',
               'PCW_DDR_RAM_HIGHADDR':'0x1FFFFFFF','PCW_UIPARAM_DDR_FREQ_MHZ':'400',
               'PCW_UART1_UART1_IO':'MIO 52 .. 53','PCW_PRESET_BANK0_VOLTAGE':'LVCMOS 3.3V',
               'PCW_PRESET_BANK1_VOLTAGE':'LVCMOS 3.3V'},
        'vdma':{'C_NUM_FSTORES':'2','C_M_AXI_MM2S_DATA_WIDTH':'64','C_M_AXIS_MM2S_TDATA_WIDTH':'24'},
        'vtc':{'GEN_HACTIVE_SIZE':'1024','GEN_VACTIVE_SIZE':'600','GEN_HFRAME_SIZE':'1344',
               'GEN_F0_VFRAME_SIZE':'635','GEN_HSYNC_POLARITY':'High','GEN_VSYNC_POLARITY':'High'}}
    for module,properties in expected.items():
        params={p.get('NAME'):p.get('VALUE') for p in modules[module].findall('./PARAMETERS/PARAMETER')}
        for key,value in properties.items():assert params[key]==value,(module,key,params.get(key))
    bases={m.get('INSTANCE'):m.get('BASEVALUE') for m in hwh.findall('.//MEMRANGE')}
    assert bases['status_gpio']=='0x41200000' and bases['vdma']=='0x43000000' and bases['vtc']=='0x43C00000'
    for reset in ['rst_axi','rst_pixel']:
        ports={p.get('NAME'):p.attrib for p in modules[reset].findall('./PORTS/PORT')}
        assert ports['ext_reset_in']['POLARITY']=='ACTIVE_LOW'
        assert ports['aux_reset_in']['POLARITY']=='ACTIVE_LOW'
        assert ports['aux_reset_in']['SIGNAME']=='const1_dout','Active-low auxiliary reset held asserted'
        assert ports['mb_debug_sys_rst']['SIGNAME']=='const0_dout'
        expected_lock='const1_dout' if reset=='rst_axi' else 'rgb_io_pixel_locked'
        assert ports['dcm_locked']['SIGNAME']==expected_lock
    for name in ['ps7_init.tcl','ps7_init.c','ps7_init.h']:
        (hardware/name).write_bytes(z.read(name))

elf_results=[]
for name,low,high in [('ps_probe_ocm',0,0x30000),('screen_bringup',0x100000,0x1000000)]:
    data=(software/f'{name}.elf').read_bytes()
    assert data[:6]==b'\x7fELF\x01\x01' and struct.unpack_from('<H',data,18)[0]==40,'Not ARM ELF32 little-endian'
    entry,phoff=struct.unpack_from('<II',data,24)
    entsize,count=struct.unpack_from('<HH',data,42)
    segments=[]
    for n in range(count):
        kind,offset,virt,phys,filesz,memsz,flags,align=struct.unpack_from('<8I',data,phoff+n*entsize)
        if kind==1:
            assert low<=virt and virt+memsz<=high and phys==virt,(name,hex(virt),hex(memsz))
            segments.append({'start':hex(virt),'end_exclusive':hex(virt+memsz)})
    assert segments and entry==low
    if name=='ps_probe_ocm':assert b'WAIT_D:' in data and b'VIDEO_STARTED:' not in data
    else:assert b'VIDEO_STARTED:' in data and b'WAIT_D:' not in data
    elf_results.append({'name':name,'entry':hex(entry),'load_segments':segments,'state':'verified-offline','board_execution':'not-assessed'})
    print(f'PASS: {name}: entry={entry:#x}, all load/heap/stack ranges in [{low:#x},{high:#x})')
timing=(hardware/'timing_summary.rpt').read_text(encoding='utf-8',errors='replace').splitlines()
i=next(i for i,line in enumerate(timing) if 'WNS(ns)' in line)
values=timing[i+2].split();wns,whs=float(values[0]),float(values[4])
assert wns>=0 and whs>=0
drc=(hardware/'drc.rpt').read_text(encoding='utf-8',errors='replace')
assert 'ZPS7-1' not in drc and '| Error ' not in drc and '| Critical Warning ' not in drc
io_log=Path(r'D:\FPGA_Project\SP2_PS_Screen_Build\io_sim\simulation.log').read_text(encoding='utf-8',errors='replace')
assert 'PASS: PS RGB layer' in io_log and 'Fatal:' not in io_log
result={'state':'offline-only','board_run':'not-assessed','panel_test':'not-assessed','board_evidence':'docs/2026-10-20-board-bringup.md','ddr_profile':'candidate RE-125 timing for TW-107 hardware; board delays unmeasured',
        'wns_ns':wns,'whs_ns':whs,'elfs':elf_results,'xsa_sha256':sha(xsa),
        'drc_warning_count':sum(int(n) for n in re.findall(r'\|\s*[\w-]+\s*\|\s*Warning\s*\|[^\n]*\|\s*(\d+)\s*\|',drc)),
        'drc_advisory_count':sum(int(n) for n in re.findall(r'\|\s*[\w-]+\s*\|\s*Advisory\s*\|[^\n]*\|\s*(\d+)\s*\|',drc)),
        'known_reports':'Vendor FIFO/BRAM and VTC CDC diagnostics retained; consult the reports. This is not a clean CDC signoff.'}
(root/'build/ps-validation.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(f'PASS: final XSA/bitstream, current sources, address map and timings WNS={wns} WHS={whs}')
print('PASS: custom RGB IO simulation; wrote build/ps-validation.json. NO board/panel execution inferred.')
