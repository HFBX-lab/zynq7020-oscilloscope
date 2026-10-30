"""Independent full-record voltage/period evidence audit; no board access."""
from pathlib import Path
import argparse
import json
import math
import struct

from audit_hardware_trigger_evidence import checked_bytes, load
from audit_highspeed_50m_evidence import known_tone_fit


def check_values(words, rate, measured):
    assert len(words)==4096 and not any(w&0xf800f800 for w in words)
    context=measured['context']
    assert int(context['rate'])==rate and int(context['count'])==len(words)
    assert context['channel'] in ('1','2') and context['panel'] in ('0','1')
    assert len(measured['channels'])==2
    detail=[]
    for ch,item in enumerate(measured['channels']):
        assert item['ch']==str(ch+1) and item['valid']=='1'
        codes=[(w>>(ch*16))&1023 for w in words]
        volt=[c*10/1023-5 for c in codes]
        mean=math.fsum(volt)/len(volt)
        expected=dict(min_v=min(volt),max_v=max(volt),mean_v=mean,vpp_v=max(volt)-min(volt),
            rms_total_v=math.sqrt(math.fsum(v*v for v in volt)/len(volt)),
            rms_ac_v=math.sqrt(math.fsum((v-mean)**2 for v in volt)/len(volt)))
        overrange=any(c in (0,1023) for c in codes) or any(w&(1<<(ch*16+10)) for w in words)
        assert item['overrange']==str(int(overrange))
        for key,value in expected.items():
            got=float(item[key]);assert math.isfinite(got) and abs(got-value)<=.00000051,(ch,key,got,value)
        frequency,period=float(item['frequency_hz']),float(item['period_us'])
        assert math.isfinite(frequency) and math.isfinite(period)
        assert item['frequency_valid'] in ('0','1')
        if item['frequency_valid']=='1':
            assert not overrange and max(codes)-min(codes)>=16
            assert 2*rate/(len(words)-1)<=frequency<=rate/4
            assert abs(period-1e6/frequency)<=.0000011
        else:
            assert frequency==period==0
        detail.append(dict(channel=ch+1,expected=expected,overrange=overrange,
                           frequency_hz=frequency,period_us=period))
    return detail


def audit(directory):
    session=load(directory/'session.json')
    assert session['state'].startswith('verified-board-') and session['flash_written'] is False
    entries=session['measurement_records']
    assert [e['name'] for e in entries]==['measure-raw-ch2','measure-raw-ch1','measure-cic6m25','measure-cic625k']
    checks=session['measurement_checks']
    names=['measurement-RAW50M-pointer-open-frozen','measurement-pointer-CH1-keeps-independent-trigger',
           'measurement-pointer-CH2-restores-same-record','measurement-independent-of-vertical-scale-position',
           'measurement-pointer-close-keeps-frozen','measurement-invalid-panel-command-rejected',
           'measurement-frozen-RAW-keeps-own-rate-while-requesting-CIC',
           'measurement-new-CIC6m25-record','measurement-new-CIC625k-record']
    assert [c['test'] for c in checks]==names
    raw=checks[0]
    for index,c in enumerate(checks):
        u,a,m=c['ui'],c['adc'],c['measurement']
        # A frozen timebase change clears the old "single complete" badge,
        # while its record and measurement cache remain protected.
        assert u['running']=='0' and u['trig_channel']=='2'
        assert u['single_complete']==('0' if index==6 else '1')
        assert a['display_id']==u['display_id']==m['context']['display_id']
        assert m['context']['rate']==a['display_rate'] and m['context']['epoch']==u['epoch']
        assert m['context']['channel']==('1' if u['source']=='ADC1' else '2')
    for c in checks[:7]:
        for key in ('display_id','display_crc','display_rate','display_filter','display_epoch','display_conversion_hz'):
            assert c['adc'][key]==raw['adc'][key]
        assert c['measurement']['channels']==raw['measurement']['channels']
    assert all(c['ui']['epoch']==raw['ui']['epoch'] for c in checks[:6])
    assert checks[4]['measurement']['context']['panel']==checks[5]['measurement']['context']['panel']=='0'
    assert checks[6]['ui']['time']=='6' and checks[6]['adc']['display_rate']=='50000000'
    assert int(checks[6]['ui']['epoch'])==(int(raw['ui']['epoch'])+1)%2**32
    report=[]
    for entry,rate,check in zip(entries,[50000000,50000000,6250000,625000],[checks[0],checks[1],checks[7],checks[8]]):
        m=entry['measurement'];u,a=entry['ui'],entry['adc']
        assert m==check['measurement']
        # LVGL can finish a pending redraw between the two read-only queries.
        # Bind acquisition ownership rather than its unrelated video counter.
        for key in ('display_id','epoch','source','trig_channel','time','running','single_complete'):
            assert u[key]==check['ui'][key]
        for key in ('display_id','display_crc','display_rate','display_filter','display_epoch','display_conversion_hz','pending'):
            assert a[key]==check['adc'][key]
        assert int(entry['rate'])==rate and m['context']['panel']=='1'
        assert entry['id']==u['display_id']==a['display_id']==m['context']['display_id']
        assert entry['crc32'].upper()==a['display_crc'].upper()
        assert a['pending']=='0' and u['running']=='0'
        data=checked_bytes(directory/(entry['name']+'.bin'),entry['crc32'],entry['sha256'],16384)
        words=struct.unpack('<4096I',data)
        values=check_values(words,rate,m)
        assert m['channels'][0]['frequency_valid']=='0' and m['channels'][1]['frequency_valid']=='1'
        tone=known_tone_fit([(w>>16)&1023 for w in words],rate)
        assert abs(values[1]['frequency_hz']/tone['frequency_hz']-1)<.02
        checked_bytes(directory/(entry['name']+'-frame.bin'),entry['frame_crc'],entry['frame_sha256'],1843200)
        report.append(dict(name=entry['name'],rate=rate,crc32=entry['crc32'],values=values,known_tone_fit=tone))
    result=dict(state='verified-offline-independent-measurement-evidence',raw_records=4,ddr_frames=4,
                controls=len(checks),records=report,voltage_calibrated=False,physical_panel='awaiting-current-feedback')
    return result


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('run',type=Path);directory=p.parse_args().run
    result=audit(directory)
    (directory/'measurement-evidence-audit.json').write_text(json.dumps(result,indent=2)+'\n')
    print('MEASUREMENT_EVIDENCE_PASS',json.dumps(result))
