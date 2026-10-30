"""Audit saved internal-digital board results. Never connects to hardware."""
from pathlib import Path
import hashlib,json,re,struct,zlib

root=Path(__file__).resolve().parents[1]
base=root/'build/adc-soc'
state=json.loads((base/'board-session.json').read_text(encoding='utf-8-sig'))
expected=json.loads((base/'validation.json').read_text(encoding='utf-8-sig'))
assert state['state']=='verified-board-internal-digital' and state['flash_written'] is False
run=Path(state['run'])


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest().upper()


assert sha(root/'scripts/run_adc_internal_board_test.ps1')==state['loader_script_sha256']
for item in state['artifacts']:assert sha(Path(item['Path']))==item['Hash']
for name,key in [('platform.bit','bit_sha256'),('application.elf','elf_sha256'),
                 ('ps7_init.tcl','ps7_init_sha256'),('board_init_helpers.tcl','board_init_helpers_sha256')]:
    assert sha(run/name)==expected[key]
assert sha(run/'uart.log')==state['uart_sha256']
boot=(run/'boot.log').read_text(encoding='utf-8-sig')
assert 'BOOT_MODE=0' in boot and 'ADC_APP_RAM_LOADED' in boot
uart=(run/'uart.log').read_text(encoding='utf-8-sig')
for marker in ('DDR_SAMPLE_PASS: 3x64KiB','VIDEO_BASE_READY:',
               'ADC_INTERNAL_TIMEOUT_PASS: status=0000004e id=2',
               'ADC_INTERNAL_SELFTEST_PASS:'):
    assert marker in uart,marker
assert not re.search(r'APP_FATAL|DDR_FAIL|ADC_[A-Z_]*FAIL',uart)
records=list(state['records'])+[dict(name='recovery',**state['recovery_export'])]
assert [x['name'] for x in records]==['copy0','copy1','recovery']
assert [int(x['id']) for x in records]==[1,2,3]
results=[]
for item in records:
    name=item['name'];binary=run/(name+'.bin');data=binary.read_bytes()
    assert len(data)==16384 and sha(binary)==item['sha256']
    crc=zlib.crc32(data)&0xffffffff
    assert crc==int(item['crc32'],16)
    pattern=(rf'ADC_INTERNAL_RECORD_PASS: {name} id={item["id"]} address={item["address"]} '
             rf'bytes=16384 first_n=([0-9a-fA-F]+) crc32={item["crc32"]}')
    match=re.search(pattern,uart);assert match,name
    first=int(match[1],16)
    words=struct.unpack('<4096I',data)
    for i,word in enumerate(words):
        n=(first+i)&0xfffff;a=n&1023;b=(1023-a)^((n>>10)&1023)
        wanted=a|(((n>>2)&1)<<10)|(b<<16)|(((n>>3)&1)<<26)
        assert word==wanted,(name,i,hex(word),hex(wanted))
    metadata=json.loads(binary.with_suffix('.json').read_text())
    assert metadata['source']=='board-ddr' and metadata['first_sequence']==first
    assert int(metadata['crc32'],16)==crc and metadata['sha256']==sha(binary)
    results.append(dict(name=name,id=int(item['id']),address=item['address'],words=4096,
                        first_sequence=first,crc32=f'{crc:08X}',sha256=sha(binary)))
health=(run/'recovery-export.log').read_text(encoding='utf-8-sig')
values={key:int(re.search(rf'^{key}=(\d+)$',health,re.M)[1])
        for key in ('ADC_STATUS','ADC_CAPTURE_ID','VIDEO_GPIO','MM2S_SR')}
assert values['ADC_STATUS']==0xc2 and values['ADC_CAPTURE_ID']==3
assert values['VIDEO_GPIO']==9
assert values['MM2S_SR']&0xff0==0 and values['MM2S_SR']&1==0
assert 'RECOVERY_EXPORTED' in health
result=dict(state='verified-board-internal-digital',run=str(run),records=results,health=values,
            dma_error_mask='0x00000FF0',dma_errors=0,flash_written=False,
            board_scope='PL internal digital source -> frozen BRAM -> PS -> DDR -> independent host checks',
            ddr_full_capacity_or_stress='not-run',real_adc_sampling='not-run',physical_lcd='not-run',
            capacitive_touch='not-run',freertos_lvgl_live_record='not-implemented',
            uart_sha256=sha(run/'uart.log'),health_log_sha256=sha(run/'recovery-export.log'))
(base/'board-validation.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
print('PASS: saved JTAG session, matching candidate hashes, three independent DDR CRC/sequence checks, timeout recovery and video/DMA health')
print(json.dumps(result))
