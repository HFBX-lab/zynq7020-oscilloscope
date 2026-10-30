"""Check frozen raw samples against raster positions using per-channel settings."""
from pathlib import Path
import argparse,json,math,struct,statistics
from PIL import Image
p=argparse.ArgumentParser();p.add_argument('run',type=Path);a=p.parse_args();r=a.run
u=json.loads((r/'channel-frame-status.json').read_text(encoding='utf-8-sig'))
words=struct.unpack('<4096I',(r/'channel-single-record.bin').read_bytes())
im=Image.open(r/'channel-frame.png').convert('RGB');scales=[.1,.2,.5,1,2,5,10];result=[]
for ch,rgb in [(0,(64,224,192)),(1,(255,200,87))]:
    scale=scales[int(u[f'ch{ch+1}_scale'])];position=int(u[f'ch{ch+1}_pos']);matches=[]
    for i in [50,125,200,275,350,425,500,650]:
        at=float(u['view_start'])+1250*i/719;n=math.floor(at);f=at-n
        before=(words[n]>>(16*ch))&1023;after=(words[n+1]>>(16*ch))&1023
        volts=(before+f*(after-before))*10/1023-5
        plot=max(-4000,min(4000,math.floor((volts/scale*1000+position)+.5)))
        # LVGL chart content:742x326 pixels, border1, range-4000..4000.
        x=25+742*i//719;y=463-int((plot+4000)*326/8000)
        found=any(im.getpixel((xx,yy))==rgb for xx in range(x-2,x+3) for yy in range(max(138,y-4),min(462,y+5)))
        matches.append(dict(point=i,predicted_xy=[x,y],matched=found))
    assert sum(m['matched'] for m in matches)>=6,(ch,matches)
    ys=[y for x in range(0,24) for y in range(138,462) if im.getpixel((x,y))==rgb]
    assert len(ys)>3
    zero_y=300-position*326/8000
    assert abs(statistics.median(ys)-zero_y)<=8,(ch,ys,zero_y)
    result.append(dict(channel=ch+1,volts_per_div=scale,position_div=position/1000,plot_samples=matches,zero_reference_y=statistics.median(ys)))
(r/'channel-plot-audit.json').write_text(json.dumps(dict(state='verified-host-raw-to-frame',channels=result,voltage_calibrated=False),indent=2)+'\n')
print('CHANNEL_RAW_FRAME_PLOT_PASS: independent scale/position and zero references for both channels')
