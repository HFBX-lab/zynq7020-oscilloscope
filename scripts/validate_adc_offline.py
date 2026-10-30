"""Audit saved ADC digital-core evidence; never connects to hardware."""
from pathlib import Path
import csv
import hashlib
import json
import re

root=Path(__file__).resolve().parents[1]
base=root/'build/adc'
state=json.loads((base/'status.json').read_text(encoding='utf-8-sig'))
assert state['state']=='succeeded' and state['task']=='all'
assert state['scope']=='offline-only' and state['board_test']=='not-run'
assert all(state[k]=='passed' for k in ('simulation','synthesis','netlist_simulation'))
run=Path(state['run']);stage=run/'source'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


for item in state['sources']:
    assert sha(stage/item['file'])==sha(root/'hardware/adc'/item['file'])==item['sha256'],item
assert sha(root/'tests/adc_pinmap_test.py')==state['pinmap_test_sha256']
for item in state['artifacts']:
    assert sha(run/item['file'])==item['sha256'],item
    if (base/item['file']).exists():assert sha(base/item['file'])==item['sha256'],item

for name,depth,warmup in [('full',4096,4),('minimum',2,4),('no_warmup',16,0),('netlist',4096,4)]:
    text=(run/f'simulation-{name}.log').read_text(encoding='utf-8-sig')
    assert f'PASS: ADC snapshot depth={depth} warmup={warmup}' in text
    assert not re.search(r'^Fatal:|^ERROR:',text,re.M)
pin_log=(run/'pinmap-tests.log').read_text(encoding='utf-8-sig')
assert 'Ran 5 tests' in pin_log and 'OK' in pin_log and 'FAILED' not in pin_log
vivado=(run/'vivado.log').read_text(encoding='utf-8-sig')
assert 'ADC_OFFLINE_SYNTH_PASS:' in vivado and not re.search(r'^ERROR:',vivado,re.M)
brams=re.search(r'ADC_OFFLINE_SYNTH_PASS: BRAM36=(\d+) BRAM18=(\d+)',vivado)
assert brams
bram36,bram18=map(int,brams.groups())
assert bram36*36864+bram18*18432>=4096*22
assert (run/'adc_snapshot_ooc.dcp').stat().st_size>0
assert (run/'adc_snapshot_funcsim.v').stat().st_size>0

mapping=json.loads((root/'hardware/adc/pinmap.json').read_text(encoding='utf-8'))
with (run/'package-pins.csv').open(encoding='utf-8-sig',newline='') as f:rows=list(csv.DictReader(f))
assert len(rows)==52
for bank in ('33','35'):
    expected={x['port']:x for x in mapping['banks'][bank]['signals']}
    actual={x['port']:x for x in rows if x['bank']==bank}
    assert set(actual)==set(expected) and len(actual)==26
    for name,pin in expected.items():
        assert actual[name]['package_pin']==pin['package_pin']
        pair=pin['net'].split('_')[-1]
        assert f'_{pair}_' in actual[name]['pin_func'] and actual[name]['pin_func'].endswith('_'+bank)
assert 'IO_L2P_T0_33' in (run/'corrected-T22.rpt').read_text()
assert 'DONE_0' in (run/'old-typo-T12.rpt').read_text()

# The full-depth checker must reject a repeat of each 1024-word address block.
mutation=Path('D:/FPGA_Project/SP2_ADC_Build/mutation-read-alias')
original=(root/'hardware/adc/rtl/adc_snapshot_core.v').read_text()
assert (mutation/'adc_snapshot_core.v').read_text(encoding='utf-8-sig').strip()==original.replace('memory[rd_addr]',"memory[rd_addr & 10'h3ff]").strip()
assert (mutation/'tb_adc_snapshot.sv').read_text().strip()==(root/'hardware/adc/sim/tb_adc_snapshot.sv').read_text().strip()
assert 'Fatal: word 1024' in (mutation/'simulation.log').read_text(encoding='utf-8-sig')
sources=json.loads((root/'artifacts/adc-reference/sources.json').read_text(encoding='utf-8'))
for item in sources.values():assert sha(Path(item['path'])).lower()==item['sha256']

result=dict(state='offline-only',run=str(run),rtl_configurations=3,functional_netlist='passed',
    pinmap_tests=5,package_pin_assignments=52,bram36=bram36,bram18=bram18,
    stored_variable_bits=4096*22,api_record_bytes=4096*4,
    address_alias_mutation='rejected at word1024',mutation_log_sha256=sha(mutation/'simulation.log'),
    physical_implementation='not-run',bitstream='not-generated',jtag='not-connected',
    ps_register_bridge='not-implemented',adc_module='absent',real_adc_sampling='not-run',
    physical_input_timing='candidate-not-validated')
(base/'validation.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
print('PASS: ADC current-source/artifact hashes, RTL+functional netlist, 52 package-pin checks, BRAM capacity and address-alias regression')
print(json.dumps(result))
