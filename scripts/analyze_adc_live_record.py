"""Independent little-endian DDR record audit; no board access."""
from pathlib import Path
import argparse,json,math,statistics,struct,zlib

def analyze(data,rate):
    if len(data)!=16384:raise ValueError('Expected4096 dual-channel words')
    words=struct.unpack('<4096I',data)
    if any(w&0xf800f800 for w in words):raise ValueError('Nonzero reserved bits')
    result={'record_hz':rate,'words':4096,'duration_s':4096/rate,'crc32':f'{zlib.crc32(data):08X}','channels':[]}
    for ch in range(2):
        codes=[(w>>(16*ch))&1023 for w in words]
        volt=[c*10/1023-5 for c in codes];mean=statistics.fmean(codes)
        span=max(codes)-min(codes);crossings=[]
        if span>=16:
            # Median intervals from interpolated mean upcrossings are independent
            # of the board's delayed high-threshold confirmation implementation.
            crossings=[i-1+(mean-a)/(b-a) for i,(a,b) in enumerate(zip(codes,codes[1:]),1) if a<=mean<b]
        intervals=[b-a for a,b in zip(crossings,crossings[1:])]
        frequency=None
        if len(intervals)>=2:
            period=statistics.median(intervals)
            if min(intervals)>=4 and all(abs(p-period)<=.2*period for p in intervals):frequency=rate/period
        result['channels'].append(dict(channel=ch+1,min_code=min(codes),max_code=max(codes),mean_code=mean,
            otr_count=sum(bool(w&(1<<(16*ch+10))) for w in words),mean_v_nominal=statistics.fmean(volt),
            vpp_v_nominal=span*10/1023,rms_total_v_nominal=math.sqrt(statistics.fmean(v*v for v in volt)),
            frequency_hz=frequency,voltage_calibrated=False))
    return result

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('record',type=Path);p.add_argument('--crc',required=True)
    p.add_argument('--rate',type=int,default=625000);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();assert a.rate>0
    report=analyze(a.record.read_bytes(),a.rate)
    assert report['crc32']==a.crc.upper(),f"DDR CRC {report['crc32']} != UART {a.crc}"
    a.output.write_text(json.dumps(report,indent=2)+'\n')
    print('ADC_LIVE_RECORD_AUDIT_PASS: '+str(a.record)+' CRC='+report['crc32'])
    print(json.dumps(report['channels'],indent=2))
