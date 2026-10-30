"""Offline tests for the independent M7 numerical/provenance auditor."""
from pathlib import Path
import copy
import json
import math
import struct
import sys
import tempfile
import unittest

import numpy as np

ROOT=Path(__file__).resolve().parents[1]
OUT=Path('D:/FPGA_Project/SP2_Calibration_Build/board-audit')
OUT.mkdir(parents=True,exist_ok=True)
sys.path.insert(0,str(ROOT/'scripts'))
import audit_calibration_evidence as audit


def f32(value):
    return struct.unpack('<f',struct.pack('<f',value))[0]


def profile():
    return dict(flags=0x103,id=1,slope_v_per_code=[f32(.009),f32(.011)],offset_v=[f32(-4.55),f32(-5.3)])


def words():
    t=np.arange(4096)
    a=np.floor(505+60*np.sin(2*np.pi*32*t/4096)+.5).astype(np.uint32)
    b=np.floor(510+50*np.cos(2*np.pi*32*t/4096)+.5).astype(np.uint32)
    return a|(b<<16)


def profile_state():
    p=profile();rows={}
    for name in ('instrument','record','spectrum'):
        values=(p['slope_v_per_code'][0],p['offset_v'][0],p['slope_v_per_code'][1],p['offset_v'][1])
        rows[name]=dict(profile=name,**{k:f'{struct.unpack("<I",struct.pack("<f",v))[0]:08x}'
            for k,v in zip(audit.COEFFICIENT_FIELDS,values)})
    h=dict(display_id='5',epoch='8',instrument_flags='259',instrument_id='00000001',
           record_flags='259',record_id='00000001',spectrum_valid='1',spectrum_flags='259',spectrum_id='00000001')
    return dict(status=h,profiles=rows)


def line(prefix,fields):
    return prefix+' '+' '.join(k+'='+str(v) for k,v in fields.items())+'\n'


def acquisition_fixture():
    measurement=audit.expected_measurements(words(),6250000,profile())
    measurement['context']=dict(panel='1',channel='1',display_id='5',rate='6250000',count='4096',epoch='8',running='0',timing='1')
    entry=dict(name='cal-test',id='5',rate=6250000,
        ui=dict(display_id='5',epoch='8',running='0',source='ADC1'),
        adc=dict(display_id='5',display_rate='6250000',pending='0'),
        measurement=measurement,calibration=profile_state())
    log='COMMANDS: test\n'+line('UI_STATUS',entry['ui'])+line('ADC_LIVE_STATUS',entry['adc'])+'COMMANDS: test\n'
    log+=line('CALIBRATION_STATUS',entry['calibration']['status'])
    for name in audit.PROFILE_NAMES:
        log+=line('CALIBRATION_COEFFICIENTS',entry['calibration']['profiles'][name])
    log+=line('CALIBRATION_END',dict(display_id='5',epoch='8'))
    log+=line('MEASURE_STATUS',measurement['context'])
    for ch,t in zip(measurement['channels'],measurement['timing']):
        log+=line('MEASURE_CHANNEL',ch)+line('MEASURE_TIMING',t)
    log+=line('MEASURE_END',dict(display_id='5'))
    return entry,log


def source_fixture(on=True,frequency=50000,vpp=1):
    return dict(vendor='Siglent Technologies',model='SDG2122X',
        raw_idn='Siglent Technologies,SDG2122X,unit-test,1.0',waveform='SINE',offset_v=0,
        frequency_hz=frequency,vpp=vpp,phase_deg=0,output_on=on,load='HZ',polarity='NOR',
        raw_bswv=f'C2:BSWV WVTP,SINE,FRQ,{frequency}HZ,AMP,{vpp}V,OFST,0V,PHSE,0',
        raw_outp='C2:OUTP '+('ON' if on else 'OFF')+',LOAD,HZ,PLRT,NOR',
        modes={k:dict(state='OFF',raw=f'C2:{k} STATE,OFF') for k in ('MDWV','SWWV','BTWV')},
        coupling=dict(fields={k:'OFF' for k in ('TRACE','FCOUP','PCOUP','ACOUP','TRDUCH')},
                      raw='COUP TRACE,OFF,FCOUP,OFF,PCOUP,OFF,ACOUP,OFF,TRDUCH,OFF'))


def stability_fixture():
    log='';observations=[]
    def snapshot(identity):
        nonlocal log
        ui=dict(display_id=str(identity),epoch='8',running='1',source='ADC1',triggered='1')
        adc=dict(display_id=str(identity),display_rate='6250000',display_conversion_hz='50000000')
        parameter=dict(status=dict(valid='1',blob_crc='11111111',wave_valid='1',wave_crc='22222222',
                                   epoch='8',running='1',display_id=str(identity)),config=dict(time_ns='10000'))
        c=profile_state();c['status']['display_id']=str(identity)
        log+='COMMANDS: test\n'+line('UI_STATUS',ui)+line('ADC_LIVE_STATUS',adc)+'COMMANDS: test\n'
        log+=line('PARAM_STATUS',parameter['status'])+line('PARAM_CONFIG',parameter['config'])+line('PARAM_END',dict(epoch='8'))
        log+=line('CALIBRATION_STATUS',c['status'])
        for name in audit.PROFILE_NAMES:log+=line('CALIBRATION_COEFFICIENTS',c['profiles'][name])
        log+=line('CALIBRATION_END',dict(display_id=str(identity),epoch='8'))
        return dict(ui=ui,adc=adc,parameter=parameter,calibration=c)
    first=snapshot(100)
    for second in range(1,33):
        observation=snapshot(100+second);observation['elapsed_seconds']=float(second)
        observations.append(observation)
        log+=line('APP_DIAG',dict(ms=str(1000*second),locked='1',pixel_locked='1',dma_errors='00000000'))
    last=snapshot(133)
    session=dict(stability=dict(duration_seconds=32.5,observations=observations,start_id='100',end_id='133',
                               parameter_before=first['parameter'],parameter_after=last['parameter']),
                 control_checks=[dict(name='cal-stability-32s',before={k:first[k] for k in ('ui','adc')},
                                      after={k:last[k] for k in ('ui','adc')})])
    return session,log


class Numerical(unittest.TestCase):
    def test_profile_nominal_partial_and_invalid(self):
        self.assertTrue(audit.valid_profile(profile()))
        nominal=dict(flags=0,id=0,slope_v_per_code=[0,0],offset_v=[0,0])
        self.assertEqual(audit.coefficients(nominal,0),(10/1023,-5))
        part=profile();part['flags']=0x101;part['slope_v_per_code'][1]=0;part['offset_v'][1]=0
        self.assertTrue(audit.valid_profile(part))
        self.assertEqual(audit.coefficients(part,1),(10/1023,-5))
        for change in [('flags',0x104),('id',-1)]:
            bad=profile();bad[change[0]]=change[1];self.assertFalse(audit.valid_profile(bad))
        for value in (float('nan'),float('inf'),0,.02):
            bad=profile();bad['slope_v_per_code'][0]=value;self.assertFalse(audit.valid_profile(bad))

    def test_calibrated_fft_periodic_hann_and_dc(self):
        p=profile();raw=words()
        for ch in (0,1):
            for window in (0,1):
                for remove_mean in (False,True):
                    result=audit.reference_spectrum_calibrated(raw,6250000,ch,window,remove_mean,p)
                    codes=((raw>>(16*ch))&1023).astype(float)
                    volts=codes*p['slope_v_per_code'][ch]+p['offset_v'][ch]
                    samples=volts-volts.mean() if remove_mean else volts
                    weights=np.ones(4096) if not window else np.hanning(4097)[:-1]
                    expected=np.abs(np.fft.rfft(samples*weights))/weights.sum()
                    expected[1:-1]*=math.sqrt(2)
                    np.testing.assert_allclose(result['vrms'],expected,atol=1e-12)
                    self.assertAlmostEqual(result['mean_v'],float(volts.mean()),places=12)
                    self.assertAlmostEqual(result['coherent_gain'],1 if not window else .5,places=12)
                    metadata={k:v for k,v in result.items() if k!='vrms'}
                    report=audit.compare_spectrum_calibrated(raw,result['vrms'].astype('<f4').tobytes(),metadata,p)
                    self.assertEqual(report['bins'],2049)

    def test_fft_wrong_gain_dc_peak_and_bad_words_rejected(self):
        p=profile();raw=words();result=audit.reference_spectrum_calibrated(raw,6250000,0,1,True,p)
        metadata={k:v for k,v in result.items() if k!='vrms'}
        bad=result['vrms'].copy();bad[32]*=1.1
        with self.assertRaises((AssertionError,ValueError)):
            audit.compare_spectrum_calibrated(raw,bad,metadata,p)
        for field,delta in [('mean_v',.01),('peak_vrms',.1),('peak_hz',10000)]:
            changed=copy.deepcopy(metadata);changed[field]+=delta
            with self.assertRaises((AssertionError,ValueError)):
                audit.compare_spectrum_calibrated(raw,result['vrms'],changed,p)
        raw=raw.copy();raw[0]|=1<<11
        with self.assertRaises((AssertionError,ValueError)):
            audit.reference_spectrum_calibrated(raw,6250000,0,1,True,p)

    def test_quantizer_nominal_all_millivolts_and_calibrated_clamp(self):
        nominal=dict(flags=0,id=0,slope_v_per_code=[0,0],offset_v=[0,0])
        for mv in range(-5000,5001):
            self.assertEqual(audit.trigger_q8(nominal,0,mv),((mv+5000)*1023*256+5000)//10000)
        p=profile()
        self.assertEqual(audit.trigger_q8(p,0,-5000),0)
        self.assertEqual(audit.trigger_q8(p,0,5000),1023*256)
        self.assertEqual(audit.trigger_q8(p,0,0),math.floor((-p['offset_v'][0]/p['slope_v_per_code'][0])*256+.5))

    def test_statistics_affine_dc_rms_and_timing(self):
        raw=words();p=profile();measurement=audit.expected_measurements(raw,6250000,p)
        self.assertEqual(len(measurement['channels']),2)
        report=audit.check_measurements(raw,6250000,measurement,p,require_timing=True)
        self.assertEqual(len(report),2)
        for field in ('min_v','max_v','mean_v','vpp_v','rms_total_v','rms_ac_v'):
            bad=copy.deepcopy(measurement);bad['channels'][0][field]=str(float(bad['channels'][0][field])+.001)
            with self.assertRaises((AssertionError,ValueError)):
                audit.check_measurements(raw,6250000,bad,p,require_timing=True)
        bad=copy.deepcopy(measurement);bad['timing'][0]['mid_v']=str(float(bad['timing'][0]['mid_v'])+.001)
        with self.assertRaises((AssertionError,ValueError)):
            audit.check_measurements(raw,6250000,bad,p,require_timing=True)
        a=raw.copy();a[0]|=1<<10
        measured=audit.expected_measurements(a,6250000,p)
        self.assertEqual(measured['channels'][0]['overrange'],'1')
        self.assertEqual(measured['channels'][0]['frequency_valid'],'0')
        self.assertEqual(measured['timing'][0]['pulse_valid'],'0')

    def test_designated_calibrated_threshold_and_cursor(self):
        raw=words().copy();p=profile();rate=6250000;level=audit.trigger_q8(p,0,0)/256
        before,after=math.floor(level-2),math.ceil(level+2)
        raw[125]=(int(raw[125])&~1023)|before;raw[126]=(int(raw[126])&~1023)|after
        crossing=125+(level-before)/(after-before);start=crossing-125
        ui=dict(display_id='5',source='ADC1',time='6',view_start=f'{start:.3f}',crossing=f'{crossing:.3f}',
                trig_channel='1',level_mv='0',trig_rising='1',triggered='1',single_complete='0',epoch='8',
                cursors='1',bars='0',cursor_a='200',cursor_b='800')
        adc=dict(id='5',display_id='5',hw_seen='4096',hw_start='0',hw_after='126',hw_matched='1',hw_auto='0',display_rate=str(rate))
        entry=dict(ui=ui,adc=adc,rate=rate)
        result=audit.check_trigger(raw,entry,p)
        self.assertAlmostEqual(result['start'],start,places=12)
        points=[start+625*int(ui[k])/1000 for k in ('cursor_a','cursor_b')]
        def interp(at):
            lo=int(at);fraction=at-lo
            return ((int(raw[lo])&1023)+fraction*((int(raw[lo+1])&1023)-(int(raw[lo])&1023)))*p['slope_v_per_code'][0]+p['offset_v'][0]
        ui.update(cursor_dt_us='60.000000',cursor_dv=f'{interp(points[1])-interp(points[0]):.6f}',
                  cursor_valid='1',cursor_inv_hz=f'{1/(60e-6):.6f}')
        audit.check_cursor(raw,rate,ui,p,start)
        bad=copy.deepcopy(entry);bad['ui']['level_mv']='100'
        with self.assertRaises((AssertionError,ValueError)):
            audit.check_trigger(raw,bad,p)
        ui['cursor_dv']=str(float(ui['cursor_dv'])+.01)
        with self.assertRaises((AssertionError,ValueError)):
            audit.check_cursor(raw,rate,ui,p,start)


class Provenance(unittest.TestCase):
    def test_source_recipe_values_cannot_be_changed_with_the_plan(self):
        recipe=[]
        for name,value in [('n1p5',-1.5),('n0p5',-.5),('zero-a',0),('p0p5',.5),('p1p5',1.5),('zero-b',0)]:
            recipe.append(dict(name='cal-dc-'+name,waveform='DC',offset_v=value,rates=[50000000,6250000],reference_policy='generator-setpoints'))
        for name,value in [('0p2',.2),('1',1),('4',4)]:
            recipe.append(dict(name='cal-ac-50k-'+name,waveform='SINE',offset_v=0,frequency_hz=50000,vpp=value,
                               rates=[50000000,6250000],reference_policy='generator-setpoints'))
        recipe.append(dict(name='cal-freq-5m',waveform='SINE',offset_v=0,frequency_hz=5000000,vpp=1,
                           rates=[50000000],reference_policy='generator-setpoints'))
        groups=[dict(point=p,records=[p['name']+('-raw50m' if rate==50000000 else '-cic6m25') for rate in p['rates']]) for p in recipe]
        baseline=dict(name='cal-regression-baseline',waveform='SINE',frequency_hz=50000,vpp=1,offset_v=0,reference_policy='generator-setpoints')
        groups.append(dict(point=baseline,records=['cal-trigger-rise','cal-trigger-fall','cal-frozen-cic','memory-live-before','cal-autoset']))
        with tempfile.TemporaryDirectory(dir=OUT) as temporary:
            directory=Path(temporary);path=directory/'validation-points.json';path.write_text(json.dumps(recipe))
            for point in recipe+[baseline]:(directory/(point['name']+'-request.json')).write_text(json.dumps(point))
            audit.check_recipe(directory,groups)
            bad_request=copy.deepcopy(recipe[0]);bad_request['offset_v']=-1.
            (directory/(recipe[0]['name']+'-request.json')).write_text(json.dumps(bad_request))
            with self.assertRaises(ValueError):audit.check_recipe(directory,groups)
            (directory/(recipe[0]['name']+'-request.json')).write_text(json.dumps(recipe[0]))
            changed=copy.deepcopy(groups);changed[0]['point']['offset_v']=-1.
            changed_recipe=copy.deepcopy(recipe);changed_recipe[0]['offset_v']=-1.;path.write_text(json.dumps(changed_recipe))
            with self.assertRaises(ValueError):audit.check_recipe(directory,changed)

    def test_atomic_frames_and_coordinated_identity_forgery(self):
        entry,log=acquisition_fixture();evidence=audit.uart_provenance(log)
        audit.bind_acquisition(entry,evidence,require_timing=True)
        for targets in [('ui','display_id'),('adc','display_id')]:
            changed=copy.deepcopy(entry);changed[targets[0]][targets[1]]='77'
            with self.assertRaises((AssertionError,ValueError)):
                audit.bind_acquisition(changed,evidence,require_timing=True)
        changed=copy.deepcopy(entry)
        changed['id']=changed['ui']['display_id']=changed['adc']['display_id']='77'
        changed['measurement']['context']['display_id']=changed['calibration']['status']['display_id']='77'
        with self.assertRaises((AssertionError,ValueError)):
            audit.bind_acquisition(changed,evidence,require_timing=True)

    def test_timing_and_calibration_cannot_be_spliced(self):
        entry,log=acquisition_fixture();evidence=audit.uart_provenance(log)
        changed=copy.deepcopy(entry);changed['measurement']['timing'][0]['cycles']='999'
        with self.assertRaises((AssertionError,ValueError)):
            audit.bind_acquisition(changed,evidence,require_timing=True)
        changed=copy.deepcopy(entry);changed['calibration']['profiles']['record']['slope1_bits']='3c23d70a'
        with self.assertRaises((AssertionError,ValueError)):
            audit.bind_acquisition(changed,evidence,require_timing=True)
        with self.assertRaises((AssertionError,ValueError)):
            audit.uart_provenance(log.replace('CALIBRATION_END display_id=5 epoch=8','CALIBRATION_END display_id=7 epoch=8'))

    def test_complete_recipe_and_coordinated_deleted_case_rejected(self):
        exports={key:list(value) for key,value in [('long',audit.EXPECTED_LONG),('memory',audit.EXPECTED_MEMORY),
            ('spectrum',audit.EXPECTED_SPECTRUM),('controls',audit.EXPECTED_CONTROLS)]}
        session=dict(expected_exports=exports,expected_frames=list(audit.EXPECTED_FRAMES),
            long_records=[dict(name=n) for n in audit.EXPECTED_LONG],memory_exports=[dict(name=n) for n in audit.EXPECTED_MEMORY],
            spectrum_exports=[dict(name=n) for n in audit.EXPECTED_SPECTRUM],control_checks=[dict(name=n) for n in audit.EXPECTED_CONTROLS])
        with tempfile.TemporaryDirectory(dir=OUT) as temporary:
            directory=Path(temporary)
            def write(s):
                (directory/'expected-exports.json').write_text(json.dumps(s['expected_exports']))
                (directory/'expected-frames.json').write_text(json.dumps(s['expected_frames']))
            write(session);audit.expected_exports(directory,session)
            for group in ('long_records','memory_exports','spectrum_exports','control_checks'):
                changed=copy.deepcopy(session);changed[group].pop()
                with self.assertRaises(ValueError):audit.expected_exports(directory,changed)
            changed=copy.deepcopy(session);changed['expected_exports']['long'].pop();changed['long_records'].pop()
            write(changed)
            with self.assertRaises(ValueError):audit.expected_exports(directory,changed)
            changed=copy.deepcopy(session);changed['expected_frames'].pop();write(changed)
            with self.assertRaises(ValueError):audit.expected_exports(directory,changed)

    def test_original_source_off_is_restored_and_bad_identity_rejected(self):
        original=source_fixture(False,1000,4);before=source_fixture()
        point=dict(name='unit-source',waveform='SINE',frequency_hz=50000,vpp=1,offset_v=0)
        group=dict(point=point,source_before=before,source_after=copy.deepcopy(before),
                   source_file='before.json',source_after_file='after.json',records=['unit-raw'])
        session=dict(source_restored=copy.deepcopy(original),points=[group],long_records=[dict(name='unit-raw')])
        with tempfile.TemporaryDirectory(dir=OUT) as temporary:
            directory=Path(temporary)
            for name,value in [('source-original.json',original),('source-restored.json',original),('before.json',before),('after.json',before)]:
                (directory/name).write_text(json.dumps(value))
            result=audit.check_source_points(directory,session);self.assertEqual(set(result),{'unit-raw'})
            changed=copy.deepcopy(session);changed['source_restored']=source_fixture(True,1000,4)
            (directory/'source-restored.json').write_text(json.dumps(changed['source_restored']))
            with self.assertRaises(ValueError):audit.check_source_points(directory,changed)
        for mutation in ('vendor','coupling','phase','raw_load'):
            changed=copy.deepcopy(before)
            if mutation=='vendor':changed['raw_idn']='Other,SDG2122X,unit-test,1.0'
            elif mutation=='coupling':changed['coupling']['fields']['TRACE']='ON'
            elif mutation=='phase':changed['phase_deg']=float('inf');changed['raw_bswv']=changed['raw_bswv'].replace('PHSE,0','PHSE,inf')
            else:changed['raw_outp']=changed['raw_outp'].replace('LOAD,HZ','LOAD,50')
            with self.assertRaises(ValueError):audit.source_check(changed)

    def test_frozen_raw_and_slots_reject_mutations(self):
        entry,_=acquisition_fixture();entry['ui']['seen_id']='5'
        entry['adc'].update(display_address='13000000',display_crc='12345678',display_rate='6250000',
                            display_conversion_hz='50000000',display_filter='3',display_epoch='7')
        entry['parameter']=dict(status=dict(valid='1',blob_crc='11111111',wave_valid='1',wave_crc='22222222'))
        audit.raw_held(entry,copy.deepcopy(entry));audit.slots_held(entry,copy.deepcopy(entry))
        for field in ('display_crc','display_rate','display_epoch'):
            changed=copy.deepcopy(entry);changed['adc'][field]='0'
            with self.assertRaises(ValueError):audit.raw_held(entry,changed)
        changed=copy.deepcopy(entry);changed['calibration']['profiles']['record']['slope1_bits']='00000000'
        with self.assertRaises(ValueError):audit.raw_held(entry,changed)
        changed=copy.deepcopy(entry);changed['parameter']['status']['wave_crc']='00000000'
        with self.assertRaises(ValueError):audit.slots_held(entry,changed)

    def test_real_stability_window_and_slot_change_rejected(self):
        session,log=stability_fixture();provenance=audit.uart_provenance(log)
        report=audit.audit_stability(session,provenance,profile());self.assertEqual(report['video_rows'],32)
        changed=copy.deepcopy(session);changed['stability']['duration_seconds']=31.99
        with self.assertRaises(ValueError):audit.audit_stability(changed,provenance,profile())
        changed=copy.deepcopy(session);changed['stability']['observations'][-1]['parameter']['status']['wave_crc']='00000000'
        with self.assertRaises(ValueError):audit.audit_stability(changed,provenance,profile())
        short_log='\n'.join(line for line in log.splitlines() if not line.startswith('APP_DIAG ') or 'ms=1000 ' in line)
        with self.assertRaises(ValueError):audit.audit_stability(session,audit.uart_provenance(short_log),profile())
        no_advance=copy.deepcopy(session);no_advance['stability']['end_id']='100'
        with self.assertRaises(ValueError):audit.audit_stability(no_advance,provenance,profile())


if __name__=='__main__':
    unittest.main()
