"""Offline adversarial tests. Synthetic bytes here are never board evidence."""
import copy
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import shutil
import struct
import sys
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
FILE = ROOT / 'scripts/audit_storage_evidence.py'
if FILE.exists():
    spec = importlib.util.spec_from_file_location('storage_audit', FILE)
    audit = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(audit)
else:
    audit = None


def line(tag, values):
    return tag + ' ' + ' '.join(f'{k}={v}' for k, v in values.items()) + '\n'


def tf_fixture():
    actions, log = [], ''
    commands = ['tfsave 0', 'tfsave 1'] + ['tfsave 0'] * 6 + ['tfsave 0',
        'tfload 0 1', 'tfload 1 1', 'tfload 0 9999999', 'tfopen 0', 'tflist 0 1']
    wave = 0
    for seq, command in enumerate(commands, 11):
        kind = command.split()[1]
        if command == 'tfsave 0':
            wave += 1
        result = '7' if '9999999' in command else '0'
        identity = int(command.split()[2]) if command.startswith('tfload') else wave if command == 'tfsave 0' else 1
        blob = dict(address='00600000', bytes='16512' if kind == '0' else '96', crc='12345678')
        if result == '7' or command.startswith(('tfopen', 'tflist')):
            blob = dict(address='00000000', bytes='0', crc='00000000')
        state = dict(status=dict(busy='0', ready='1', result=result, seq=str(seq), id=str(identity), kind=kind),
            media=dict(cid='00000000000000000000000000000000', fat='3', sector_count='61067264'), blob=blob)
        actions.append(dict(command=command, sequence=str(seq), status=state))
        ack = 'tfopen' if command.startswith('tfopen') else command
        log += f'TF_COMMAND {ack} QUEUED seq={seq}\n'
        for tag, key in [('TF_STATUS', 'status'), ('TF_MEDIA', 'media'), ('TF_BLOB', 'blob')]:
            log += line(tag, state[key])
    return dict(tf_actions=actions, first_wave_id=1, parameter_id=1), log


def source_fixture(enabled):
    raw = 'C2:BSWV WVTP,SQUARE,FRQ,50000HZ,AMP,1V,OFST,0V,DUTY,50'
    return dict(frequency_hz=50000, vpp=1, offset_v=0, waveform='SQUARE',
        output_on=enabled, load='HZ', polarity='NOR', raw_bswv=raw,
        raw_outp='C2:OUTP '+('ON' if enabled else 'OFF')+',LOAD,HZ,PLRT,NOR',
        vendor='Siglent Technologies', model='SDG2122X',
        raw_idn='Siglent Technologies,SDG2122X,SYNTHETIC,2.01.01.39R7T2',
        bswv=dict(WVTP='SQUARE', FRQ='50000HZ', AMP='1V', OFST='0V', DUTY='50'),
        outp=dict(LOAD='HZ', PLRT='NOR'),
        modes={k:dict(state='OFF', raw=f'C2:{k} STATE,OFF') for k in ('MDWV','SWWV','BTWV')},
        coupling=dict(raw='COUP TRACE,OFF,FCOUP,OFF,PCOUP,OFF,ACOUP,OFF,TRDUCH,OFF',
            fields={k:'OFF' for k in ('TRACE','FCOUP','PCOUP','ACOUP','TRDUCH')}))


def sealed(data):
    data=bytearray(data);data[12:16]=bytes(4)
    struct.pack_into('<I',data,12,zlib.crc32(data))
    return bytes(data)


def record_fixture():
    data=bytearray(16512)
    struct.pack_into('<4sHHI',data,0,b'SCW1',2,128,16512)
    struct.pack_into('<11I',data,16,4096,6250000,50000000,3,50,12,10000,0,0,0,1)
    struct.pack_into('<iI2I2i4I',data,60,0,1,2,2,0,0,250,750,2,0)
    values=[int(x,16) for x in ('3c19e024','c0976997','3c158529','c094a49b')]
    floats=[struct.unpack('<f',struct.pack('<I',x))[0] for x in values]
    q8=math.floor(-floats[3]/floats[2]*256+.5)
    struct.pack_into('<II4II',data,100,q8,0x2b474854,*values,259)
    for i in range(4096):
        struct.pack_into('<I',data,128+4*i,500|((480+i%100)<<16))
    return sealed(data)


def settings_fixture():
    data=bytearray(96)
    struct.pack_into('<4sHHI',data,0,b'SCP1',1,96,96)
    struct.pack_into('<4Ii3I2i3I',data,16,10000,50000000,1,1,0,9,2,2,0,0,250,750,0)
    return sealed(data)


def record_bundle():
    wave=record_fixture();raw=wave[128:];scp=settings_fixture()
    crc=lambda b:f'{zlib.crc32(b):08x}'
    ui=dict(display_id='50',seen_id='50',epoch='12',running='0',time='6',source='ADC2',
        ch1_scale='2',ch2_scale='2',ch1_pos='0',ch2_pos='0',trig_channel='2',
        trig_normal='0',trig_rising='1',level_mv='0',dual='1',grid='1',cursors='0',cursor_a='250',
        cursor_b='750',lang='zh-CN')
    view=dict(record_address='13000000',record_crc=crc(raw),rate='6250000',conversion='50000000',
        filter='3',display_epoch='12',display_id='50',actual_conversion='50000000')
    wave_status=dict(valid='1',blob_crc=crc(wave),saved_id='50',saved_rate='6250000',saved_epoch='12',replay='0')
    param_status=dict(valid='1',blob_crc=crc(scp),wave_valid='1',wave_crc=crc(wave))
    config=dict(time_ns='10000',origin_conversion='50000000',selected_channel='1',trigger_channel='1',
        level_mv='0',flags='9',language='0',ch1_scale='2',ch2_scale='2',ch1_pos='0',ch2_pos='0',
        cursor_a='250',cursor_b='750')
    ch={key:'0.000000' for key in ('min_v','max_v','mean_v','vpp_v','rms_total_v','rms_ac_v','frequency_hz','period_us')}
    base=dict(ui=ui,memory=dict(view=view,status=wave_status),parameter=dict(status=param_status,config=config),
        measurement=dict(channels=[dict(ch,ch='1'),dict(ch,ch='2')]))
    checkpoints={name:copy.deepcopy(base) for name in ('frozen-before-files','wave-save-held','parameter-save-held',
        'live-50m-before-replay','tf-replay-original-rate','returned-live','before-TF-parameters',
        'after-TF-parameters','missing-file-held')}
    for name,epoch in [('live-50m-before-replay',14),('returned-live',16),('before-TF-parameters',16),
                        ('after-TF-parameters',17),('missing-file-held',17)]:
        snap=checkpoints[name];snap['ui'].update(display_id='60',seen_id='60',epoch=str(epoch))
        snap['memory']['view'].update(record_address='13004000',rate='50000000',filter='0',display_id='60',display_epoch='14')
    replay=checkpoints['tf-replay-original-rate'];replay['ui']['epoch']='15'
    replay['memory']['status']['replay']='1';replay['memory']['view']['record_address']='00610000'
    data={'wave-saved':wave,'wave-loaded':wave,'parameters-saved':scp,'parameters-loaded':scp,
        'original-raw':raw,'tf-replay-raw':raw,'protected-live-raw':raw,'protected-live-after':raw}
    exports={name:dict(address='00600000',bytes=len(value),crc=crc(value)) for name,value in data.items()}
    for name,snapname in [('original-raw','frozen-before-files'),('tf-replay-raw','tf-replay-original-rate'),
                          ('protected-live-raw','live-50m-before-replay'),('protected-live-after','live-50m-before-replay')]:
        exports[name]['address']=checkpoints[snapname]['memory']['view']['record_address']
    state,_=tf_fixture()
    for name,index in [('wave-saved',0),('parameters-saved',1),('wave-loaded',9),('parameters-loaded',10)]:
        state['tf_actions'][index]['status']['blob']=copy.deepcopy(exports[name])
    state['tf_fft']=dict(status=dict(valid='1',rate='6250000',id='50',source_epoch='12',seq='2'),values=dict(replay='1'))
    text=line('FFT_STATUS',state['tf_fft']['status'])+line('FFT_VALUES',state['tf_fft']['values'])+line('FFT_END',dict(seq='2'))
    return state,checkpoints,exports,data,text


def snapshot_bundle():
    _,points,_,_,_=record_bundle()
    snap=points['frozen-before-files'];ui=snap['ui'];view=snap['memory']['view']
    view.update(epoch=ui['epoch'],running='0',record_bytes='16384')
    snap['adc']=dict(display_id=ui['display_id'],pending='0',display_address=view['record_address'],
        display_crc=view['record_crc'],display_rate=view['rate'],display_epoch=view['display_epoch'],
        conversion_hz='50000000',display_conversion_hz='50000000')
    snap['parameter']['status'].update(record_address=view['record_address'],record_crc=view['record_crc'],
        record_bytes='16384',rate=view['rate'],actual_conversion='50000000',display_id=ui['display_id'],epoch=ui['epoch'])
    snap['measurement']['context']=dict(display_id=ui['display_id'],epoch=ui['epoch'],count='4096',rate=view['rate'])
    status=dict(display_id=ui['display_id'],epoch=ui['epoch'],instrument_id='2b474854',instrument_flags='259',
        record_id='2b474854',record_flags='259',spectrum_id='2b474854',spectrum_flags='259',spectrum_valid='1')
    snap['calibration']=dict(status=status,profiles={name:dict(audit.CAL_BITS,profile=name)
        for name in ('instrument','record','spectrum')})
    snap['fft']=dict(status=dict(seq='5'),values=dict(display_id=ui['display_id'],ui_epoch=ui['epoch'],
        wave_crc=snap['parameter']['status']['wave_crc'],param_crc=snap['parameter']['status']['blob_crc'],
        replay='0',live_rate=view['rate']))
    return snap


def snapshot_log(snap):
    text=line('UI_STATUS',snap['ui'])+line('ADC_LIVE_STATUS',snap['adc'])
    text+=line('MEMORY_STATUS',snap['memory']['status'])+line('MEMORY_VIEW',snap['memory']['view'])
    text+=line('MEMORY_END',dict(display_id=snap['ui']['display_id']))
    text+=line('PARAM_STATUS',snap['parameter']['status'])+line('PARAM_CONFIG',snap['parameter']['config'])
    text+=line('PARAM_END',dict(epoch=snap['ui']['epoch']))
    text+=line('MEASURE_STATUS',snap['measurement']['context'])
    for channel in snap['measurement']['channels']:
        text+=line('MEASURE_CHANNEL',channel)
    text+=line('MEASURE_END',dict(display_id=snap['ui']['display_id']))
    text+=line('FFT_STATUS',snap['fft']['status'])+line('FFT_VALUES',snap['fft']['values'])
    text+=line('FFT_END',dict(seq=snap['fft']['status']['seq']))
    text+=line('CALIBRATION_STATUS',snap['calibration']['status'])
    for name in ('instrument','record','spectrum'):
        text+=line('CALIBRATION_COEFFICIENTS',snap['calibration']['profiles'][name])
    return text+line('CALIBRATION_END',dict(display_id=snap['ui']['display_id'],epoch=snap['ui']['epoch']))


class StorageEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(audit, 'independent auditor implementation is missing')
        base = Path('D:/FPGA_Project/SP2_Storage_Build/auditor-tests')
        base.mkdir(parents=True, exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(dir=base)
        self.addCleanup(self.tmp.cleanup)
        self.run = Path(self.tmp.name)

    def write_json(self, name, value):
        (self.run/name).write_text(json.dumps(value), encoding='utf8')

    def test_tf_complete_transaction(self):
        state, text = tf_fixture()
        self.assertEqual(audit.audit_tf_actions(state, audit.UART(text))['wave_save_count'], 8)

    def test_tf_wrong_sequence_cannot_borrow_other_transaction(self):
        state, text = tf_fixture()
        state['tf_actions'][0]['status']['status']['seq'] = '12'
        with self.assertRaisesRegex(ValueError, 'seq|sequence'):
            audit.audit_tf_actions(state, audit.UART(text))

    def test_tf_status_media_blob_must_be_atomic_and_ordered(self):
        state, text = tf_fixture()
        lines = text.splitlines(True)
        lines[2], lines[3] = lines[3], lines[2]
        with self.assertRaises(ValueError):
            audit.audit_tf_actions(state, audit.UART(''.join(lines)))

    def test_tf_ack_must_match_full_command(self):
        state, text = tf_fixture()
        text = text.replace('TF_COMMAND tfload 0 1 QUEUED', 'TF_COMMAND tfload 0 2 QUEUED')
        with self.assertRaises(ValueError):
            audit.audit_tf_actions(state, audit.UART(text))

    def test_tf_missing_failure_must_have_no_blob(self):
        state, text = tf_fixture()
        state['tf_actions'][11]['status']['blob']['bytes'] = '16512'
        with self.assertRaises(ValueError):
            audit.audit_tf_actions(state, audit.UART(text))

    def test_tf_ids_must_be_new_and_monotone(self):
        state, text = tf_fixture()
        state['tf_actions'][3]['status']['status']['id'] = '1'
        text = text.replace('seq=14 id=3', 'seq=14 id=1')
        with self.assertRaises(ValueError):
            audit.audit_tf_actions(state, audit.UART(text))

    def test_tf_media_identity_required(self):
        state, text = tf_fixture()
        text = text.replace('61067264', '61067265')
        for action in state['tf_actions']:
            action['status']['media']['sector_count'] = '61067265'
        with self.assertRaises(ValueError):
            audit.audit_tf_actions(state, audit.UART(text))

    def test_tf_frozen_seven_files_cannot_change_payload_crc(self):
        state,text=tf_fixture()
        state['tf_actions'][2]['status']['blob']['crc']='abcdef01'
        text=text.replace('seq=13 id=2 kind=0\nTF_MEDIA cid=00000000000000000000000000000000 fat=3 sector_count=61067264\nTF_BLOB address=00600000 bytes=16512 crc=12345678',
            'seq=13 id=2 kind=0\nTF_MEDIA cid=00000000000000000000000000000000 fat=3 sector_count=61067264\nTF_BLOB address=00600000 bytes=16512 crc=abcdef01')
        with self.assertRaises(ValueError):
            audit.audit_tf_actions(state,audit.UART(text))

    def test_tf_missing_file_status_id_must_match_request(self):
        state,text=tf_fixture()
        action=state['tf_actions'][11]
        action['status']['status']['id']='9999998'
        text=text.replace('seq=22 id=9999999','seq=22 id=9999998')
        with self.assertRaises(ValueError):
            audit.audit_tf_actions(state,audit.UART(text))

    def test_tf_raw_commands_must_exactly_cover_manifest(self):
        state,text=tf_fixture()
        extra='TF_COMMAND tfsave 0 QUEUED seq=999\n'
        extra+=line('TF_STATUS',dict(busy='0',ready='1',result='0',seq='999',id='999',kind='0'))
        extra+=line('TF_MEDIA',dict(cid=audit.CID,fat='3',sector_count='61067264'))
        extra+=line('TF_BLOB',dict(address='00600000',bytes='16512',crc='12345678'))
        for changed in (extra+text,text+extra,text+'TF_COMMAND tfload 0 55 REJECTED seq=999\n'):
            with self.subTest(placement=changed[:40]):
                with self.assertRaises(ValueError):
                    audit.audit_tf_actions(state,audit.UART(changed))

    def sources(self):
        for name, enabled in [('original',False),('on',True),('restored',False)]:
            self.write_json(f'source-{name}.json', source_fixture(enabled))

    def test_square_source_off_on_off(self):
        self.sources()
        self.assertEqual(audit.audit_source(self.run)['waveform'], 'SQUARE')

    def test_source_raw_and_parsed_disagreement_rejected(self):
        self.sources()
        value = source_fixture(True)
        value['raw_bswv'] = value['raw_bswv'].replace('50000HZ','1000HZ')
        self.write_json('source-on.json', value)
        with self.assertRaises(ValueError):
            audit.audit_source(self.run)

    def test_source_restoration_must_retain_raw_waveform(self):
        self.sources()
        value = source_fixture(False)
        value['raw_bswv'] += ',PHSE,0'
        value['bswv']['PHSE'] = '0'
        self.write_json('source-restored.json', value)
        with self.assertRaises(ValueError):
            audit.audit_source(self.run)

    def test_source_on_mode_raw_cannot_claim_off(self):
        self.sources()
        value = source_fixture(True)
        value['modes']['MDWV']['raw'] = 'C2:MDWV STATE,ON'
        self.write_json('source-on.json', value)
        with self.assertRaises(ValueError):
            audit.audit_source(self.run)

    def export_fixture(self):
        raw = bytes(range(256))*64
        crc = f'{zlib.crc32(raw):08x}'
        entry = dict(name='original-raw',address='13000000',bytes=len(raw),crc=crc,
            sha256=hashlib.sha256(raw).hexdigest(),ddr=True)
        (self.run/'original-raw.bin').write_bytes(raw)
        tcl = f'mrd -bin -file {{{self.run.as_posix()}/original-raw.bin}} 0x13000000 4096\nputs STORAGE_BYTES_EXPORTED\n'
        (self.run/'export-original-raw.tcl').write_text(tcl)
        (self.run/'export-original-raw.log').write_text('STORAGE_BYTES_EXPORTED\n')
        uart = audit.UART(line('MEMORY_VIEW', dict(record_address=entry['address'],record_bytes='16384',record_crc=crc)))
        return entry, uart

    def test_export_exact_bytes_crc_sha_and_uart(self):
        entry, uart = self.export_fixture()
        self.assertEqual(len(audit.audit_export(self.run,entry,uart,[])),16384)

    def test_export_truncation_rejected(self):
        entry, uart = self.export_fixture()
        (self.run/'original-raw.bin').write_bytes(b'broken')
        with self.assertRaises(ValueError):
            audit.audit_export(self.run,entry,uart,[])

    def test_export_uart_must_bind_address_length_and_crc_same_line(self):
        entry, _ = self.export_fixture()
        text = line('MEMORY_VIEW',dict(record_address='13004000',record_bytes='16384',record_crc=entry['crc']))
        text += line('MEMORY_VIEW',dict(record_address='13000000',record_bytes='16384',record_crc='00000000'))
        with self.assertRaises(ValueError):
            audit.audit_export(self.run,entry,audit.UART(text),[])

    def test_export_tcl_extra_read_or_write_rejected(self):
        entry, uart = self.export_fixture()
        path=self.run/'export-original-raw.tcl'
        good=path.read_text()
        for extra in ['mwr 0x100000 0\n','mrd 0x100000\n','rst -system\n','exec cmd\n',
                      'eval {mwr 0x100000 0}\n','file delete anything\n','open anything w\n']:
            with self.subTest(extra=extra):
                path.write_text(good+extra)
                with self.assertRaises(ValueError):
                    audit.audit_export(self.run,entry,uart,[])

    def test_adc_uart_cannot_lie_about_export_length(self):
        entry,_=self.export_fixture()
        uart=audit.UART(line('ADC_LIVE_STATUS',dict(display_address=entry['address'],
            bytes='4',display_crc=entry['crc'])))
        with self.assertRaises(ValueError):
            audit.audit_export(self.run,entry,uart,[])

    def test_export_tcl_wrong_address_or_words_rejected(self):
        entry, uart = self.export_fixture()
        path=self.run/'export-original-raw.tcl'
        path.write_text(path.read_text().replace('4096','4095'))
        with self.assertRaises(ValueError):
            audit.audit_export(self.run,entry,uart,[])

    def test_export_success_marker_required(self):
        entry, uart = self.export_fixture()
        (self.run/'export-original-raw.log').write_text('failed\n')
        with self.assertRaises(ValueError):
            audit.audit_export(self.run,entry,uart,[])

    def test_package_path_escape_rejected_even_with_correct_hash(self):
        outside=self.run.parent/'outside-audit-fixture'
        outside.write_bytes(b'fixture')
        self.addCleanup(outside.unlink)
        state=dict(package=[dict(Path=str(outside),Hash=hashlib.sha256(b'fixture').hexdigest())])
        with self.assertRaises(ValueError):
            audit.audit_package(self.run,state)

    def test_incomplete_state_does_not_receive_verified_label(self):
        self.write_json('session.json',dict(state='prepared-not-run'))
        with self.assertRaises(ValueError):
            audit.audit_run(self.run)

    def test_readonly_load_link_requires_actual_artifacts_and_uart(self):
        self.write_json('session.json',dict(state='verified-new-application-readonly-list-no-card-writes',
            elf_sha256='6917E229BE48D990EB7255ADA4E5BDDD052CEBF4B6E97BABD105C69C4B9C5A4C',
            card_writes=0,flash_written=False))
        self.assertTrue(hasattr(audit,'audit_load'),'readonly load source binding is missing')
        with self.assertRaises(ValueError):
            audit.audit_load(self.run)

    def test_scw_scp_original_rate_and_held_slots(self):
        state,points,exports,data,text=record_bundle()
        result=audit.audit_records(state,points,exports,data,audit.UART(text))
        self.assertEqual(result['record_hz'],6250000)

    def test_record_adversarial_identity_rate_slots_settings(self):
        mutations=[
            lambda s,p,e,d: p['tf-replay-original-rate']['memory']['view'].__setitem__('rate','50000000'),
            lambda s,p,e,d: p['tf-replay-original-rate']['memory']['view'].__setitem__('display_epoch','13'),
            lambda s,p,e,d: p['tf-replay-original-rate']['ui'].__setitem__('display_id','51'),
            lambda s,p,e,d: d.__setitem__('tf-replay-raw',bytes(16384)),
            lambda s,p,e,d: d.__setitem__('protected-live-after',bytes(16384)),
            lambda s,p,e,d: d.__setitem__('parameters-loaded',bytes(96)),
            lambda s,p,e,d: p['after-TF-parameters']['ui'].__setitem__('time','5'),
            lambda s,p,e,d: p['after-TF-parameters']['ui'].__setitem__('epoch','16'),
            lambda s,p,e,d: p['missing-file-held']['parameter']['status'].__setitem__('blob_crc','00000000'),
            lambda s,p,e,d: p['tf-replay-original-rate']['memory']['status'].__setitem__('blob_crc','00000000'),
            lambda s,p,e,d: p['before-TF-parameters']['memory']['view'].__setitem__('rate','6250000'),
            lambda s,p,e,d: e['wave-loaded'].__setitem__('address','00600004'),
            lambda s,p,e,d: s['tf_fft']['status'].__setitem__('rate','50000000'),
            lambda s,p,e,d: s['tf_fft']['values'].__setitem__('replay','0'),
        ]
        for index,mutate in enumerate(mutations):
            with self.subTest(index=index):
                state,points,exports,data,text=record_bundle();mutate(state,points,exports,data)
                with self.assertRaises(ValueError):
                    audit.audit_records(state,points,exports,data,audit.UART(text))

    def test_calibration_atomic_raw_rows_and_float_bits(self):
        status=dict(instrument_id='2b474854',record_id='2b474854',spectrum_id='2b474854',
            instrument_flags='259',record_flags='259',spectrum_flags='259',spectrum_valid='1',display_id='50',epoch='12')
        profiles={name:dict(audit.CAL_BITS,profile=name) for name in ('instrument','record','spectrum')}
        cal=dict(status=status,profiles=profiles)
        text=line('CALIBRATION_STATUS',status)
        for name in ('instrument','record','spectrum'):
            text+=line('CALIBRATION_COEFFICIENTS',profiles[name])
        text+=line('CALIBRATION_END',dict(display_id='50',epoch='12'))
        audit.audit_calibration(cal,audit.UART(text))
        cal['profiles']['instrument']['offset1_bits']='c0976998'
        with self.assertRaises(ValueError):
            audit.audit_calibration(cal,audit.UART(text.replace('c0976997','c0976998')))

    def test_checkpoint_full_raw_uart_transaction(self):
        snap=snapshot_bundle()
        audit.audit_snapshot(snap,audit.UART(snapshot_log(snap)))

    def test_checkpoint_consistent_claims_cannot_mix_records_slots_epochs(self):
        mutations=[lambda s:s['memory']['view'].__setitem__('epoch','13'),
            lambda s:s['adc'].__setitem__('display_id','51'),
            lambda s:s['parameter']['status'].__setitem__('wave_crc','00000000'),
            lambda s:s['measurement']['context'].__setitem__('rate','50000000'),
            lambda s:s['fft']['values'].__setitem__('param_crc','00000000'),
            lambda s:s['calibration']['status'].__setitem__('record_id','00000000')]
        for index,mutate in enumerate(mutations):
            with self.subTest(index=index):
                snap=snapshot_bundle();mutate(snap)
                with self.assertRaises(ValueError):
                    audit.audit_snapshot(snap,audit.UART(snapshot_log(snap)))

    def test_checkpoint_measure_channels_cannot_borrow_separate_record(self):
        snap=snapshot_bundle();text=snapshot_log(snap)
        text=re.sub(r'(?m)^(MEASURE_CHANNEL .* ch=2)$',
            r'MEASURE_STATUS display_id=51 epoch=13 count=4096 rate=6250000\n\1',text)
        with self.assertRaises(ValueError):
            audit.audit_snapshot(snap,audit.UART(text))

    def stability_bundle(self):
        state,_=tf_fixture();text=''
        def acq(identity,epoch='21'):
            return dict(ui=dict(display_id=str(identity),running='1',epoch=epoch),adc=dict(available='1',id=str(identity)))
        running_before=acq(60);running_after=acq(61)
        state['running_save']=dict(before=running_before,after=running_after,tf=state['tf_actions'][8]['status'])
        state['stability']=dict(seconds=31.2,before=acq(70),samples=[acq(80),acq(90)],after=acq(100))
        values=[running_before,running_after,state['stability']['before'],*state['stability']['samples'],state['stability']['after']]
        for index,value in enumerate(values):
            text+='COMMANDS: synthetic-test-query\n'+line('UI_STATUS',value['ui'])+line('ADC_LIVE_STATUS',value['adc'])
            if index==2:
                text+=line('APP_DIAG',dict(ms='1000',frames='10',locked='1',pixel_locked='1',dma_errors='00000000'))
            text+='COMMANDS: synthetic-test-query-end\n'
            if index==4:
                text+=line('APP_DIAG',dict(ms='33000',frames='42',locked='1',pixel_locked='1',dma_errors='00000000'))
        return state,text

    def test_stability_uses_uart_frames_and_ids(self):
        state,text=self.stability_bundle()
        self.assertEqual(audit.audit_stability(state,audit.UART(text))['video_frame_delta'],32)

    def test_stability_cannot_borrow_diagnostics_outside_its_window(self):
        state,text=self.stability_bundle()
        diagnostics='\n'.join(row for row in text.splitlines() if row.startswith('APP_DIAG ')).splitlines(True)
        without='\n'.join(row for row in text.splitlines() if not row.startswith('APP_DIAG '))+'\n'
        text=diagnostics[0].rstrip()+'\n'+without+diagnostics[-1].rstrip()+'\n'
        with self.assertRaisesRegex(ValueError,'diagnostic'):
            audit.audit_stability(state,audit.UART(text))

    def test_stability_requires_intermediate_sample(self):
        state,text=self.stability_bundle();state['stability']['samples']=[]
        with self.assertRaisesRegex(ValueError,'intermediate'):
            audit.audit_stability(state,audit.UART(text))

    def test_stability_cannot_reuse_same_uart_transaction_for_two_samples(self):
        state,text=self.stability_bundle()
        state['stability']['samples'].insert(1,copy.deepcopy(state['stability']['samples'][0]))
        with self.assertRaisesRegex(ValueError,'ordered, framed'):
            audit.audit_stability(state,audit.UART(text))

    def test_stability_ui_adc_must_share_query_boundary(self):
        state,text=self.stability_bundle()
        text=text.replace('ADC_LIVE_STATUS available=1 id=80',
            'COMMANDS: artificial-separate-query\nADC_LIVE_STATUS available=1 id=80')
        with self.assertRaisesRegex(ValueError,'ordered, framed'):
            audit.audit_stability(state,audit.UART(text))

    def test_stability_fake_count_no_video_growth_dma_and_epoch_rejected(self):
        for bad in ('fakeids','frames','dma','epoch','duration'):
            with self.subTest(bad=bad):
                state,text=self.stability_bundle()
                if bad=='fakeids':state['stability']['after']['ui']['display_id']='101'
                if bad=='frames':text=text.replace('frames=42','frames=10')
                if bad=='dma':text=text.replace('dma_errors=00000000','dma_errors=00000001')
                if bad=='epoch':state['running_save']['after']['ui']['epoch']='22';text=text.replace('display_id=61 running=1 epoch=21','display_id=61 running=1 epoch=22')
                if bad=='duration':state['stability']['seconds']=29.9
                with self.assertRaises(ValueError):
                    audit.audit_stability(state,audit.UART(text))

    def test_full_real_evidence_and_tampered_copies(self):
        path=os.environ.get('STORAGE_EVIDENCE_REAL_RUN')
        if not path:
            self.skipTest('Set STORAGE_EVIDENCE_REAL_RUN for optional recorded-run regression; no devices are accessed')
        source=Path(path).resolve()
        require_files=('session.json','uart.log','source-on.json','original-raw.bin','export-original-raw.tcl')
        shutil.copytree(source,self.run,dirs_exist_ok=True)
        state=json.loads((self.run/'session.json').read_text(encoding='utf-8-sig'))
        state['run']=self.run.as_posix()
        for entry in state['package']:
            entry['Path']=(self.run/Path(entry['Path']).name).as_posix()
        copied_load=self.run/'readonly-load-copy'
        shutil.copytree(Path(state['load_run']),copied_load)
        state['load_run']=copied_load.as_posix()
        # Export scripts retain the exact copied run destination, as a genuine
        # relocated artifact package must. The original run is never changed.
        for file in self.run.glob('export-*.tcl'):
            file.write_text(file.read_text().replace(source.as_posix(),self.run.as_posix()))
        self.write_json('session.json',state)
        originals={name:(self.run/name).read_bytes() for name in require_files}
        load_uart=(copied_load/'uart.log').read_bytes()
        self.assertEqual(audit.audit_run(self.run)['state'],audit.PASS_STATE)
        for kind in ('rawbit','source','tcl','tfseq','slot','manifest','video',
                     'short-stability','short-stability-with-sample',
                     'short-stability-valid-samples','extra-load-write',
                     'extra-main-write-before','extra-main-write-after','duplicate-main-ack'):
            with self.subTest(kind=kind):
                for name,data in originals.items():(self.run/name).write_bytes(data)
                (copied_load/'uart.log').write_bytes(load_uart)
                changed=copy.deepcopy(state)
                if kind=='rawbit':
                    value=bytearray(originals['original-raw.bin']);value[0]^=1
                    (self.run/'original-raw.bin').write_bytes(value)
                if kind=='source':
                    value=source_fixture(True);value['raw_bswv']+=',PHSE,0';value['bswv']['PHSE']='0'
                    self.write_json('source-on.json',value)
                if kind=='tcl':
                    (self.run/'export-original-raw.tcl').write_text(originals['export-original-raw.tcl'].decode()+'\nfile delete anything\n')
                if kind=='tfseq':changed['tf_actions'][1]['sequence']=changed['tf_actions'][0]['sequence']
                if kind=='slot':changed['checkpoints'][4]['snapshot']['parameter']['status']['blob_crc']='00000000'
                if kind=='manifest':changed['package']=[item for item in changed['package'] if not item['Path'].endswith('record_blob.py')]
                if kind=='video':
                    text=originals['uart.log'].decode()
                    text=re.sub(r'(?m)^(APP_DIAG [^\r\n]*?)frames=\d+',r'\1frames=0',text)
                    (self.run/'uart.log').write_text(text)
                if kind.startswith('short-stability'):
                    changed['stability']=dict(seconds=31,before=changed['running_save']['before'],
                        samples=[] if kind=='short-stability' else [changed['running_save']['before']],
                        after=changed['running_save']['after'])
                if kind=='short-stability-valid-samples':
                    original=state['stability']['samples']
                    changed['stability']=dict(seconds=31,before=original[-3],samples=[original[-2]],after=original[-1])
                if kind=='extra-load-write':
                    extra='TF_COMMAND tfsave 0 QUEUED seq=999\n'
                    extra+=line('TF_STATUS',dict(busy='0',ready='1',result='0',seq='999',id='999',kind='0'))
                    extra+=line('TF_MEDIA',dict(cid=audit.CID,fat='3',sector_count='61067264'))
                    extra+=line('TF_BLOB',dict(address='00600000',bytes='16512',crc='12345678'))
                    (copied_load/'uart.log').write_bytes(load_uart+extra.encode())
                if kind.startswith('extra-main-write'):
                    extra='TF_COMMAND tfsave 0 QUEUED seq=999\n'
                    extra+=line('TF_STATUS',dict(busy='0',ready='1',result='0',seq='999',id='999',kind='0'))
                    extra+=line('TF_MEDIA',dict(cid=audit.CID,fat='3',sector_count='61067264'))
                    extra+=line('TF_BLOB',dict(address='00600000',bytes='16512',crc='12345678'))
                    raw=originals['uart.log']
                    (self.run/'uart.log').write_bytes(extra.encode()+raw if kind.endswith('before') else raw+extra.encode())
                if kind=='duplicate-main-ack':
                    text=originals['uart.log'].decode()
                    ack=next(row for row in text.splitlines() if row.startswith('TF_COMMAND '))
                    (self.run/'uart.log').write_bytes(originals['uart.log']+(ack+'\n').encode())
                self.write_json('session.json',changed)
                with self.assertRaises(ValueError):audit.audit_run(self.run)


if __name__=='__main__':
    unittest.main()
