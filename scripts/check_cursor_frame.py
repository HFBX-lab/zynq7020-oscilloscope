"""Independent calculation from frozen raw words, plus cursor-line image check."""
from pathlib import Path
import argparse,json,math,struct,zlib
from PIL import Image
p=argparse.ArgumentParser();p.add_argument('run',type=Path);p.add_argument('--highspeed-10m',action='store_true');p.add_argument('--highspeed-50m',action='store_true');a=p.parse_args();r=a.run
u=json.loads((r/'cursor-frame-status.json').read_text(encoding='utf-8-sig'))
raw=(r/'cursor-single-record.bin').read_bytes();words=struct.unpack('<4096I',raw)
times=[.00005,.0001,.0002,.0005,.001,.002,.005,.01,.02,.05,.1,.2,.5]
if a.highspeed_10m or a.highspeed_50m:times=[.000001,.000002,.000005,.00001,.00002]+times
if a.highspeed_50m:times=[.0000001,.0000002,.0000005]+times
ch=int(u['source'][-1])-1;span=10*times[int(u['time'])]*625000
pos=[int(u['cursor_a']),int(u['cursor_b'])];volts=[]
for t in pos:
    at=float(u['view_start'])+span*t/1000;n=math.floor(at);f=at-n
    assert 0<=at<=4095
    x=(words[n]>>(ch*16))&2047;y=(words[n+1]>>(ch*16))&2047 if f else x
    assert not ((x|y)&1024)
    volts.append((x+f*(y-x))*10/1023-5)
dt_us=span*(pos[1]-pos[0])/1000/625000*1e6;dv=volts[1]-volts[0]
assert abs(dt_us-float(u['cursor_dt_us']))<1e-5
# UART display_start is rounded to 0.001 sample; allow at most1mV in this raw recheck.
assert abs(dv-float(u['cursor_dv']))<.001
assert u['cursor_valid']=='1' and abs(1e6/abs(dt_us)-float(u['cursor_inv_hz']))<1e-5
im=Image.open(r/'cursor-frame.png').convert('RGB');line_counts=[]
for rgb in [(120,169,255),(255,134,183)]:
    columns=[(sum(im.getpixel((x,y))==rgb for y in range(155,460)),x) for x in range(25,767)]
    count,x=max(columns);assert count>250;line_counts.append(dict(x=x,pixels=count))
    expected=25+1+741*pos[len(line_counts)-1]//1000
    assert abs(x-expected)<=2,(x,expected)
out=dict(state='verified-host-from-frozen-raw-and-frame',record_crc32=f'{zlib.crc32(raw):08X}',dt_us=dt_us,
         voltage_v=volts,delta_v=dv,reported_delta_v=float(u['cursor_dv']),voltage_tolerance_v=.001,
         reason='UART display_start rounded to 0.001 sample',line_columns=line_counts,calibrated=False)
(r/'cursor-numeric-audit.json').write_text(json.dumps(out,indent=2)+'\n')
print('CURSOR_RAW_FRAME_AUDIT_PASS: '+json.dumps(out))
