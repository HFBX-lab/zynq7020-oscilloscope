"""Independent read-only SCP1 parameter/SCW1/raw/frame board evidence audit."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

from audit_hardware_trigger_evidence import load, checked_bytes
from audit_measurement_evidence import check_values
from audit_memory_evidence import (_log_index, _bind, _snapshot as memory_snapshot,
    _slot, equal_fields, check_epoch)
from record_blob import parse_record
from settings_blob import parse_settings, PROFILE_TIMEBASE_NS, TIMEBASE_NS

PREFS = ('time', 'source', 'ch1_scale', 'ch2_scale', 'ch1_pos', 'ch2_pos',
         'trig_channel', 'trig_normal', 'trig_rising', 'level_mv', 'dual',
         'grid', 'cursors', 'cursor_a', 'cursor_b', 'lang')
RAW = ('record_address', 'record_crc', 'rate', 'conversion', 'filter',
       'display_id', 'display_epoch')
SLOT = ('valid', 'blob_address', 'blob_bytes', 'blob_crc', 'saved_id',
        'saved_rate', 'saved_conversion', 'saved_filter', 'saved_epoch',
        'saved_time_ns', 'saved_flags')
EXPORTS = ('param-saved-en', 'param-restored-en', 'param-default-zh',
           'param-updated-running')
CHECKS = ('parameters-empty-restore-refused',
    'parameters-pointer-save-preserves-single-and-wave',
    'param-saved-en-independent-settings-wave-raw-frame',
    'parameters-pointer-restore-atomic-and-frozen',
    'param-restored-en-independent-settings-wave-raw-frame',
    'parameters-default-exact-keeps-both-slots',
    'param-default-zh-independent-settings-wave-raw-frame',
    'parameters-replay-all-actions-refused',
    'parameters-running-save-then-stop',
    'param-updated-running-independent-settings-wave-raw-frame')
DEFAULTS = dict(timebase_ns=200000, selected_channel=0, trigger_channel=0,
    trigger_level_mv=500, flags=9, scale_index=[2, 2], position_mdiv=[0, 0],
    cursor=[250, 750], language=0)


def check_config(settings, config):
    expected = dict(time_ns=settings['timebase_ns'],
        origin_conversion=settings['origin_conversion_hz'],
        selected_channel=settings['selected_channel'], trigger_channel=settings['trigger_channel'],
        level_mv=settings['trigger_level_mv'], flags=settings['flags'],
        ch1_scale=settings['scale_index'][0], ch2_scale=settings['scale_index'][1],
        ch1_pos=settings['position_mdiv'][0], ch2_pos=settings['position_mdiv'][1],
        cursor_a=settings['cursor'][0], cursor_b=settings['cursor'][1], language=settings['language'])
    assert set(config) == set(expected), ('parameter configuration schema', config)
    for key, value in expected.items():
        assert int(config[key]) == value, ('saved config', key, config[key], value)


def check_settings_ui(settings, ui, actual_conversion):
    times = [ns for ns in TIMEBASE_NS if ns in PROFILE_TIMEBASE_NS[actual_conversion]]
    assert 0 <= int(ui['time']) < len(times)
    assert times[int(ui['time'])] == settings['timebase_ns'], ('physical timebase', settings, ui)
    expected = dict(source='ADC' + str(settings['selected_channel'] + 1),
        trig_channel=str(settings['trigger_channel'] + 1), level_mv=str(settings['trigger_level_mv']),
        ch1_scale=str(settings['scale_index'][0]), ch2_scale=str(settings['scale_index'][1]),
        ch1_pos=str(settings['position_mdiv'][0]), ch2_pos=str(settings['position_mdiv'][1]),
        cursor_a=str(settings['cursor'][0]), cursor_b=str(settings['cursor'][1]),
        lang='en' if settings['language'] else 'zh-CN')
    for bit, key in enumerate(('trig_rising', 'trig_normal', 'dual', 'grid', 'cursors')):
        expected[key] = str((settings['flags'] >> bit) & 1)
    equal_fields(ui, expected, expected)


def _parameter(parameter, index):
    _bind(index, 'PARAM_STATUS', parameter['status'])
    _bind(index, 'PARAM_CONFIG', parameter['config'])
    _bind(index, 'PARAM_END', {'epoch': parameter['status']['epoch']})
    p = parameter['status']
    assert p['actual_conversion'] == '50000000' and p['blob_bytes'] == '96'
    assert p['valid'] in ('0', '1') and p['replay'] in ('0', '1')


def _snapshot(snapshot, index):
    memory_snapshot(snapshot, index)
    _parameter(snapshot['parameter'], index)
    p, ui, view, wave = snapshot['parameter']['status'], snapshot['ui'], snapshot['memory']['view'], snapshot['memory']['status']
    equal_fields(p, ui, ('epoch', 'running', 'display_id'))
    equal_fields(p, view, ('record_address', 'record_bytes', 'record_crc', 'rate', 'actual_conversion', 'replay'))
    assert p['wave_valid'] == wave['valid'] and p['wave_crc'].upper() == wave['blob_crc'].upper()
    return snapshot


def _acq(snapshot, index):
    _bind(index, 'UI_STATUS', snapshot['ui'])
    _bind(index, 'ADC_LIVE_STATUS', snapshot['adc'])


def _held(before, after, advance):
    check_epoch(before['ui'], after['ui'], advance)
    equal_fields(before['ui'], after['ui'], ('display_id', 'seen_id', 'running'))
    equal_fields(before['memory']['view'], after['memory']['view'], RAW)
    equal_fields(before['memory']['status'], after['memory']['status'], SLOT)
    assert before['measurement']['channels'] == after['measurement']['channels'], 'measurement cache changed'


def _same_saved(before, after):
    equal_fields(before['parameter']['status'], after['parameter']['status'],
                 ('valid', 'blob_address', 'blob_bytes', 'blob_crc'))
    assert before['parameter']['config'] == after['parameter']['config'], 'saved configuration changed'


def _export_addresses(directory, name, snapshot, index, frame_crc):
    """Bind the exact exported DDR ranges to the UART snapshot and frame."""
    script = (directory / ('export-' + name + '.tcl')).read_text(encoding='utf-8-sig')
    p, memory = snapshot['parameter']['status'], snapshot['memory']['status']
    expected = {'.scp': (p['blob_address'], 24), '.scw': (memory['blob_address'], 4128),
                '.bin': (p['record_address'], 4096)}
    frame_address = None
    for suffix, count in (('.scp', 24), ('.scw', 4128), ('.bin', 4096), ('-frame.bin', 460800)):
        matches = re.findall(r'mrd -bin -file \{[^{}\r\n]*[/\\]' + re.escape(name + suffix)
                             + r'\} 0x([0-9a-fA-F]{8}) (\d+)', script)
        assert len(matches) == 1 and int(matches[0][1]) == count, ('export range missing', name, suffix)
        address = matches[0][0].lower()
        if suffix in expected:
            assert address == expected[suffix][0].lower(), ('wrong exported address', name, suffix)
        else:
            assert address in ('10000000', '10200000'), ('unexpected frame DDR address', address)
            frame_address = address
    frames = [dict(fields) for fields in index.get('SNAPSHOT_READY', ())]
    assert any(frame.get('addr', '').lower() == frame_address
               and frame.get('crc', '').upper() == frame_crc.upper()
               and frame.get('bytes') == '1843200' and frame.get('format') == 'BGR888'
               for frame in frames), ('frame range/CRC absent from UART', name)


def audit(directory):
    directory = Path(directory)
    session = load(directory / 'session.json')
    assert session['state'].startswith('verified-board-') and session['flash_written'] is False
    assert [item['test'] for item in session['parameter_checks']] == list(CHECKS)
    entries = session['parameter_exports']
    assert [entry['name'] for entry in entries] == list(EXPORTS)
    log_bytes = (directory / 'uart.log').read_bytes()
    assert hashlib.sha256(log_bytes).hexdigest().upper() == session['uart_sha256'].upper()
    log = log_bytes.decode('utf-8-sig')
    assert not re.search(r'APP_FATAL|APP_LOG_ERROR|ADC_LIVE_FAIL|DDR_FAIL|VIDEO_PRESENT_FAIL|VIDEO_STOP_TIMEOUT', log)
    assert not re.search(r'APP_DIAG[^\r\n]*dma_errors=(?!00000000)[0-9a-fA-F]{8}', log)
    index = _log_index(log)
    for control in session['controls']:
        _acq(control, index)
    control_by_name = {control['test']: control for control in session['controls']}
    checks = {item['test']: _snapshot(item['snapshot'], index) for item in session['parameter_checks']}
    transitions = {}
    for key in ('empty', 'saved', 'loaded', 'default', 'rejected'):
        transitions[key] = tuple(_snapshot(session['parameter_' + key][side], index) for side in ('before', 'after'))
    empty_before, empty = transitions['empty']
    _held(empty_before, empty, 0)
    equal_fields(empty_before['ui'], empty['ui'], PREFS + ('single_armed', 'single_complete', 'triggered', 'waiting'))
    assert empty['parameter']['status']['valid'] == '0' and empty['parameter']['status']['notice'] == '1'
    _same_saved(empty_before, empty)
    save_before, saved = transitions['saved']
    _held(save_before, saved, 0)
    equal_fields(save_before['ui'], saved['ui'], PREFS + ('single_armed', 'single_complete', 'triggered', 'waiting', 'view_start', 'crossing'))
    assert saved['parameter']['status']['valid'] == '1' and saved['parameter']['status']['notice'] == '2'
    assert saved['ui']['single_complete'] == '1' and saved['ui']['lang'] == 'en' and saved['ui']['grid'] == '0'
    load_before, loaded = transitions['loaded']
    _held(load_before, loaded, 1)
    equal_fields(saved['ui'], loaded['ui'], PREFS)
    _same_saved(saved, loaded)
    default_before, default = transitions['default']
    _held(default_before, default, 1)
    _same_saved(saved, default)
    check_settings_ui(DEFAULTS, default['ui'], 50000000)
    for item in (loaded, default):
        assert item['ui']['single_armed'] == item['ui']['single_complete'] == item['ui']['triggered'] == '0'
        assert float(item['ui']['view_start']) == float(item['ui']['crossing']) == 0
    fresh = control_by_name['parameters-default-resume-new-epoch-empty-CH1']
    check_settings_ui(DEFAULTS, fresh['ui'], 50000000)
    assert fresh['ui']['running'] == '1' and fresh['ui']['source'] == 'ADC1'
    assert fresh['adc']['display_epoch'] == fresh['ui']['epoch']
    check_epoch(default['ui'], fresh['ui'], 1)
    assert int(fresh['ui']['display_id']) > int(default['ui']['display_id'])
    rb, ra = transitions['rejected']
    _held(rb, ra, 0)
    equal_fields(rb['ui'], ra['ui'], PREFS + ('single_armed', 'single_complete', 'triggered', 'waiting'))
    _same_saved(rb, ra)
    assert rb['parameter']['status']['replay'] == ra['parameter']['status']['replay'] == '1'
    for name, snapshot in (('parameters-empty-restore-refused', empty),
            ('parameters-pointer-save-preserves-single-and-wave', saved),
            ('parameters-pointer-restore-atomic-and-frozen', loaded),
            ('parameters-default-exact-keeps-both-slots', default),
            ('parameters-replay-all-actions-refused', ra)):
        _held(snapshot, checks[name], 0)
        equal_fields(snapshot['ui'], checks[name]['ui'], PREFS + ('single_armed', 'single_complete'))
        _same_saved(snapshot, checks[name])

    running = session['parameter_running_save']
    _acq(running['before'], index)
    for ack in ('save_ack', 'stop_ack'):
        _bind(index, 'UI_STATUS', running[ack])
    updated = _snapshot(running['snapshot'], index)
    assert running['before']['ui']['running'] == running['save_ack']['running'] == '1'
    assert running['stop_ack']['running'] == updated['ui']['running'] == '0'
    check_epoch(running['before']['ui'], running['save_ack'], 0)
    check_epoch(running['save_ack'], running['stop_ack'], 1)
    equal_fields(running['before']['ui'], running['save_ack'], PREFS)
    equal_fields(running['save_ack'], updated['ui'], PREFS + ('display_id', 'seen_id'))
    equal_fields(running['stop_ack'], updated['ui'], PREFS + ('epoch', 'display_id', 'seen_id'))
    equal_fields(saved['memory']['status'], updated['memory']['status'], SLOT)
    assert updated['memory']['view']['display_epoch'] == running['save_ack']['epoch']
    assert updated['memory']['view']['rate'] == '6250000'
    assert int(updated['ui']['display_id']) > int(default['ui']['display_id'])

    demo = session['parameter_demo']
    for side in ('before', 'after'):
        _acq(demo[side], index)
    _parameter(demo['parameter'], index)
    equal_fields(demo['before']['ui'], demo['after']['ui'], PREFS + ('epoch', 'display_id', 'seen_id', 'running'))
    assert demo['after']['ui']['source'] == 'SIM'
    equal_fields(updated['parameter']['status'], demo['parameter']['status'], ('valid', 'blob_crc', 'wave_valid', 'wave_crc'))
    assert updated['parameter']['config'] == demo['parameter']['config']
    for command in ('paramsave', 'paramload', 'paramdefault'):
        assert len(re.findall(r'(?m)^PARAM_COMMAND ' + command + r' REJECTED\r?$', log)) >= 2

    records, raws, wave_bytes, reports = [], [], [], []
    owners = (saved, loaded, default, updated)
    for entry, owner in zip(entries, owners):
        name = entry['name']
        snapshot = _snapshot(entry['snapshot'], index)
        _held(owner, snapshot, 0)
        equal_fields(owner['ui'], snapshot['ui'], PREFS + ('single_armed', 'single_complete'))
        _same_saved(owner, snapshot)
        check = checks[name + '-independent-settings-wave-raw-frame']
        _held(snapshot, check, 0)
        _same_saved(snapshot, check)
        p, wave, view = snapshot['parameter']['status'], snapshot['memory']['status'], snapshot['memory']['view']
        assert entry['settings_crc'].upper() == p['blob_crc'].upper()
        assert entry['wave_crc'].upper() == p['wave_crc'].upper() == wave['blob_crc'].upper()
        assert entry['raw_crc'].upper() == p['record_crc'].upper() == view['record_crc'].upper()
        scp = checked_bytes(directory / (name + '.scp'), entry['settings_crc'], entry['settings_sha256'], 96)
        settings = parse_settings(scp)
        check_config(settings['settings'], snapshot['parameter']['config'])
        blob = checked_bytes(directory / (name + '.scw'), entry['wave_crc'], entry['wave_sha256'], 16512)
        record = parse_record(blob)
        _slot(record, wave)
        raw = checked_bytes(directory / (name + '.bin'), entry['raw_crc'], entry['raw_sha256'], 16384)
        values = check_values(struct.unpack('<4096I', raw), int(view['rate']), snapshot['measurement'])
        checked_bytes(directory / (name + '-frame.bin'), entry['frame_crc'], entry['frame_sha256'], 1843200)
        _export_addresses(directory, name, snapshot, index, entry['frame_crc'])
        records.append(settings)
        raws.append(raw)
        wave_bytes.append(blob)
        reports.append(dict(name=name, settings_sha256=settings['sha256'],
            settings=settings['settings'], raw_sha256=entry['raw_sha256'],
            waveform_sha256=record['sha256'], values=values))
    assert len({record['sha256'] for record in records[:3]}) == 1, 'restore/default modified saved SCP'
    assert raws[0] == raws[1] == raws[2], 'restore/default modified frozen raw'
    assert all(blob == wave_bytes[0] for blob in wave_bytes), 'parameter operation modified waveform slot'
    prior_wave = session['memory_exports'][-1]
    assert hashlib.sha256(wave_bytes[0]).hexdigest().upper() == prior_wave['blob_sha256'].upper()
    check_settings_ui(records[0]['settings'], save_before['ui'], 50000000)
    check_settings_ui(records[0]['settings'], loaded['ui'], 50000000)
    check_settings_ui(records[3]['settings'], running['save_ack'], 50000000)
    assert records[0]['settings']['origin_conversion_hz'] == records[3]['settings']['origin_conversion_hz'] == 50000000

    final = session['parameter_final']
    _acq(final, index)
    _parameter(final['parameter'], index)
    assert final['ui']['epoch'] == final['adc']['display_epoch'] == final['parameter']['status']['epoch']
    assert final['ui']['running'] == final['parameter']['status']['running'] == '1'
    assert final['ui']['source'] == 'ADC2' and final['ui']['time'] == '6' and final['ui']['lang'] == 'zh-CN'
    assert final['ui']['trig_normal'] == final['ui']['cursors'] == '0' and final['ui']['dual'] == '1'
    assert final['adc']['display_rate'] == final['parameter']['status']['rate'] == '6250000'
    assert final['parameter']['status']['actual_conversion'] == '50000000'
    equal_fields(updated['parameter']['status'], final['parameter']['status'], ('valid', 'blob_crc', 'wave_valid', 'wave_crc'))
    check_config(records[3]['settings'], final['parameter']['config'])
    equal_fields(final['ui'], control_by_name['final-parameter-CH2-10us-live-auto']['ui'],
                 PREFS + ('epoch', 'display_id', 'running'))
    return dict(state='verified-offline-independent-settings-evidence', controls=len(CHECKS),
        settings_blobs=4, raw_exports=4, waveform_exports=4, ddr_frames=4, records=reports,
        persistent_storage='not-implemented-RAM-only', physical_panel='separate-user-feedback')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run', type=Path)
    args = parser.parse_args()
    result = audit(args.run)
    (args.run / 'settings-evidence-audit.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print('SETTINGS_EVIDENCE_PASS', json.dumps(result))


if __name__ == '__main__':
    main()
