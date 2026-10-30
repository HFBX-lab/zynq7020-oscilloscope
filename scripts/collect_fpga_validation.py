"""Validate artifact provenance and summarize this offline FPGA delivery."""
from pathlib import Path
import hashlib
import json
import re

root=Path(__file__).resolve().parents[1]
staged=Path(r'D:\FPGA_Project\SP2_Scope_Build\hardware')
results=[]
for name,folder in [('colorbars','vivado'),('grid','vivado_grid'),('waveform','vivado_waveform')]:
    base=root/'build'/folder
    status=json.loads((base/'build-status.json').read_text(encoding='utf-8-sig'))
    assert status['state']=='succeeded',f'{name}: latest build did not succeed'
    bit=base/status['artifact']['file']
    assert hashlib.sha256(bit.read_bytes()).hexdigest().upper()==status['artifact']['sha256'],f'{name}: bitstream hash'
    for entry in status['source_hashes']:
        local=root/'hardware'/Path(entry['Path']).relative_to(staged)
        assert hashlib.sha256(local.read_bytes()).hexdigest().upper()==entry['Hash'],f'{name}: source changed: {local}'
    log=(base/'build.log').read_text(encoding='utf-8',errors='replace')
    assert f'BUILD_PASS: {name}' in log,f'{name}: no build PASS'
    assert not re.search(r'^ERROR:|^CRITICAL WARNING:',log,re.M),f'{name}: build errors'
    text=(base/'timing_summary.rpt').read_text(encoding='utf-8',errors='replace')
    lines=text.splitlines()
    i=next(i for i,s in enumerate(lines) if 'WNS(ns)' in s)
    summary=lines[i+2].split()
    wns,whs=float(summary[0]),float(summary[4])
    assert wns>=0 and whs>=0,f'{name}: timing failed'
    assert 'All user specified timing constraints are met.' in text
    results.append({'pattern':name,'state':'offline-only','bitstream':str(bit),
                    'sha256':status['artifact']['sha256'],'bytes':bit.stat().st_size,
                    'wns_ns':wns,'whs_ns':whs,'warnings':['ZPS7-1: PS7 not instantiated']})
    print(f'PASS: {name}: bitstream + current sources match; WNS={wns:.3f}ns, WHS={whs:.3f}ns')
for name,marker in [('core','PASS: two'),('patterns','PASS: all three'),('top','PASS: UNISIM'),('powerup','PASS: production')]:
    log=(root/'build/xsim/offline'/f'{name}.log').read_text(encoding='utf-8',errors='replace')
    assert marker in log and not re.search(r'^Fatal:|^ERROR:',log,re.M),f'{name}: simulation did not pass'
summary={'project':'FPGA竞赛示波器项目','hardware_tests':'not-run','adc':'not implemented',
         'simulations':'core/patterns/UNISIM clock+IO/production power-up passed',
         'simulation_boundary':'UNISIM top uses 16/16 startup counts and slows only unconnected spare MMCM outputs; separate power-up test uses full production counts and ideal 51.2MHz pixel clock.',
         'builds':results}
(root/'build/fpga-validation.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print('PASS: four simulation suites; wrote build/fpga-validation.json')
