"""Prove frozen CH2 trigger crossing while the UI measures flat CH1."""
from pathlib import Path
import argparse,json,math,struct,zlib
p=argparse.ArgumentParser();p.add_argument('run',type=Path);p.add_argument('--hardware-trigger',action='store_true');args=p.parse_args();r=args.run
u=json.loads((r/'trigsrc-frame-status.json').read_text(encoding='utf-8-sig'))
raw=(r/'trigsrc-single-record.bin').read_bytes();words=struct.unpack('<4096I',raw)
assert u['source']=='ADC1' and u['trig_channel']=='2' and u['triggered']=='1' and u['single_complete']=='1'
level=(int(u['level_mv'])/1000+5)*102.3
if args.hardware_trigger:level=math.floor(level*256+.5)/256
measurement=[w&1023 for w in words];trigger=[(w>>16)&1023 for w in words]
assert max(measurement)<level,'Test requires known signal only on CH2; CH1 crossed trigger level'
assert not any(w&(1<<26) for w in words)
cross=float(u['crossing']);i=math.floor(cross)
assert trigger[i]<=level<trigger[i+1]
actual=i+(level-trigger[i])/(trigger[i+1]-trigger[i])
assert abs(actual-cross)<.001 and abs(cross-float(u['view_start'])-250)<.004
out=dict(state='verified-host-independent-trigger-source',measurement_channel=1,trigger_channel=2,
         measurement_min=min(measurement),measurement_max=max(measurement),level_code=level,
         raw_crossing_sample=actual,reported_crossing_sample=cross,alignment_samples=250,
         record_crc32=f'{zlib.crc32(raw):08X}',calibrated=False)
(r/'trigsrc-record-audit.json').write_text(json.dumps(out,indent=2)+'\n')
print('INDEPENDENT_TRIGGER_RECORD_PASS: '+json.dumps(out))
