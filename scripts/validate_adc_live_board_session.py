"""Validate saved external ADC board evidence without serial/JTAG access."""
from pathlib import Path
import hashlib,json,math,re,statistics,struct,zlib
root=Path(__file__).resolve().parents[1]
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest().upper()
s=read(root/'build/adc-live-soc/board-session.json');run=Path(s['run'])
assert s['state']=='verified-board-external-ddr-transfer-awaiting-signal-and-panel-review'
assert s['real_adc']=='external-records-exported-CRC-verified' and not s['flash_written'] and not s['resume_needed']
for f in s['files']:assert sha(Path(f['Path']))==f['Hash'],f['Path']
assert sha(run/'loader.ps1')==s['loader_sha256'] and sha(run/'uart.log')==s['uart_sha256']
boot=(run/'boot.log').read_text(encoding='utf-8-sig');uart=(run/'uart.log').read_text(encoding='utf-8-sig')
assert 'BOOT_MODE=0' in boot and 'ADC_LIVE_APP_RAM_LOADED' in boot
assert 'ADC_LIVE_READY: EXTERNAL BANK33/J6 conversion_hz=5000000 record_hz=625000' in uart
assert 'TOUCH_STATUS available=1 family=GTXX address=5d id=911 ' in uart
assert not re.search(r'APP_FATAL|ADC_LIVE_FAIL|VIDEO_PRESENT_FAIL|VIDEO_STOP_TIMEOUT|TAP_QUEUED',uart)
start=uart.index('LVGL_READY os=freertos')
diags=re.findall(r'APP_DIAG os=freertos[^\r\n]+',uart[start:]);assert len(diags)>=3
for line in diags:
 for field in ['locked=1','pixel_locked=1','dma_errors=00000000','underflow_sample=0','overflow_sample=0','tasks=5']:
  assert re.search(r'(?:^| )'+field+r'(?: |$)',line),(field,line)
assert len(s['records'])==2 and s['records'][1]['id']>s['records'][0]['id']>0
records=[]
for i,meta in enumerate(s['records']):
 p=run/meta['file'];data=p.read_bytes();assert len(data)==16384 and sha(p)==meta['sha256']
 crc=f'{zlib.crc32(data):08X}';assert crc==meta['crc32']
 assert re.search(rf'ADC_LIVE_STATUS available=1 pending=[01] id={meta["id"]} address={meta["address"]} bytes=16384 rate=625000 crc32={crc.lower()}',uart)
 assert 'ADC_LIVE_RECORD_EXPORTED' in (run/f'export{i}.log').read_text()
 words=struct.unpack('<4096I',data);assert not any(w&0xf800f800 for w in words)
 report=read(run/f'record{i}.json');assert report['crc32']==crc and report['record_hz']==625000
 for c in range(2):
  codes=[(w>>(c*16))&1023 for w in words];v=[x*10/1023-5 for x in codes];r=report['channels'][c]
  assert (r['min_code'],r['max_code'])==(min(codes),max(codes))
  assert abs(r['mean_code']-statistics.fmean(codes))<1e-9
  assert abs(r['rms_total_v_nominal']-math.sqrt(statistics.fmean(x*x for x in v)))<1e-9
  assert r['otr_count']==sum(bool(w&(1<<(c*16+10))) for w in words) and not r['voltage_calibrated']
 records.append(dict(id=meta['id'],crc32=crc,adc1=report['channels'][0],adc2=report['channels'][1]))
assert s['server']=='owned-server-stopped'
result=dict(state='verified-saved-external-ADC-transfer',run=str(run),records=records,
 input_evidence=s['analog_input'],physical_panel=s['physical_panel'],conversion_hz_nominal=5000000,
 record_hz_nominal=625000,diagnostic_samples=len(diags),flash_written=False,
 limitations=['no calibrated voltage or timebase claim','ADC2 no known input','panel interaction awaits user confirmation','input timing candidate, no full PVT signoff'])
(root/'build/adc-live-soc/board-validation.json').write_text(json.dumps(result,indent=2)+'\n')
print('PASS: actual external ADC boot, frozen DDR records/IDs/CRCs, dual-channel format/statistics and video diagnostics; no hardware connection')
