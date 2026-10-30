"""Independent 50M autoset planning/transaction audit, no board access."""
from pathlib import Path
import argparse
import json
import math
import struct

from audit_hardware_trigger_evidence import checked_bytes, check_designated, load
from audit_highspeed_50m_evidence import TIMES, known_tone_fit, profile
from audit_measurement_evidence import check_values

SCALES=(.1,.2,.5,1,2,5,10)


def rounded(value):
    return int(math.copysign(math.floor(abs(value)+.5),value))


def make_plan(words,rate,frequency,channel=1):
    assert len(words)==4096 and rate>0 and channel in (0,1)
    assert not any(w&0xf800f800 for w in words)
    assert math.isfinite(frequency) and 2*rate/4095<=frequency<=rate/4
    stats=[]
    for c in range(2):
        codes=[(w>>(16*c))&1023 for w in words]
        volt=[v*10/1023-5 for v in codes]
        stats.append(dict(low=min(volt),high=max(volt),mean=math.fsum(volt)/4096,
                          clipped=min(codes)==0 or max(codes)==1023 or any(w&(1<<(16*c+10)) for w in words)))
    assert not stats[channel]['clipped'] and stats[channel]['high']-stats[channel]['low']>=320/1023-1e-12
    candidates=[]
    for index,seconds in enumerate(TIMES):
        output,_,kind=profile(index)
        cycles=10*seconds*frequency
        if 2<=cycles<=8 and 10*seconds*output<=4095 and frequency<=output/(4 if kind==0 else 10):
            candidates.append((abs(math.log(cycles/4)),index,output))
    assert candidates,'No supported full display window'
    _,index,output=min(candidates)
    plan=dict(time=index,rate=output,scales={},positions={},mask=0,trigger_channel=channel+1,
              level_mv=rounded((stats[channel]['low']+stats[channel]['high'])*500),frequency_hz=frequency,stats=stats)
    for c,s in enumerate(stats):
        if s['clipped']:continue
        for i,scale in enumerate(SCALES):
            position=rounded(-s['mean']/scale*1000)
            if (s['high']-s['low']<=6*scale and abs(s['mean'])<=3.5*scale and
                s['low']/scale+position/1000>=-3.5 and s['high']/scale+position/1000<=3.5):
                plan['scales'][c]=i;plan['positions'][c]=position;plan['mask']|=1<<c;break
    assert plan['mask']&(1<<channel)
    return plan


def delta(new,old):
    return (int(new)-int(old))%2**32


def restored(before,after,advance):
    for key in ('time','scale','ch1_scale','ch2_scale','ch1_pos','ch2_pos','source','trig_channel','trig_normal','trig_rising','level_mv','running','single_armed','single_complete','display_id'):
        assert before['ui'][key]==after['ui'][key],key
    for key in ('display_id','display_address','display_crc','display_rate','display_filter','display_conversion_hz','display_epoch'):
        assert before['adc'][key]==after['adc'][key],key
    assert before['measurement']['channels']==after['measurement']['channels']
    assert delta(after['ui']['epoch'],before['ui']['epoch'])==advance
    assert delta(after['ui']['seen_id'],before['ui']['seen_id'])<2**31


def audit(directory):
    s=load(directory/'session.json')
    assert s['state'].startswith('verified-board-') and s['flash_written'] is False
    entries={e['name']:e for e in s['autoset_records']}
    assert len(entries)==4 and [c['name'] for c in s['autoset_cases']]==['manual-stop','single']
    report=[]
    for case in s['autoset_cases']:
        source,result=entries[case['source']],entries[case['result']]
        before,completed,after=case['before'],case['completed'],case['after']
        assert source['id']==before['ui']['display_id']==before['measurement']['context']['display_id']
        assert source['crc32'].upper()==before['adc']['display_crc'].upper()
        data=checked_bytes(directory/(source['name']+'.bin'),source['crc32'],source['sha256'],16384)
        words=struct.unpack('<4096I',data)
        check_values(words,source['rate'],before['measurement'])
        frequency=float(before['measurement']['channels'][1]['frequency_hz'])
        assert before['measurement']['channels'][1]['frequency_valid']=='1'
        tone=known_tone_fit([(w>>16)&1023 for w in words],source['rate'])
        assert abs(frequency/tone['frequency_hz']-1)<.02
        plan=make_plan(words,source['rate'],frequency)
        a=completed['context'];u=after['ui'];adc=after['adc']
        assert a['state']=='2' and a['busy']=='0' and a['running']=='1' and a['single_complete']=='0'
        assert delta(a['epoch'],before['ui']['epoch'])==1
        assert 0<delta(a['display_id'],before['ui']['display_id'])<2**31
        assert int(a['target_time'])==int(a['time'])==plan['time']==int(u['time'])
        assert int(a['target_mask'])==plan['mask']
        assert int(a['display_rate'])==int(adc['display_rate'])==plan['rate']
        assert int(a['trigger_channel'])==int(u['trig_channel'])==plan['trigger_channel']
        assert int(a['level_mv'])==int(u['level_mv'])==plan['level_mv']
        assert abs(float(a['target_frequency_hz'])-frequency)<=.0000011
        assert u['source']=='ADC2' and u['trig_rising']=='1' and u['trig_normal']=='0' and u['running']=='0'
        assert delta(u['epoch'],a['epoch'])==1 and adc['display_epoch']==a['epoch']
        for c in range(2):
            scale=plan['scales'].get(c,int(before['ui'][f'ch{c+1}_scale']))
            position=plan['positions'].get(c,int(before['ui'][f'ch{c+1}_pos']))
            assert int(completed['channels'][c]['scale'])==int(u[f'ch{c+1}_scale'])==scale
            assert int(completed['channels'][c]['position'])==int(u[f'ch{c+1}_pos'])==position
        assert result['id']==u['display_id']==adc['display_id']
        assert result['crc32'].upper()==adc['display_crc'].upper()
        out=checked_bytes(directory/(result['name']+'.bin'),result['crc32'],result['sha256'],16384)
        out_words=struct.unpack('<4096I',out)
        check_values(out_words,result['rate'],after['measurement'])
        check_designated(out_words,result,TIMES)
        out_tone=known_tone_fit([(w>>16)&1023 for w in out_words],result['rate'])
        checked_bytes(directory/(result['name']+'-frame.bin'),result['frame_crc'],result['frame_sha256'],1843200)
        report.append(dict(name=case['name'],plan=plan,source_crc=source['crc32'],result_crc=result['crc32'],tone=out_tone))
    canceled=s['autoset_cancel'];restored(canceled['before'],canceled['after'],2)
    assert canceled['before']['ui']['single_complete']==canceled['after']['ui']['single_complete']=='1'
    assert canceled['after']['auto']['context']['state']=='8'
    empty=s['autoset_empty'];restored(empty['before'],empty['after'],0)
    assert empty['before']['ui']['source']=='ADC1' and empty['after']['auto']['context']['state']=='4'
    controls={e['test']:e for e in s['controls']}
    manual=controls['autoset-does-not-track-after-manual-adjustment']
    assert manual['ui']['ch2_scale']=='3' and manual['ui']['ch2_pos']=='1000'
    assert s['autoset_final']['context']['state']=='2' and s['autoset_final']['context']['running']=='1'
    final=controls['final-autoset-complete-CH2-10us-auto-running']
    assert final['ui']['time']=='6' and final['ui']['triggered']==final['ui']['running']=='1'
    assert final['adc']['display_rate']=='6250000' and final['adc']['display_epoch']==final['ui']['epoch']
    return dict(state='verified-independent-autoset-board-evidence',cases=report,raw_records=4,ddr_frames=2,
                cancel_scope=canceled['scope'],voltage_calibrated=False,physical_feedback='pending-current-autoset')


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('run',type=Path);directory=p.parse_args().run
    result=audit(directory)
    (directory/'autoset-evidence-audit.json').write_text(json.dumps(result,indent=2)+'\n')
    print('AUTOSET_EVIDENCE_PASS',json.dumps(result))
