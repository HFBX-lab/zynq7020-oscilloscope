"""Independent offline audit of engineering-calibrated M7 board evidence.

This module has no serial, JTAG, socket, or source-control operations. Voltage
reference is the generator setpoint; a numerical match is not traceable metrology.
"""
from pathlib import Path
import argparse
import hashlib
import json
import math
import re
import struct
import zlib

import numpy as np

from spectrum_reference import _words
from timing_reference import timing_reference
from record_blob import parse_record
from audit_spectrum_evidence import metadata_from_fft,check_plot,_raster

PROFILE_NAMES=('instrument','record','spectrum')
COEFFICIENT_FIELDS=('slope1_bits','offset1_bits','slope2_bits','offset2_bits')
STATUS_FIELDS=('display_id','epoch','instrument_flags','instrument_id','record_flags',
               'record_id','spectrum_valid','spectrum_flags','spectrum_id')
SLOPE_MIN,SLOPE_MAX=(struct.unpack('<f',struct.pack('<f',v))[0] for v in (.005,.015))
DIVISIONS=(1e-7,2e-7,5e-7,1e-6,2e-6,5e-6,1e-5,2e-5,5e-5,1e-4,2e-4,
           5e-4,.001,.002,.005,.01,.02,.05,.1,.2,.5)
EXPECTED_LONG=tuple(f'cal-dc-{point}-{rate}' for point in ('n1p5','n0p5','zero-a','p0p5','p1p5','zero-b')
                    for rate in ('raw50m','cic6m25'))+tuple(
    f'cal-ac-50k-{point}-{rate}' for point in ('0p2','1','4') for rate in ('raw50m','cic6m25'))+(
    'cal-freq-5m-raw50m','cal-trigger-rise','cal-trigger-fall','cal-frozen-cic','memory-live-before','cal-autoset')
EXPECTED_MEMORY=('memory-cal-saved','memory-cal-replay','memory-cal-return')
EXPECTED_SPECTRUM=('fft-cal-hann','fft-cal-rect','fft-cal-mean-off','fft-cal-empty','fft-cal-replay')
EXPECTED_CONTROLS=('cal-frozen-view-operations','cal-memory-v2-replay-original-rate','cal-return-live-profile',
                   'cal-parameter-default-restore-profile','cal-autoset-from-frozen','cal-stability-32s')
EXPECTED_FRAMES=('cal-dc-zero-a-cic6m25','cal-ac-50k-1-cic6m25','cal-freq-5m-raw50m',
    'cal-trigger-rise','cal-trigger-fall','cal-frozen-cic','memory-live-before','cal-autoset')+EXPECTED_MEMORY+EXPECTED_SPECTRUM


def require(value,message):
    if not value:
        raise ValueError(message)


def load(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def hex32(value):
    require(isinstance(value,str) and re.fullmatch('[0-9a-fA-F]{8}',value),'Expected exactly eight hex digits')
    return int(value,16)


def valid_profile(profile):
    try:
        flags,identity=profile['flags'],profile['id']
        if type(flags) is not int or type(identity) is not int or not 0<=identity<=0xffffffff:
            return False
        mask=flags&3
        if flags&~0x103 or (mask and not flags&0x100) or (not mask and (flags or identity)):
            return False
        slopes,offsets=profile['slope_v_per_code'],profile['offset_v']
        if len(slopes)!=2 or len(offsets)!=2:
            return False
        for ch,(slope,offset) in enumerate(zip(slopes,offsets)):
            if not math.isfinite(slope) or not math.isfinite(offset):
                return False
            if mask&(1<<ch):
                if not SLOPE_MIN<=slope<=SLOPE_MAX or not 256<=-offset/slope<=768:
                    return False
                if struct.unpack('<2f',struct.pack('<2f',slope,offset))!=(slope,offset):
                    return False
            elif slope!=0 or offset!=0:
                return False
        return True
    except (KeyError,TypeError,ValueError,OverflowError,ZeroDivisionError):
        return False


def coefficients(profile,channel):
    require(valid_profile(profile) and channel in (0,1),'Invalid calibration/channel')
    if profile['flags']&(1<<channel):
        return profile['slope_v_per_code'][channel],profile['offset_v'][channel]
    return 10/1023,-5


def trigger_q8(profile,channel,level_mv):
    require(type(level_mv) is int and -5000<=level_mv<=5000,'Trigger outside supported physical range')
    slope,offset=coefficients(profile,channel)
    if not profile['flags']&(1<<channel):
        return ((level_mv+5000)*1023*256+5000)//10000
    code=max(0,min(1023,(level_mv/1000-offset)/slope))
    return math.floor(code*256+.5)


def profile_manifest(path,verify_sources=True):
    manifest=load(path)
    binding=manifest['binding']
    encoded=json.dumps(binding,ensure_ascii=True,sort_keys=True,separators=(',',':')).encode()
    require(hashlib.sha256(encoded).hexdigest()==manifest['binding_sha256'],'Profile binding SHA mismatch')
    require(zlib.crc32(encoded)==manifest['id'],'Profile binding CRC identity mismatch')
    require(manifest['flags']==0x103 and binding['schema']==1,'Unsupported engineering profile')
    require(binding['reference']=='SDG2122X-CH2-generator-setpoints-no-independent-voltage-reference',
            'Unexpected calibration reference policy')
    channels=binding['channels']
    require([c['channel'] for c in channels]==[1,2],'Calibration channel mapping mismatch')
    result=dict(flags=manifest['flags'],id=manifest['id'],
                slope_v_per_code=[c['slope_v_per_code'] for c in channels],
                offset_v=[c['offset_v'] for c in channels])
    require(valid_profile(result),'Invalid manifest coefficients')
    for ch in channels:
        require(struct.pack('<2f',ch['slope_v_per_code'],ch['offset_v']).hex()==ch['float32_le_hex'],
                'Manifest float32 bits disagree')
        if verify_sources:
            run=Path(ch['run'])
            for filename,key in [('session.json','session_sha256'),('characterization-analysis.json','analysis_sha256')]:
                require(hashlib.sha256((run/filename).read_bytes()).hexdigest()==ch[key],
                        'Manifest source evidence SHA mismatch: '+filename)
    return result


def calibration_profiles(state,expected):
    h,rows=state['status'],state['profiles']
    require(set(h)==set(STATUS_FIELDS),'Calibration diagnostic schema mismatch')
    require(set(rows)==set(PROFILE_NAMES),'Missing calibration profile diagnostic')
    require(h['spectrum_valid'] in ('0','1'),'Invalid spectrum-valid state')
    result={}
    for name in PROFILE_NAMES:
        row=rows[name]
        require(set(row)=={'profile',*COEFFICIENT_FIELDS} and row['profile']==name,'Wrong calibration coefficient row')
        values=[struct.unpack('<f',struct.pack('<I',hex32(row[k])))[0] for k in COEFFICIENT_FIELDS]
        p=dict(flags=int(h[name+'_flags']),id=hex32(h[name+'_id']),
               slope_v_per_code=[values[0],values[2]],offset_v=[values[1],values[3]])
        require(valid_profile(p),'Invalid diagnostic profile: '+name)
        require(p==expected or p==dict(flags=0,id=0,slope_v_per_code=[0,0],offset_v=[0,0]),
                'Diagnostic coefficients have unknown identity')
        result[name]=p
    require(result['instrument']==expected,'Instrument profile does not match audited source binding')
    return result


def frequency_estimate(codes,rate):
    """Confirmed raw-code midpoint crossings; voltage mapping cannot alter time."""
    bottom,top=int(min(codes)),int(max(codes));span=top-bottom
    if span<16:
        return None
    mean=math.fsum(map(float,codes))/len(codes)
    hysteresis=max(4,span*.1);armed=False;pending=None;crossings=[]
    for i,(a,b) in enumerate(zip(codes[:-1],codes[1:]),1):
        a,b=float(a),float(b)
        if a<=mean-hysteresis or b<=mean-hysteresis:
            armed=True;pending=None
        if armed and a<=mean<b:
            pending=i-1+(mean-a)/(b-a)
        if armed and pending is not None and b>=mean+hysteresis:
            crossings.append(pending);armed=False;pending=None
    if len(crossings)<3:
        return None
    periods=np.diff(crossings);period=math.fsum(map(float,periods))/len(periods)
    if min(periods)<4 or min(periods)<.8*period or max(periods)>1.2*period:
        return None
    return rate/period


def expected_measurements(words,rate,profile):
    words=_words(words);channels=[];timing=[]
    require(type(rate) is int and rate>0,'Invalid measurement rate')
    for ch in (0,1):
        codes=((words>>(16*ch))&1023).astype(np.float64)
        slope,offset=coefficients(profile,ch);voltage=codes*slope+offset
        mean=math.fsum(map(float,voltage))/4096
        overrange=bool(np.any((codes==0)|(codes==1023)) or np.any(words&(1<<(16*ch+10))))
        frequency=frequency_estimate(codes,rate)
        valid=not overrange and frequency is not None and 2*rate/4095<=frequency<=rate/4
        values=dict(ch=str(ch+1),valid='1',overrange=str(int(overrange)),min_v=float(min(voltage)),
                    max_v=float(max(voltage)),mean_v=mean,vpp_v=float(max(voltage)-min(voltage)),
                    rms_total_v=math.sqrt(math.fsum(float(v)*float(v) for v in voltage)/4096),
                    rms_ac_v=math.sqrt(math.fsum((float(v)-mean)**2 for v in voltage)/4096),
                    frequency_valid=str(int(valid)),frequency_hz=frequency if valid else 0.,
                    period_us=1e6/frequency if valid else 0.)
        channels.append({k:(f'{v:.6f}' if isinstance(v,float) else v) for k,v in values.items()})
        t=timing_reference(words.tolist(),rate,ch)
        if max(codes)-min(codes)>=16 and not overrange:
            for key,fraction in [('low_v',.1),('mid_v',.5),('high_v',.9)]:
                t[key]=(min(codes)+(max(codes)-min(codes))*fraction)*slope+offset
        t['ch']=str(ch+1)
        timing.append({k:(str(int(v)) if isinstance(v,bool) else f'{v:.6f}' if isinstance(v,float) else str(v))
                       for k,v in t.items()})
    return dict(channels=channels,timing=timing)


def check_measurements(words,rate,measurement,profile,require_timing=False):
    expected=expected_measurements(words,rate,profile)
    require(len(measurement['channels'])==2,'Incomplete measurement channels')
    for ch,(actual,wanted) in enumerate(zip(measurement['channels'],expected['channels'])):
        require(set(actual)==set(wanted),'Measurement channel schema mismatch')
        for key in ('ch','valid','overrange','frequency_valid'):
            require(actual[key]==wanted[key],'Measurement validity mismatch: '+key)
        for key in ('min_v','max_v','mean_v','vpp_v','rms_total_v','rms_ac_v','frequency_hz','period_us'):
            value=float(actual[key]);target=float(wanted[key])
            require(math.isfinite(value) and abs(value-target)<=1.1e-6,
                    f'Calibrated measurement mismatch: CH{ch+1}/{key}')
    if require_timing:
        require('timing' in measurement,'No saved timing values: cannot verify timing voltage references')
    if 'timing' in measurement:
        require(len(measurement['timing'])==2,'Incomplete timing channels')
        for ch,(actual,wanted) in enumerate(zip(measurement['timing'],expected['timing'])):
            require(set(actual)==set(wanted),'Timing channel schema mismatch')
            for key in wanted:
                if key in ('ch','pulse_valid','rise_valid','fall_valid','cycles'):
                    require(actual[key]==wanted[key],'Timing validity mismatch: '+key)
                else:
                    value=float(actual[key]);target=float(wanted[key])
                    require(math.isfinite(value) and abs(value-target)<=1.1e-6+abs(target)*2e-12,
                            f'Calibrated timing mismatch: CH{ch+1}/{key}')
    return expected['channels']


def freeze(fields):
    return tuple(sorted(fields.items()))


def fields(line):
    pairs=re.findall(r'(\w+)=([^\s]+)',line)
    require(len({k for k,_ in pairs})==len(pairs),'Duplicate UART fields')
    return dict(pairs)


def measurement_identity(measurement,with_timing):
    base=(freeze(measurement['context']),tuple(freeze(row) for row in measurement['channels']))
    return base+(tuple(freeze(row) for row in measurement['timing']),) if with_timing else base


def calibration_identity(state):
    return freeze(state['status']),tuple(freeze(state['profiles'][name]) for name in PROFILE_NAMES)


def uart_provenance(log):
    require(not re.search(r'APP_FATAL|APP_LOG_ERROR|ADC_LIVE_FAIL|DDR_FAIL|VIDEO_PRESENT_FAIL|VIDEO_STOP_TIMEOUT',log),
            'Application/hardware failure in UART evidence')
    require(not re.search(r'APP_DIAG[^\r\n]*dma_errors=(?!00000000)[0-9a-fA-F]{8}',log),
            'Video DMA error in UART evidence')
    out=dict(pairs=set(),measurements=set(),timing=set(),calibrations=set(),memory=set(),fft=set(),parameters=set(),autosets=set(),
             snapshots=[],index={},pair_positions={},diagnostics=[])
    ui=adc=cal=measurement=memory=fft=parameter=autoset=None
    last=dict(pair=None,calibration=None,memory=None,fft=None)
    for position,line in enumerate(log.splitlines()):
        prefix=line.split(' ',1)[0];row=fields(line)
        if row:
            out['index'].setdefault(prefix,set()).add(freeze(row))
        if line.startswith('COMMANDS:'):
            if ui is not None and adc is not None:
                last['pair']=(freeze(ui),freeze(adc));out['pairs'].add(last['pair'])
                out['pair_positions'].setdefault(last['pair'],[]).append(position)
            ui=adc=None
        elif prefix=='UI_STATUS':ui=row
        elif prefix=='ADC_LIVE_STATUS':adc=row
        elif prefix=='CALIBRATION_STATUS':cal=dict(status=row,profiles={})
        elif prefix=='CALIBRATION_COEFFICIENTS' and cal is not None:
            name=row.get('profile');require(name in PROFILE_NAMES and name not in cal['profiles'],
                                          'Unexpected/duplicate calibration profile row')
            cal['profiles'][name]=row
        elif prefix=='CALIBRATION_END' and cal is not None:
            require(row=={k:cal['status'][k] for k in ('display_id','epoch')},'Mixed calibration UART frame')
            require(tuple(cal['profiles'])==PROFILE_NAMES,'Incomplete/unordered calibration UART frame')
            last['calibration']=calibration_identity(cal);out['calibrations'].add(last['calibration']);cal=None
        elif prefix=='MEASURE_STATUS':measurement=dict(context=row,channels=[],timing=[])
        elif prefix=='MEASURE_CHANNEL' and measurement is not None:measurement['channels'].append(row)
        elif prefix=='MEASURE_TIMING' and measurement is not None:measurement['timing'].append(row)
        elif prefix=='MEASURE_END' and measurement is not None:
            require(row=={'display_id':measurement['context']['display_id']},'Mixed measurement UART frame')
            require([r.get('ch') for r in measurement['channels']]==['1','2'],'Incomplete/unordered measurement UART frame')
            out['measurements'].add(measurement_identity(measurement,False))
            if measurement['timing']:
                require([r.get('ch') for r in measurement['timing']]==['1','2'],'Incomplete/unordered timing UART frame')
                out['timing'].add(measurement_identity(measurement,True))
            measurement=None
        elif prefix=='MEMORY_STATUS':memory=dict(status=row)
        elif prefix=='MEMORY_VIEW' and memory is not None:memory['view']=row
        elif prefix=='MEMORY_END' and memory is not None:
            require('view' in memory and row=={'display_id':memory['view']['display_id']},'Mixed memory UART frame')
            last['memory']=(freeze(memory['status']),freeze(memory['view']))
            out['memory'].add(last['memory']);memory=None
        elif prefix=='FFT_STATUS':fft=dict(status=row)
        elif prefix=='FFT_VALUES' and fft is not None:fft['values']=row
        elif prefix=='FFT_END' and fft is not None:
            require('values' in fft and row=={'seq':fft['status']['seq']},'Mixed FFT UART frame')
            last['fft']=(freeze(fft['status']),freeze(fft['values']));out['fft'].add(last['fft']);fft=None
        elif prefix=='PARAM_STATUS':parameter=dict(status=row)
        elif prefix=='PARAM_CONFIG' and parameter is not None:parameter['config']=row
        elif prefix=='PARAM_END' and parameter is not None:
            require('config' in parameter and row=={'epoch':parameter['status']['epoch']},'Mixed parameter UART frame')
            out['parameters'].add((freeze(parameter['status']),freeze(parameter['config'])));parameter=None
        elif prefix=='AUTOSET_STATUS':autoset=dict(status=row,channels=[])
        elif prefix=='AUTOSET_CHANNEL' and autoset is not None:autoset['channels'].append(row)
        elif prefix=='AUTOSET_END' and autoset is not None:
            require(row=={'epoch':autoset['status']['epoch']} and [r.get('ch') for r in autoset['channels']]==['1','2'],
                    'Mixed/incomplete autoset UART frame')
            out['autosets'].add((freeze(autoset['status']),tuple(freeze(r) for r in autoset['channels'])));autoset=None
        elif prefix=='SNAPSHOT_READY':
            out['snapshots'].append(dict(last,fields=row))
        elif prefix=='APP_DIAG':out['diagnostics'].append((position,row))
    return out


def bind_calibration(state,provenance):
    require(calibration_identity(state) in provenance['calibrations'],
            'Calibration values are not one actual complete UART transaction')


def bind_acquisition(entry,provenance,require_timing=False):
    pair=(freeze(entry['ui']),freeze(entry['adc']))
    require(pair in provenance['pairs'],'Acquisition values are not one actual framed UART query')
    measured=entry['measurement'];ui=entry['ui'];adc=entry['adc'];h=measured['context']
    require(measurement_identity(measured,False) in provenance['measurements'],
            'Measurement values are not one actual complete UART transaction')
    if require_timing:
        require('timing' in measured and measurement_identity(measured,True) in provenance['timing'],
                'Timing/basic values are not the same actual UART transaction')
    bind_calibration(entry['calibration'],provenance)
    c=entry['calibration']['status']
    require(str(entry['id'])==ui['display_id']==adc['display_id']==h['display_id']==c['display_id'],
            'Raw/measurement/calibration identify different records')
    require(ui['epoch']==h['epoch']==c['epoch'],'Frozen diagnostic epochs disagree')
    require(ui['running']==h['running']=='0' and adc['pending']=='0','Export is not canceled/frozen')
    require(int(entry['rate'])==int(adc['display_rate'])==int(h['rate']) and h['count']=='4096',
            'Raw/measurement original rate disagrees')


def checked_bytes(path,crc,digest,size):
    data=Path(path).read_bytes()
    require(len(data)==size,'Wrong evidence byte count: '+str(path))
    require(hex32(crc)==zlib.crc32(data),'Evidence CRC mismatch: '+str(path))
    require(isinstance(digest,str) and re.fullmatch('[0-9a-fA-F]{64}',digest) and
            hashlib.sha256(data).hexdigest()==digest.lower(),'Evidence SHA mismatch: '+str(path))
    return data


def export_operations(directory,name,marker):
    require(re.fullmatch('[a-z0-9-]+',name),'Unsafe evidence basename')
    text=(directory/('export-'+name+'.tcl')).read_text(encoding='utf-8-sig')
    operations=re.findall(r'(?m)^mrd\s+-bin\s+-file\s+\{([^}]+)\}\s+0x([0-9a-fA-F]{8})\s+(\d+)\s*$',text)
    require(len(operations)==len(re.findall(r'(?m)^mrd\b',text)),'Unparsed Tcl export operation')
    log=(directory/('export-'+name+'.log')).read_text(encoding='utf-8-sig')
    require(re.search(r'(?m)^'+re.escape(marker)+r'\s*$',log) and 'EXPORT_ERROR:' not in log,
            'Missing successful actual Tcl export marker')
    out={}
    for path,address,count in operations:
        resolved=Path(path.replace('\\','/')).resolve()
        require(resolved.parent==directory.resolve() and resolved.name not in out,'Tcl path outside run/duplicate')
        out[resolved.name]=(hex32(address),int(count))
    return out


def bind_frame(directory,entry,operations,provenance,context_name,context):
    name=entry['name'];frame=checked_bytes(directory/(name+'-frame.bin'),entry['frame_crc'],entry['frame_sha256'],1843200)
    address,count=operations[name+'-frame.bin']
    require(address in (0x10000000,0x10200000) and count==460800,'Wrong framebuffer Tcl address/length')
    matching=[s for s in provenance['snapshots'] if s[context_name]==context and
        s['fields'].get('addr','').lower()==f'{address:08x}' and s['fields'].get('bytes')=='1843200' and
        s['fields'].get('format')=='BGR888' and s['fields'].get('crc','').lower()==entry['frame_crc'].lower()]
    require(matching,'Frame is not bound to this source context and actual SNAPSHOT_READY/Tcl')
    return frame


def visible_span(ui,rate):
    index=int(ui['time']);require(0<=index<len(DIVISIONS),'Unsupported physical timebase')
    return 10*DIVISIONS[index]*rate


def check_trigger(words,entry,profile):
    words=_words(words);ui,adc=entry['ui'],entry['adc'];rate=int(entry['rate'])
    require(adc['id']==adc['display_id']==ui['display_id'],'Frozen raw identity does not identify trigger source')
    seen,ring=int(adc['hw_seen']),int(adc['hw_start'])
    require(seen>=4096 and 0<=ring<4096,'Invalid hardware ring progress')
    if seen!=0xffffffff:
        require(ring==seen%4096,'Hardware ring origin disagrees with progress')
    span=visible_span(ui,rate);start=float(ui['view_start']);cross=float(ui['crossing'])
    require(all(map(math.isfinite,(span,start,cross))) and start>=0 and start+span<=4095.004,
            'Invalid captured trigger viewport')
    require(adc['hw_matched'] in ('0','1') and adc['hw_auto'] in ('0','1'), 'Invalid hardware trigger flags')
    require(adc['hw_matched']!=adc['hw_auto'],'Ambiguous hardware matched/auto source')
    if adc['hw_matched']=='0':
        require(ui['triggered']=='0' and adc['hw_after']=='0' and cross==start==0,
                'AUTO record invented a designated trigger')
        require(ui['single_complete']=='0','No-match single acquisition completed')
        return dict(matched=False,start=0.,span=span)
    require(ui['triggered']=='1','Hardware match is not the displayed trigger')
    after=int(adc['hw_after']);require(after==math.ceil(.2*span)+1 and 1<=after<4096,
                                      'Designated hardware pair does not match requested window')
    channel=int(ui['trig_channel'])-1;require(channel in (0,1),'Invalid trigger channel')
    require(not (int(words[after-1])|int(words[after]))&(1<<(channel*16+10)), 'OTR on designated pair')
    a,b=((int(words[i])>>(channel*16))&1023 for i in (after-1,after))
    level_q8=trigger_q8(profile,channel,int(ui['level_mv']));level=level_q8/256
    require(ui['trig_rising'] in ('0','1'),'Invalid trigger direction')
    require(a<=level<b if ui['trig_rising']=='1' else a>=level>b,
            'Designated pair disagrees with calibrated physical trigger level')
    actual=after-1+(level-a)/(b-a);actual_start=actual-.2*span
    require(abs(cross-actual)<=.001 and abs(start-actual_start)<=.001 and abs(cross-start-.2*span)<=.004,
            'Calibrated designated crossing/window alignment mismatch')
    if ui['single_complete']=='1':
        require(adc['display_epoch']==ui['epoch'],'Single record has another acquisition epoch')
    return dict(matched=True,level_q8=level_q8,crossing=actual,start=actual_start,span=span,after=after)


def check_cursor(words,rate,ui,profile,start=None):
    words=_words(words)
    for key in ('cursor_dt_us','cursor_dv','cursor_inv_hz'):
        require(math.isfinite(float(ui[key])),'Nonfinite cursor diagnostic')
    if ui['cursors']=='0' or ui.get('bars','0')=='1':
        require(ui['cursor_valid']=='0' and all(float(ui[k])==0 for k in ('cursor_dt_us','cursor_dv','cursor_inv_hz')),
                'Hidden cursor has active/calculated readings')
        return dict(visible=False)
    require(ui['cursors']=='1' and ui['source'] in ('ADC1','ADC2'),'Invalid cursor source')
    ch=int(ui['source'][-1])-1;slope,offset=coefficients(profile,ch);span=visible_span(ui,rate)
    if start is None:start=float(ui['view_start'])
    positions=[int(ui[k]) for k in ('cursor_a','cursor_b')]
    require(all(0<=p<=1000 for p in positions),'Cursor fraction outside viewport')
    times=[span*p/1000/rate for p in positions];delta=times[1]-times[0]
    require(abs(float(ui['cursor_dt_us'])-delta*1e6)<=.00000055,'Cursor original-rate time mismatch')
    inverse=1/abs(delta) if delta else 0
    require(abs(float(ui['cursor_inv_hz'])-inverse)<=.00000055+abs(inverse)*1e-12,'Cursor reciprocal mismatch')
    volts=[]
    for p in positions:
        at=start+span*p/1000
        if at<0 or at>4095:
            volts.append(None);continue
        lo=int(at);fraction=at-lo;a=(int(words[lo])>>(ch*16))&2047;b=a
        if fraction>0:b=(int(words[lo+1])>>(ch*16))&2047
        volts.append(None if (a|b)&1024 else (a+fraction*(b-a))*slope+offset)
    valid=all(v is not None for v in volts)
    require(ui['cursor_valid']==str(int(valid)),'Cursor point validity mismatch')
    expected=volts[1]-volts[0] if valid else 0
    require(abs(float(ui['cursor_dv'])-expected)<=.00000055,'Cursor calibrated voltage delta mismatch')
    return dict(visible=True,valid=valid,delta_v=expected,delta_us=delta*1e6)


def source_check(source,point=None,require_on=True):
    require(source['vendor']=='Siglent Technologies' and source['model']=='SDG2122X','Unexpected source vendor/model')
    identity=[v.strip() for v in source['raw_idn'].split(',')]
    require(len(identity)==4 and all(identity) and identity[:2]==[source['vendor'],source['model']],
            'Raw source identity differs from parsed vendor/model')
    require(type(source['output_on']) is bool and (not require_on or source['output_on']), 'Source output is not enabled')
    require(source['load']=='HZ' and source['polarity']=='NOR','Source is not HiZ/normal polarity')
    waveform=source['waveform'];offset=source['offset_v']
    require(waveform in ('DC','SINE') and math.isfinite(offset),'Invalid source waveform/offset')
    def pairs(raw,prefix):
        require(raw.startswith(prefix),'Unexpected raw source command/channel')
        parts=raw[len(prefix):].split(',')
        require(len(parts)%2==0 and len(set(parts[::2]))==len(parts)//2,'Malformed/duplicate source fields')
        return dict(zip(parts[::2],parts[1::2]))
    def number(text,unit):
        require(text.endswith(unit),'Missing source response units')
        value=float(text[:-len(unit)]);require(math.isfinite(value),'Nonfinite raw source value')
        return value
    raw=pairs(source['raw_bswv'],'C2:BSWV ')
    require(raw['WVTP']==waveform and number(raw['OFST'],'V')==offset,'Raw/parsed source waveform/offset disagree')
    if waveform=='SINE':
        require(all(math.isfinite(source[k]) for k in ('frequency_hz','vpp','phase_deg')),'Nonfinite source point')
        require(1<=source['frequency_hz']<=10000000 and .2<=source['vpp']<=4 and
                abs(offset)+source['vpp']/2<=2.5 and 0<=source['phase_deg']<=360,'Source point exceeds authorized bounds')
        require(number(raw['FRQ'],'HZ')==source['frequency_hz'] and number(raw['AMP'],'V')==source['vpp'] and
                float(raw['PHSE'])==source['phase_deg'],'Raw/parsed sine point disagree')
    else:require(abs(offset)<=2,'DC point exceeds authorized bounds')
    out=pairs(source['raw_outp'],'C2:OUTP '+('ON' if source['output_on'] else 'OFF')+',')
    require(out['LOAD']=='HZ' and out['PLRT']=='NOR','Raw source load/polarity disagree')
    require(set(source['modes'])=={'MDWV','SWWV','BTWV'},'Incomplete source mode checks')
    for mode,value in source['modes'].items():
        require(value==dict(state='OFF',raw=f'C2:{mode} STATE,OFF'),'Active/unknown source mode')
    coupling=source['coupling']['fields']
    require(all(coupling.get(k)=='OFF' for k in ('TRACE','FCOUP','PCOUP','ACOUP')) and
            all(v=='OFF' for v in coupling.values()) and pairs(source['coupling']['raw'],'COUP ')==coupling,
            'Active/unknown/mismatched source coupling')
    if point is not None:
        require(point['waveform']==waveform and point['offset_v']==offset,'Source readback differs from requested point')
        if waveform=='SINE':
            require(point['frequency_hz']==source['frequency_hz'] and point['vpp']==source['vpp'],
                    'Sine readback differs from requested point')
    return source


def run_json(directory,path):
    path=Path(path)
    if not path.is_absolute():path=directory/path
    require(path.resolve().parent==directory.resolve() and path.suffix=='.json','Source JSON outside evidence run')
    return load(path)


def check_recipe(directory,groups):
    expected=[]
    for name,value in [('n1p5',-1.5),('n0p5',-.5),('zero-a',0),('p0p5',.5),('p1p5',1.5),('zero-b',0)]:
        expected.append(dict(name='cal-dc-'+name,waveform='DC',offset_v=value,rates=[50000000,6250000],reference_policy='generator-setpoints'))
    for name,value in [('0p2',.2),('1',1),('4',4)]:
        expected.append(dict(name='cal-ac-50k-'+name,waveform='SINE',frequency_hz=50000,vpp=value,offset_v=0,
                             rates=[50000000,6250000],reference_policy='generator-setpoints'))
    expected.append(dict(name='cal-freq-5m',waveform='SINE',frequency_hz=5000000,vpp=1,offset_v=0,
                         rates=[50000000],reference_policy='generator-setpoints'))
    require(load(directory/'validation-points.json')==expected,'Validation source setpoints/rates differ from approved recipe')
    require(len(groups)==11 and [g['point'] for g in groups[:10]]==expected,'Point-source groups differ from approved recipe')
    for group,point in zip(groups,expected):
        require(load(directory/(point['name']+'-request.json'))==point,'Saved source request differs from approved point')
        names=[point['name']+('-raw50m' if rate==50000000 else '-cic6m25') for rate in point['rates']]
        require(group['records']==names,'Point record rate/name plan mismatch')
    baseline=dict(name='cal-regression-baseline',waveform='SINE',frequency_hz=50000,vpp=1,offset_v=0,reference_policy='generator-setpoints')
    require(groups[-1]['point']==baseline and tuple(groups[-1]['records'])==EXPECTED_LONG[-5:],
            'Regression records do not share the approved baseline source')
    require(load(directory/'cal-regression-baseline-request.json')==baseline,'Saved regression source request changed')


def check_source_points(directory,session):
    original=source_check(load(directory/'source-original.json'),require_on=False)
    restored=source_check(session['source_restored'],require_on=False)
    require(original==restored==load(directory/'source-restored.json'),'Original generator state was not restored exactly')
    points=session['points'];require(points,'No source point evidence')
    mapping={};point_names=[]
    for group in points:
        point=group['point'];name=point['name'];point_names.append(name)
        source=source_check(group['source_before'],point)
        after=source_check(group['source_after'],point)
        require(source==after and source['raw_idn']==original['raw_idn'],'Source settings/identity changed during capture')
        require(source==run_json(directory,group['source_file']),'Source metadata does not match saved SCPI readback')
        after_file=group.get('source_after_file',name+'-source-after.json')
        require(after==run_json(directory,after_file),'Source after metadata does not match saved SCPI readback')
        require(group['records'] and len(group['records'])==len(set(group['records'])),'Empty/duplicate source record binding')
        for record in group['records']:
            require(record not in mapping,'Record appears in multiple source points')
            mapping[record]=group
    require(len(point_names)==len(set(point_names)),'Duplicate source point names')
    require(set(mapping)=={entry['name'] for entry in session['long_records']},'Source point bindings do not cover exactly the planned raw records')
    return mapping


def audit_long_record(directory,entry,provenance,expected_profile,source_group,adc_input,has_frame):
    name=entry['name'];bind_acquisition(entry,provenance,require_timing=True)
    profiles=calibration_profiles(entry['calibration'],expected_profile);profile=profiles['record']
    require(profile==expected_profile,'New live record was not calibrated')
    require(entry['point']==source_group['point'] and entry['source_file']==source_group['source_file'] and
            entry['source']==source_group['source_before'],'Raw record uses another source point/readback')
    rate=int(entry['rate']);ui,adc=entry['ui'],entry['adc']
    require(rate in (50000000,6250000) and adc['available']=='1' and adc['bytes']=='16384','Unexpected M7 raw dimensions')
    require(int(adc['display_conversion_hz'])==50000000 and int(adc['display_filter'])==(0 if rate==50000000 else 3),
            'Wrong original ADC conversion/filter metadata')
    require(entry['crc32'].lower()==adc['display_crc'].lower(),'Raw CRC differs from displayed record')
    require(ui['source'] in ('ADC1','ADC2') and entry['measurement']['context']['channel']==ui['source'][-1],
            'Measurement does not identify selected source')
    raw=checked_bytes(directory/(name+'.bin'),entry['crc32'],entry['sha256'],16384);words=_words(raw)
    checks=check_measurements(words,rate,entry['measurement'],profile,require_timing=True)
    trigger=check_trigger(words,entry,profile);cursor=check_cursor(words,rate,ui,profile,trigger['start'])
    require(('frame_crc' in entry)==has_frame and ('frame_sha256' in entry)==has_frame,'Missing/unplanned raw frame')
    operations=export_operations(directory,name,'LONG_RECORD_EXPORTED')
    expected_names={name+'.bin'}|({name+'-frame.bin'} if has_frame else set())
    require(set(operations)==expected_names,'Unexpected raw export operations')
    require(operations[name+'.bin']==(hex32(adc['display_address']),4096) and
            hex32(adc['display_address']) in (0x13000000,0x13004000),'Wrong captured DDR export address/length')
    if has_frame:bind_frame(directory,entry,operations,provenance,'pair',(freeze(ui),freeze(adc)))
    ch=adc_input-1;codes=((words>>(16*ch))&1023).astype(np.float64);slope,offset=coefficients(profile,ch)
    require(not bool(np.any((codes==0)|(codes==1023))) and not bool(np.any(words&(1<<(16*ch+10)))),
            'Characterization has clipped/OTR input')
    point=entry['point'];values=dict(name=name,rate=rate,id=int(entry['id']),trigger=trigger,cursor=cursor,
                                  expected_channels=checks,sha256=entry['sha256'])
    if point['waveform']=='DC':
        error=float(np.mean(codes))*slope+offset-point['offset_v']
        require(abs(error)<=.020,'Calibrated DC point exceeds 20 mV relative to source setpoint')
        values['source_setpoint_error_v']=error
    else:
        t=np.arange(4096)/rate;phase=2*np.pi*point['frequency_hz']*t
        basis=np.column_stack((np.ones(4096),np.sin(phase),np.cos(phase)))
        fit=np.linalg.lstsq(basis,codes,rcond=None)[0];residual=codes-basis@fit
        vpp=float(2*np.hypot(fit[1],fit[2])*slope)
        require(abs(vpp-point['vpp'])<=.020+.02*point['vpp'],
                'Calibrated sine amplitude exceeds source-setpoint engineering limit (20 mV + 2%)')
        require(float(np.max(np.abs(residual)))<32,'Gross digital/analog sample discontinuity')
        values.update(fitted_vpp=vpp,fitted_dc_v=float(fit[0]*slope+offset),
                      source_setpoint_error_percent=(vpp/point['vpp']-1)*100,
                      residual_peak_code=float(np.max(np.abs(residual))))
    return values


def expected_exports(directory,session):
    plan=load(directory/'expected-exports.json')
    declared=dict(session['expected_exports']);frames=session['expected_frames']
    require(load(directory/'expected-frames.json')==frames,'Saved/requested frame plan mismatch')
    require(plan==declared,'Saved/requested export plan mismatch')
    for key,wanted in [('long',EXPECTED_LONG),('memory',EXPECTED_MEMORY),('spectrum',EXPECTED_SPECTRUM),('controls',EXPECTED_CONTROLS)]:
        require(tuple(declared[key])==wanted,'Unexpected or incomplete M7 export recipe: '+key)
    require(tuple(frames)==EXPECTED_FRAMES,'Unexpected or incomplete M7 frame recipe')
    for key,wanted in [('long_records',EXPECTED_LONG),('memory_exports',EXPECTED_MEMORY),('spectrum_exports',EXPECTED_SPECTRUM)]:
        require(tuple(entry['name'] for entry in session[key])==wanted,'Missing, extra or reordered export: '+key)
    require(tuple(entry['name'] for entry in session['control_checks'])==EXPECTED_CONTROLS,
            'Missing, extra or reordered regression control')


def elf_segments(directory,session):
    data=(directory/'application.elf').read_bytes()
    require(hashlib.sha256(data).hexdigest().lower()==session['elf_sha256'].lower(),'Loaded candidate ELF hash mismatch')
    require(data[:6]==b'\x7fELF\x01\x01' and struct.unpack_from('<H',data,18)[0]==40,'Not an ELF32 ARM candidate')
    offset=struct.unpack_from('<I',data,28)[0];size,count=struct.unpack_from('<HH',data,42)
    require(size>=32 and offset+count*size<=len(data),'Invalid ELF program table')
    segments=[]
    for i in range(count):
        kind,_,virtual,physical,_,memory_size,_,_=struct.unpack_from('<8I',data,offset+i*size)
        if kind==1:
            require(virtual==physical and 0x100000<=virtual<virtual+memory_size<=0x1000000,'Candidate load outside audited application DDR')
            segments.append((virtual,virtual+memory_size))
    require(segments,'No candidate load segments')
    proof=load(directory/'validation.json')
    require(proof['elf_sha256'].lower()==session['elf_sha256'].lower() and
            segments==[(int(a,16),int(b,16)) for a,b in proof['load_segments']], 'Build/loaded ELF provenance disagrees')
    return segments


def in_elf(address,size,segments):
    address=hex32(address)
    require(address%4==0 and any(low<=address<address+size<=high for low,high in segments),
            'Diagnostic export outside candidate ELF buffers')
    return address


def audit_user_backup(directory,entry,provenance):
    ui,adc=entry['ui'],entry['adc'];name=entry['name'];rate=int(entry['rate'])
    require((freeze(ui),freeze(adc)) in provenance['pairs'],'User backup does not match a real pre-boot UART query')
    require(str(entry['id'])==ui['display_id']==adc['display_id'] and rate==int(adc['display_rate']),
            'User backup identity/rate mismatch')
    require(ui['running']=='0' and adc['pending']=='0' and adc['available']=='1' and adc['bytes']=='16384',
            'User backup not frozen/complete')
    require(adc['display_crc'].lower()==entry['crc32'].lower(),'User backup raw/display CRC mismatch')
    require(adc['display_conversion_hz']=='50000000' and
            ((adc['display_filter']=='0' and rate==50000000) or
             (adc['display_filter']=='3' and rate in (6250000,625000,62500,6250,625))), 'User backup original filter/rate mismatch')
    raw=checked_bytes(directory/(name+'.bin'),entry['crc32'],entry['sha256'],16384);_words(raw)
    require('frame_crc' in entry and 'frame_sha256' in entry,'User backup framebuffer missing')
    ops=export_operations(directory,name,'LONG_RECORD_EXPORTED')
    require(set(ops)=={name+'.bin',name+'-frame.bin'} and ops[name+'.bin']==(hex32(adc['display_address']),4096) and
            hex32(adc['display_address']) in (0x13000000,0x13004000),'User backup Tcl raw address/length mismatch')
    bind_frame(directory,entry,ops,provenance,'pair',(freeze(ui),freeze(adc)))
    return dict(id=int(entry['id']),rate=rate,sha256=entry['sha256'],state='verified-pre-boot-raw-backup-no-voltage-reinterpretation')


def bind_memory(memory,provenance):
    require((freeze(memory['status']),freeze(memory['view'])) in provenance['memory'],
            'Memory values are not one actual complete UART transaction')


def bind_parameter(parameter,provenance):
    require((freeze(parameter['status']),freeze(parameter['config'])) in provenance['parameters'],
            'Parameter values are not one actual complete UART transaction')


def audit_memory(directory,entry,provenance,expected_profile,segments,long_entries):
    name=entry['name'];bind_acquisition(entry,provenance,require_timing=True);bind_memory(entry['memory'],provenance)
    profiles=calibration_profiles(entry['calibration'],expected_profile);profile=profiles['record']
    require(profile==expected_profile,'M7 memory record lost its voltage profile')
    h,v=entry['memory']['status'],entry['memory']['view'];rate=int(entry['rate'])
    require(h['valid']=='1' and h['blob_bytes']=='16512' and v['record_bytes']=='16384','Invalid memory slot dimensions')
    require(str(entry['id'])==v['display_id'] and rate==int(v['rate']) and v['running']=='0' and
            v['epoch']==entry['ui']['epoch'] and v['actual_conversion']=='50000000', 'Memory display identity/rate mismatch')
    require(entry['blob_crc'].lower()==h['blob_crc'].lower() and entry['record_crc'].lower()==v['record_crc'].lower(),
            'Memory CRC is not from exported UART source')
    blob=checked_bytes(directory/(name+'.scw'),entry['blob_crc'],entry['blob_sha256'],16512)
    record=checked_bytes(directory/(name+'.bin'),entry['record_crc'],entry['record_sha256'],16384)
    parsed=parse_record(blob);meta=parsed['meta'];words=_words(record)
    require(meta['version']==2 and meta['unit_code']==2 and meta['calibration']==expected_profile,'SCW v2 calibration identity/units mismatch')
    for key,field in [('record_id','saved_id'),('rate_hz','saved_rate'),('conversion_hz','saved_conversion'),
                       ('filter_kind','saved_filter'),('capture_epoch','saved_epoch'),('timebase_ns','saved_time_ns'),('flags','saved_flags')]:
        require(meta[key]==int(h[field]),'Saved SCW field differs from actual slot: '+key)
    if name in ('memory-cal-saved','memory-cal-replay'):
        require(record==parsed['raw_data'] and rate==meta['rate_hz'] and int(v['display_epoch'])==meta['capture_epoch'] and
                int(v['display_id'])==meta['record_id'], 'Saved/replayed raw payload or original metadata changed')
    require(h['replay']==v['replay']==('1' if name=='memory-cal-replay' else '0'), 'Wrong memory lifecycle case')
    if name=='memory-cal-return':
        require(rate==50000000 and entry['id']==long_entries['memory-live-before']['id'] and
                record==(directory/'memory-live-before.bin').read_bytes(),'Return to live did not restore protected live source')
    check_measurements(words,rate,entry['measurement'],profile,require_timing=True)
    start=parsed['view_start'] if name=='memory-cal-replay' else float(v['start'])
    check_cursor(words,rate,entry['ui'],profile,start)
    ops=export_operations(directory,name,'MEMORY_RECORD_EXPORTED');blob_address=in_elf(h['blob_address'],16512,segments)
    raw_address=hex32(v['record_address'])
    if v['replay']=='1':
        in_elf(v['record_address'],16384,segments)
        require(raw_address+16384<=blob_address or blob_address+16512<=raw_address,'Replay overwrites saved blob storage')
    else:require(raw_address in (0x13000000,0x13004000),'Live memory record outside capture DDR')
    wanted={name+'.scw',name+'.bin',name+'-frame.bin'}
    require(ops[name+'.scw']==(blob_address,4128) and ops[name+'.bin']==(raw_address,4096),'Memory Tcl address/length mismatch')
    if 'protected_address' in entry:
        wanted.add('memory-replay-live-held.bin');live=long_entries['memory-live-before']
        protected=hex32(entry['protected_address'])
        require(protected==hex32(live['adc']['display_address']) and ops['memory-replay-live-held.bin']==(protected,4096),
                'Wrong protected live DDR export')
        held=checked_bytes(directory/'memory-replay-live-held.bin',entry['protected_crc'],entry['protected_sha256'],16384)
        require(held==(directory/'memory-live-before.bin').read_bytes(),'Replay changed protected live DDR bytes')
    require(set(ops)==wanted,'Unexpected/missing memory export operations')
    bind_frame(directory,entry,ops,provenance,'memory',(freeze(h),freeze(v)))
    return dict(name=name,id=int(entry['id']),rate=rate,profile_id=f"{profile['id']:08x}",
                blob_sha256=entry['blob_sha256'],raw_sha256=entry['record_sha256']),parsed


def audit_fft(directory,entry,provenance,expected_profile,segments):
    name=entry['name'];h,v=entry['fft']['status'],entry['fft']['values']
    require((freeze(h),freeze(v)) in provenance['fft'],'FFT values are not one actual complete UART transaction')
    bind_calibration(entry['calibration'],provenance);profiles=calibration_profiles(entry['calibration'],expected_profile)
    c=entry['calibration']['status']
    require(c['spectrum_valid']=='1' and profiles['spectrum']==expected_profile,'FFT lost its calibrated snapshot identity')
    require(c['display_id']==v['display_id'] and c['epoch']==v['ui_epoch'],'FFT/calibration query current identity mismatch')
    require(h['valid']==h['panel']=='1' and h['count']=='4096' and h['actual_conversion']=='50000000', 'Invalid FFT snapshot/platform')
    require(h['rate'] in ('50000000','6250000') and v['running']=='0','FFT export is not a frozen M7 record')
    require(h['conversion']=='50000000' and int(h['filter'])==(0 if h['rate']=='50000000' else 3), 'Wrong FFT source rate/filter')
    ranges=[]
    for field,size in [('raw',16384),('bins',8196),('plot',2880)]:
        require(int(h[field+'_bytes'])==size and entry[field+'_crc'].lower()==h[field+'_crc'].lower(), 'FFT source size/CRC mismatch')
        address=in_elf(h[field+'_address'],size,segments);ranges.append((address,address+size))
    for i in range(3):
        for j in range(i):require(ranges[i][1]<=ranges[j][0] or ranges[j][1]<=ranges[i][0],'FFT export buffers overlap')
    metadata=metadata_from_fft(entry['fft'])
    require(entry['metadata']==metadata==load(directory/(name+'-metadata.json')),'FFT scalar metadata differs from actual UART frame')
    raw=checked_bytes(directory/(name+'-raw.bin'),entry['raw_crc'],entry['raw_sha256'],16384)
    bins=checked_bytes(directory/(name+'-bins.bin'),entry['bins_crc'],entry['bins_sha256'],8196)
    plot=checked_bytes(directory/(name+'-plot.bin'),entry['plot_crc'],entry['plot_sha256'],2880)
    numerical=compare_spectrum_calibrated(raw,bins,metadata,profiles['spectrum'])
    projection=check_plot(np.frombuffer(bins,dtype='<f4'),np.frombuffer(plot,dtype='<i4'),h,v)
    ops=export_operations(directory,name,'SPECTRUM_EXPORTED')
    require(set(ops)=={name+s for s in ('-raw.bin','-bins.bin','-plot.bin','-frame.bin')}, 'Unexpected/missing FFT export operations')
    for suffix,field,count in [('-raw.bin','raw_address',4096),('-bins.bin','bins_address',2049),('-plot.bin','plot_address',720)]:
        require(ops[name+suffix]==(hex32(h[field]),count),'FFT Tcl source address/count mismatch')
    frame=bind_frame(directory,entry,ops,provenance,'fft',(freeze(h),freeze(v)))
    raster=_raster(frame,np.frombuffer(plot,dtype='<i4'),int(h['channel']))
    return dict(name=name,id=int(h['id']),rate=int(h['rate']),numerical=numerical,projection=projection,raster=raster,
                raw_sha256=entry['raw_sha256'],analysis_ms=int(h['elapsed_ms'])),raw,bins


def equal_fields(a,b,keys,message):
    require(all(a[k]==b[k] for k in keys),message)


def epoch_advance(before,after,advance):
    require((int(after['epoch'])-int(before['epoch']))%2**32==advance,'Unexpected capture epoch change')


def bind_snapshot(snapshot,provenance,expected_profile):
    ui,adc=snapshot['ui'],snapshot['adc']
    require((freeze(ui),freeze(adc)) in provenance['pairs'],'Control UI/ADC is not a real UART query')
    bind_calibration(snapshot['calibration'],provenance)
    profiles=calibration_profiles(snapshot['calibration'],expected_profile)
    require(profiles['record']==expected_profile,'Control record changed voltage profile')
    if 'memory' in snapshot:
        memory=snapshot['memory'];bind_memory(memory,provenance)
        require(memory['view']['display_id']==ui['display_id'] and memory['view']['epoch']==ui['epoch'] and
                memory['view']['rate']==adc['display_rate'] and
                memory['view']['record_crc'].lower()==adc['display_crc'].lower(), 'Control memory/source identity disagrees')
    if 'parameter' in snapshot:
        parameter=snapshot['parameter'];bind_parameter(parameter,provenance)
        if ui['running']=='0':
            equal_fields(parameter['status'],ui,('epoch','running','display_id'),'Frozen parameter context disagrees')
        if 'memory' in snapshot:
            require(parameter['status']['wave_valid']==snapshot['memory']['status']['valid'] and
                    parameter['status']['wave_crc'].lower()==snapshot['memory']['status']['blob_crc'].lower(),
                    'Parameter/wave slot provenance disagrees')
    if 'measurement' in snapshot:
        measured=snapshot['measurement']
        require(measurement_identity(measured,False) in provenance['measurements'],'Control measurements are not an actual transaction')
        if ui['running']=='0':
            equal_fields(measured['context'],ui,('display_id','epoch','running'),'Control measurement/current context disagree')
    if ui['running']=='0':
        equal_fields(snapshot['calibration']['status'],ui,('display_id','epoch'),'Frozen profile context disagrees')
    return snapshot


def raw_held(before,after):
    equal_fields(before['ui'],after['ui'],('display_id','seen_id','running'),'Frozen displayed record changed')
    equal_fields(before['adc'],after['adc'],('display_id','display_address','display_crc','display_rate',
        'display_conversion_hz','display_filter','display_epoch'),'Frozen raw metadata changed')
    require(before['calibration']['profiles']['record']==after['calibration']['profiles']['record'] and
            before['calibration']['status']['record_id']==after['calibration']['status']['record_id'],
            'Frozen record coefficient identity changed')


def slots_held(before,after):
    equal_fields(before['parameter']['status'],after['parameter']['status'],
                 ('valid','blob_crc','wave_valid','wave_crc'),'Wave/parameter slots changed unexpectedly')


def audit_regression(session,provenance,expected_profile,adc_input):
    r=session['regression'];controls={c['name']:c for c in session['control_checks']}
    pairs=[('cal-frozen-view-operations','frozen_before','frozen_after'),
           ('cal-memory-v2-replay-original-rate','memory_live','memory_replay'),
           ('cal-return-live-profile','memory_replay','memory_returned'),
           ('cal-parameter-default-restore-profile','parameter_before','parameter_restored'),
           ('cal-autoset-from-frozen','autoset_before','autoset_record')]
    keys={key for _,a,b in pairs for key in (a,b)}|{'memory_saved','parameter_default'}
    for key in keys:bind_snapshot(r[key],provenance,expected_profile)
    for name,a,b in pairs:
        require(controls[name]['before']==r[a] and controls[name]['after']==r[b],'Regression/control case binding mismatch: '+name)
    before,after=r['frozen_before'],r['frozen_after'];raw_held(before,after);slots_held(before,after)
    epoch_advance(before['ui'],after['ui'],0)
    require(before['ui']['running']==after['ui']['running']=='0' and before['ui']['single_complete']=='1' and
            after['ui']['single_complete']=='1','View actions thawed the single record')
    other='ADC'+str(3-adc_input)
    require(any(dict(row).get('source')==other and dict(row).get('display_id')==before['ui']['display_id'] and
                dict(row).get('epoch')==before['ui']['epoch'] and dict(row).get('running')=='0'
                for row in provenance['index'].get('UI_STATUS',())), 'No actual frozen measurement-channel switch')
    saved,live,replay,returned=(r[k] for k in ('memory_saved','memory_live','memory_replay','memory_returned'))
    require(saved['memory']['status']['valid']=='1' and saved['parameter']['status']['valid']=='1','Independent RAM slots were not saved')
    require(live['memory']['view']['rate']=='50000000' and saved['memory']['view']['rate']=='6250000', 'Cross-rate replay scenario not exercised')
    require(replay['memory']['status']['replay']=='1' and replay['ui']['running']=='0','Saved record not replayed frozen')
    equal_fields(saved['memory']['view'],replay['memory']['view'],
                 ('display_id','record_crc','rate','conversion','filter','display_epoch'),'Replay relabelled or changed saved source')
    require(replay['memory']['view']['actual_conversion']=='50000000','Replay changed actual ADC clock metadata')
    epoch_advance(live['ui'],replay['ui'],1);slots_held(live,replay)
    ack=r['return_ack']
    for name in ('save_ack','stop_ack'):
        require(freeze(ack[name]) in provenance['index'].get('UI_STATUS',()),'Queued return acknowledgement not in actual UART')
        require(ack[name]['display_id']==live['ui']['display_id'],'Return acknowledgement did not restore live cache')
    require(ack['save_ack']['running']=='1' and ack['stop_ack']['running']=='0','Return/stop transaction was not observed')
    epoch_advance(replay['ui'],ack['save_ack'],1);epoch_advance(ack['save_ack'],ack['stop_ack'],1)
    require(returned['ui']==ack['stop_ack'] or returned['ui']['epoch']==ack['stop_ack']['epoch'], 'Queued return snapshot has another epoch')
    equal_fields(live['memory']['view'],returned['memory']['view'],
                 ('record_address','record_crc','rate','conversion','filter','display_id','display_epoch'), 'Returning live did not restore its cache')
    require(returned['memory']['status']['replay']=='0' and returned['ui']['running']=='0','Return to live is not canceled/frozen')
    slots_held(replay,returned)
    p0,pd,pr=(r[k] for k in ('parameter_before','parameter_default','parameter_restored'))
    for point in (pd,pr):raw_held(p0,point);slots_held(p0,point)
    epoch_advance(p0['ui'],pd['ui'],1);epoch_advance(pd['ui'],pr['ui'],1)
    defaults=dict(time='10',source='ADC1',trig_channel='1',level_mv='500',trig_rising='1',trig_normal='0',
                  ch1_scale='2',ch2_scale='2',ch1_pos='0',ch2_pos='0',cursor_a='250',cursor_b='750',
                  dual='0',grid='1',cursors='0',lang='zh-CN')
    require(all(pd['ui'][k]==v for k,v in defaults.items()),'Parameter defaults changed their established contract')
    preferences=tuple(defaults)
    equal_fields(saved['ui'],pr['ui'],preferences,'Parameter restore did not restore the saved preferences')
    automatic=r['autoset_after'];context=automatic['context']
    require((freeze(context),tuple(freeze(ch) for ch in automatic['channels'])) in provenance['autosets'],
            'Autoset result is not one actual UART transaction')
    require(context['state']=='2' and context['busy']=='0' and context['running']=='1' and context['target_time']=='6' and
            context['time']=='6' and context['display_rate']=='6250000' and context['trigger_channel']==str(adc_input),
            'Calibrated automatic setup did not complete with a useful physical timebase')
    original=r['autoset_before'];completed=r['autoset_record'];slots_held(original,completed)
    require(int(completed['ui']['display_id'])>int(original['ui']['display_id']) and
            completed['adc']['display_rate']=='6250000' and completed['ui']['running']=='0', 'Autoset did not acquire a new calibrated record')
    epoch_advance(original['ui'],completed['ui'],2)
    m=original['measurement']['channels'][adc_input-1]
    level=math.floor((float(m['min_v'])+float(m['max_v']))*500+.5) if float(m['min_v'])+float(m['max_v'])>=0 else math.ceil((float(m['min_v'])+float(m['max_v']))*500-.5)
    require(abs(int(context['level_mv'])-level)<=1,'Autoset trigger is not the physical midpoint')
    target_mask=0
    for ch,m in enumerate(original['measurement']['channels']):
        if m['valid']!='1' or m['overrange']=='1':continue
        lo,hi,mean,vpp=(float(m[k]) for k in ('min_v','max_v','mean_v','vpp_v'))
        for scale_index,scale in enumerate((.1,.2,.5,1,2,5,10)):
            at=-mean/scale*1000;position=math.floor(at+.5) if at>=0 else math.ceil(at-.5)
            if vpp<=6*scale and abs(mean)<=3.5*scale and lo/scale+position/1000>=-3.5 and hi/scale+position/1000<=3.5:
                target_mask|=1<<ch
                require(int(automatic['channels'][ch]['scale'])==scale_index and
                        abs(int(automatic['channels'][ch]['position'])-position)<=1,'Autoset calibrated channel scaling mismatch')
                break
    require(int(context['target_mask'])==target_mask,'Autoset channel mask differs from calibrated full-record statistics')
    return dict(controls=5,record_profile_preserved=True,legacy_parameter_format_preserved=True)


def audit_stability(session,provenance,expected_profile):
    stable=session['stability'];observations=stable['observations'];duration=float(stable['duration_seconds'])
    require(math.isfinite(duration) and duration>=32 and len(observations)>=20,'Insufficient stable acquisition observation')
    require(int(stable['end_id'])>int(stable['start_id']),'Live acquisition did not advance')
    bind_parameter(stable['parameter_before'],provenance);bind_parameter(stable['parameter_after'],provenance)
    slots=stable['parameter_before']['status'];last_elapsed=-1.;ids=[]
    for observation in observations:
        ui,adc=observation['ui'],observation['adc'];elapsed=float(observation['elapsed_seconds'])
        require(math.isfinite(elapsed) and elapsed>=last_elapsed and 0<=elapsed<=duration,'Unordered stability observation')
        last_elapsed=elapsed
        require((freeze(ui),freeze(adc)) in provenance['pairs'],'Stability UI/ADC not in actual framed UART')
        require(ui['running']=='1' and adc['display_rate']=='6250000' and adc['display_conversion_hz']=='50000000' and
                ui['triggered']=='1','Stability did not maintain live calibrated acquisition')
        bind_parameter(observation['parameter'],provenance);bind_calibration(observation['calibration'],provenance)
        profiles=calibration_profiles(observation['calibration'],expected_profile)
        require(profiles['record']==expected_profile,'Stability record lost its voltage profile')
        equal_fields(slots,observation['parameter']['status'],('valid','blob_crc','wave_valid','wave_crc'),'Stability changed RAM slots')
        ids.append(int(ui['display_id']))
    equal_fields(slots,stable['parameter_after']['status'],('valid','blob_crc','wave_valid','wave_crc'),'Stable end changed RAM slots')
    require(all(a<=b for a,b in zip(ids,ids[1:])) and ids[-1]>ids[0] and last_elapsed>=30,'Stability record identities stalled/reversed')
    controls={c['name']:c for c in session['control_checks']};control=controls['cal-stability-32s']
    for name,key in [('before','start_id'),('after','end_id')]:
        snapshot=control[name]
        require((freeze(snapshot['ui']),freeze(snapshot['adc'])) in provenance['pairs'] and
                snapshot['ui']['display_id']==str(stable[key]),'Stability control/source identity mismatch')
    begin=min(provenance['pair_positions'][(freeze(control['before']['ui']),freeze(control['before']['adc']))])
    end=max(provenance['pair_positions'][(freeze(control['after']['ui']),freeze(control['after']['adc']))])
    rows=[row for at,row in provenance['diagnostics'] if begin<at<end]
    require(len(rows)>=28 and all(row['dma_errors']=='00000000' and row['locked']==row['pixel_locked']=='1' for row in rows),
            'Missing/failed video evidence within the actual stable observation window')
    require(int(rows[-1]['ms'])-int(rows[0]['ms'])>=30000,'Video evidence does not span the stable observation interval')
    return dict(duration_seconds=duration,observations=len(observations),start_id=int(stable['start_id']),end_id=int(stable['end_id']),video_rows=len(rows))


def audit(directory,profile_path=None,session=None):
    directory=Path(directory).resolve()
    if session is None:session=load(directory/'session.json')
    require(session['state'] in ('captured-calibration-board-candidate','verified-calibration-board-awaiting-physical-feedback'),
            'Capture is incomplete, failed or from another milestone')
    require(session['flash_written'] is False and session['sd_used'] is False and session['reset'] is True and
            session['program_downloaded'] is True,'Candidate boot or no-Flash/no-SD state mismatch')
    require(session['server']=='owned-server-stopped','Owned capture server was not released')
    adc_input=session['adc_input'];require(type(adc_input) is int and adc_input in (1,2),'Invalid physical ADC input')
    source=session['source']
    require(source['adc_input']==adc_input and source['generator_channel']==2 and
            source['reference_policy']=='generator-setpoints' and source['module_input_voltage_independently_measured'] is False,
            'Reference/wiring policy changed')
    manifest_path=Path(profile_path) if profile_path is not None else directory/'calibration-profile-candidate.json'
    expected=profile_manifest(manifest_path)
    require(session['calibration_id']==f"{expected['id']:08x}",'Session/candidate profile identity mismatch')
    expected_exports(directory,session);segments=elf_segments(directory,session)
    package=load(directory/'package.json')
    require(package['adc_input']==adc_input and package['files'],'Prepared package/wiring binding mismatch')
    for item in package['files']:
        path=Path(item['Path'])
        require(path.resolve().parent==directory and hashlib.sha256(path.read_bytes()).hexdigest()==item['Hash'].lower(),
                'Prepared immutable package changed')
    data=(directory/'uart.log').read_bytes();log=data.decode('utf-8-sig')
    if 'uart_sha256' in session:
        require(hashlib.sha256(data).hexdigest()==session['uart_sha256'].lower(),'UART byte-stream SHA mismatch')
    offset=session['boot_start_offset'];require(type(offset) is int and 0<offset<len(log),'Missing pre/post-boot UART boundary')
    provenance=uart_provenance(log[offset:]);backup_provenance=uart_provenance(log[:offset])
    boot=(directory/'boot.log').read_text(encoding='utf-8-sig')
    require(re.search(r'(?m)^M7_CALIBRATION_RAM_LOADED\s*$',boot) and not re.search(r'^ERROR:|^EXPORT_ERROR:',boot,re.M),
            'Candidate temporary load did not succeed')
    backup=audit_user_backup(directory,session['user_record'],backup_provenance)
    bind_calibration(session['boot_calibration'],provenance)
    require(calibration_profiles(session['boot_calibration'],expected)['instrument']==expected,'Boot profile was not active')
    check_recipe(directory,session['points'])
    groups=check_source_points(directory,session)
    require(session['source_original']==load(directory/'source-original.json'),'Original source state is not its saved readback')
    source_ids={load(Path(ch['run'])/'session.json')['source_restored']['raw_idn']
                for ch in load(manifest_path)['binding']['channels']}
    require(source_ids=={session['source_original']['raw_idn']},'Professional source identity differs from calibration reference')
    long_reports=[];long_map={e['name']:e for e in session['long_records']}
    ids=[int(e['id']) for e in session['long_records']]
    require(len(ids)==len(set(ids)) and all(a<b for a,b in zip(ids,ids[1:])),'Raw capture identities duplicated/reversed')
    for entry in session['long_records']:
        long_reports.append(audit_long_record(directory,entry,provenance,expected,groups[entry['name']],adc_input,
                                              entry['name'] in EXPECTED_FRAMES))
    for name,direction in [('cal-trigger-rise','1'),('cal-trigger-fall','0')]:
        ui=long_map[name]['ui']
        require(ui['trig_rising']==direction and ui['single_complete']=='1' and ui['triggered']=='1' and
                ui['trig_channel']==str(adc_input),'Calibrated physical rising/falling single case not exercised')
    memory_reports=[];stored=[]
    for entry in session['memory_exports']:
        report,record=audit_memory(directory,entry,provenance,expected,segments,long_map)
        memory_reports.append(report);stored.append(record)
    require(all(r['sha256']==stored[0]['sha256'] for r in stored),'Saved waveform blob changed across replay/live/parameters')
    spectra=[];spectrum_raw=[];spectrum_bins=[]
    for entry in session['spectrum_exports']:
        report,raw,bins=audit_fft(directory,entry,provenance,expected,segments)
        spectra.append(report);spectrum_raw.append(raw);spectrum_bins.append(bins)
    require(all(raw==stored[0]['raw_data'] for raw in spectrum_raw),'FFT controls/replay did not retain saved raw payload')
    active=adc_input-1;empty=1-active
    configs=((active,1,1),(active,0,1),(active,1,0),(empty,1,0),(active,1,1))
    for entry,configuration in zip(session['spectrum_exports'],configs):
        h,v=entry['fft']['status'],entry['fft']['values']
        require(tuple(int(h[k]) for k in ('channel','window','remove_mean'))==configuration and h['rate']=='6250000' and
                int(h['id'])==stored[0]['meta']['record_id'] and int(h['source_epoch'])==stored[0]['meta']['capture_epoch'],
                'FFT options or stored source identity/rate were relabelled')
        require(v['replay']==('1' if entry['name']=='fft-cal-replay' else '0'),'FFT replay ownership flag mismatch')
    require(session['spectrum_exports'][3]['fft']['values']['peak_valid']=='0','Unwired FFT channel invented an AC peak')
    regression=audit_regression(session,provenance,expected,adc_input)
    stable=audit_stability(session,provenance,expected)
    final=session['final']
    require((freeze(final['ui']),freeze(final['adc'])) in provenance['pairs'],'Final display state is not an actual UART query')
    require(final['ui']['running']=='1' and final['ui']['source']=='ADC'+str(adc_input) and
            final['ui']['time']=='6' and final['adc']['display_rate']=='6250000','Final live display configuration changed')
    bind_calibration(session['final_calibration'],provenance)
    require(calibration_profiles(session['final_calibration'],expected)['record']==expected,'Final live profile changed')
    dc_errors=[abs(r['source_setpoint_error_v']) for r in long_reports if 'source_setpoint_error_v' in r]
    return dict(state='verified-independent-calibration-board-awaiting-physical-feedback',adc_input=adc_input,
        profile_id=f"{expected['id']:08x}",reference='generator-setpoints-engineering-no-independent-metrology',
        raw_records=24,memory_exports=3,spectrum_exports=5,new_frames=16,preserved_user_backup=backup,
        verified_total_frames=17,max_dc_setpoint_error_v=max(dc_errors),long_records=long_reports,
        memory=memory_reports,spectra=spectra,regression=regression,stability=stable,
        uart_sha256=hashlib.sha256(data).hexdigest(),physical_feedback='pending',flash_written=False,sd_used=False,
        limits=['Source setpoints are engineering references, not independent voltage measurements.',
                'Small-signal gain error and amplitude-dependent DC offset are reported, not hidden.',
                'This audit does not certify external ADC clock pulse width, PVT, or traceable accuracy.'])


def reference_spectrum_calibrated(words,rate_hz,channel,window,remove_mean,profile):
    words=_words(words)
    require(type(rate_hz) is int and 1<=rate_hz<=50000000,'Invalid spectrum record rate')
    require(channel in (0,1) and window in (0,1) and type(remove_mean) is bool,'Invalid FFT options')
    slope,offset=coefficients(profile,channel)
    voltage=((words>>(16*channel))&1023).astype(np.float64)*slope+offset
    mean=float(np.mean(voltage))
    samples=voltage-mean if remove_mean else voltage
    weights=np.ones(4096) if not window else .5-.5*np.cos(2*np.pi*np.arange(4096)/4096)
    rms=np.abs(np.fft.rfft(samples*weights))/np.sum(weights)
    rms[1:-1]*=math.sqrt(2)
    overrange=bool(np.any(words&np.uint32(1024<<(16*channel))))
    result=dict(valid=True,overrange=overrange,peak_valid=False,remove_mean=remove_mean,
                count=4096,rate_hz=rate_hz,channel=channel,window=window,bin_hz=rate_hz/4096,
                mean_v=mean,coherent_gain=float(np.mean(weights)),
                peak_bin=0.,peak_hz=0.,peak_vrms=0.,peak_dbv=-120.,vrms=rms)
    peak=int(np.argmax(rms[1:]))+1
    if peak<2 or overrange or rms[peak]<2*slope:
        return result
    estimate=float(peak)
    if peak<=2046 and rms[peak-1]>0 and rms[peak+1]>0:
        a,b,c=np.log(rms[peak-1:peak+2]);curve=a-2*b+c
        if curve<0:
            estimate+=float(np.clip(.5*(a-c)/curve,-.5,.5))
    amplitude=float(rms[peak])
    result.update(peak_valid=True,peak_bin=estimate,peak_hz=estimate*rate_hz/4096,
                  peak_vrms=amplitude,peak_dbv=20*math.log10(amplitude))
    return result


def compare_spectrum_calibrated(words,bins,metadata,profile):
    words=_words(words)
    if isinstance(bins,(bytes,bytearray,memoryview)):
        require(len(bins)==8196,'Wrong spectrum bin byte count')
        bins=np.frombuffer(bins,dtype='<f4').astype(np.float64)
    actual=np.asarray(bins,dtype=np.float64)
    require(actual.shape==(2049,) and bool(np.all(np.isfinite(actual))) and bool(np.all(actual>=0)),
            'Nonfinite, negative or wrong spectrum bins')
    expected=reference_spectrum_calibrated(words,int(metadata['rate_hz']),int(metadata['channel']),
        int(metadata['window']),bool(metadata['remove_mean']),profile)
    error=np.abs(actual-expected['vrms']);limit=2e-6+3e-4*expected['vrms']
    require(bool(np.all(error<=limit)),'Calibrated FFT bins disagree with independent NumPy result')
    for k in ('valid','overrange','peak_valid','remove_mean','count','rate_hz','channel','window'):
        require(metadata[k]==expected[k],'Calibrated FFT metadata mismatch: '+k)
    for k,tolerance in [('bin_hz',1e-6),('mean_v',2e-6),('coherent_gain',2e-6)]:
        require(math.isfinite(float(metadata[k])) and abs(float(metadata[k])-expected[k])<=tolerance,
                'Calibrated FFT normalization mismatch: '+k)
    if expected['peak_valid']:
        for k,tolerance in [('peak_bin',.002),('peak_hz',.002*expected['bin_hz']),
                            ('peak_vrms',max(2e-6,3e-4*expected['peak_vrms'])),('peak_dbv',.002)]:
            require(math.isfinite(float(metadata[k])) and abs(float(metadata[k])-expected[k])<=tolerance,
                    'Calibrated FFT peak mismatch: '+k)
        require(abs(float(metadata['peak_vrms'])-float(max(actual[1:])))<=2e-6,
                'FFT amplitude must describe a real bin, without peak inflation')
    else:
        for k in ('peak_bin','peak_hz','peak_vrms','peak_dbv'):
            require(float(metadata[k])==expected[k],'Invalid FFT peak sentinel mismatch: '+k)
    return dict(state='verified-calibrated-numpy-spectrum-only',bins=2049,
                profile_id=f"{profile['id']:08x}",max_absolute_error_vrms=float(max(error)),
                max_error_limit_ratio=float(max(error/limit)),peak_hz=expected['peak_hz'],
                peak_vrms=expected['peak_vrms'],reference='generator-setpoints-engineering')


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory',type=Path,nargs='?')
    parser.add_argument('--profile-manifest',type=Path)
    parser.add_argument('--spectrum-raw',type=Path)
    parser.add_argument('--spectrum-bins',type=Path)
    parser.add_argument('--spectrum-metadata',type=Path)
    parser.add_argument('--calibration-state',type=Path)
    parser.add_argument('--output',type=Path)
    args=parser.parse_args(argv)
    try:
        if args.directory is not None:
            require(not any((args.spectrum_raw,args.spectrum_bins,args.spectrum_metadata,args.calibration_state)),
                    'Run audit and spectrum-only modes cannot be mixed')
            result=audit(args.directory,args.profile_manifest)
        else:
            require(args.profile_manifest is not None and all((args.spectrum_raw,args.spectrum_bins,args.spectrum_metadata,args.calibration_state)),
                    'Spectrum mode requires manifest, raw, bins, metadata and calibration-state')
            profile=profile_manifest(args.profile_manifest)
            state=load(args.calibration_state);profiles=calibration_profiles(state,profile)
            require(state['status']['spectrum_valid']=='1','No valid spectrum profile')
            result=compare_spectrum_calibrated(args.spectrum_raw.read_bytes(),args.spectrum_bins.read_bytes(),
                                               load(args.spectrum_metadata),profiles['spectrum'])
        text=json.dumps(result,indent=2,allow_nan=False)+'\n'
        if args.output:
            args.output.write_text(text,encoding='utf-8')
        else:
            print(text,end='')
        print('CALIBRATION_EVIDENCE_PASS',result['state'])
    except (OSError,KeyError,TypeError,AssertionError,ValueError,OverflowError) as exc:
        parser.exit(1,'Calibration evidence audit failed: '+str(exc)+'\n')


if __name__=='__main__':
    main()
