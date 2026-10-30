"""Independent, read-only SCW1/RAM replay evidence audit. No board access."""
from pathlib import Path
import argparse
import hashlib
import json
import math
import re
import struct

from audit_hardware_trigger_evidence import load, checked_bytes
from audit_measurement_evidence import check_values
from record_blob import parse_record

TIMES_NS = tuple(m * 10 ** e for e in range(2, 9) for m in (1, 2, 5))
PREFS = ('time', 'source', 'scale', 'ch1_scale', 'ch2_scale', 'ch1_pos', 'ch2_pos',
    'dual', 'cursors', 'cursor_a', 'cursor_b', 'trig_channel', 'trig_normal',
    'trig_rising', 'level_mv', 'grid')
RECORD = ('record_address', 'record_crc', 'rate', 'conversion', 'filter', 'display_id', 'display_epoch')
ADC_RECORD = ('display_id', 'display_address', 'display_crc', 'display_rate',
    'display_epoch', 'display_filter', 'display_conversion_hz')
REQUIRED_CHECKS = (
    'memory-pointer-save-preserves-single', 'memory-saved-independent-blob-raw-frame',
    'memory-replay-independent-blob-raw-frame', 'memory-replay-CH1-has-no-period',
    'memory-replay-poller-stays-canceled', 'memory-replay-cursor-independent-blob-raw-frame',
    'memory-replay-rejects-single-autoset-trigger', 'memory-return-restores-live-before-poller',
    'memory-settings-restore-keeps-current-raw-rate', 'memory-running-save-does-not-restart',
    'memory-updated-live-independent-blob-raw-frame')


def equal_fields(before, after, fields):
    for field in fields:
        assert before[field] == after[field], (field, before[field], after[field])


def check_epoch(before, after, advance):
    assert (int(after['epoch']) - int(before['epoch'])) % 2 ** 32 == advance, ('epoch', before, after, advance)


def _forward(before, after):
    assert 0 < (int(after) - int(before)) % 2 ** 32 < 2 ** 31, ('did not advance', before, after)


def _log_index(log):
    out = {}
    for line in log.splitlines():
        fields = dict(re.findall(r'(\w+)=([^\s]+)', line))
        if fields:
            out.setdefault(line.split()[0], set()).add(tuple(sorted(fields.items())))
    return out


def _bind(index, prefix, fields):
    assert tuple(sorted(fields.items())) in index.get(prefix, ()), ('not in UART', prefix, fields)


def _snapshot(snapshot, index):
    ui, adc, memory, measurement = (snapshot[key] for key in ('ui', 'adc', 'memory', 'measurement'))
    status, view, context = memory['status'], memory['view'], measurement['context']
    for prefix, fields in [('UI_STATUS', ui), ('ADC_LIVE_STATUS', adc),
            ('MEMORY_STATUS', status), ('MEMORY_VIEW', view), ('MEASURE_STATUS', context)]:
        _bind(index, prefix, fields)
    for channel in measurement['channels']:
        _bind(index, 'MEASURE_CHANNEL', channel)
    assert ui['running'] == view['running'] == context['running'] == '0'
    assert adc['pending'] == '0'
    assert status['replay'] == view['replay'] and status['replay'] in ('0', '1')
    assert ui['display_id'] == adc['display_id'] == view['display_id'] == context['display_id']
    assert ui['seen_id'] == view['seen_id']
    assert ui['epoch'] == view['epoch'] == context['epoch']
    assert ui['time'] == view['time'] and ui['single_complete'] == view['single_complete']
    assert adc['display_address'] == view['record_address']
    assert adc['display_crc'].upper() == view['record_crc'].upper()
    assert adc['display_rate'] == view['rate'] == context['rate']
    assert adc['display_filter'] == view['filter']
    assert adc['display_conversion_hz'] == view['conversion']
    assert adc['conversion_hz'] == view['actual_conversion']
    assert adc['display_epoch'] == view['display_epoch']
    assert view['actual_conversion'] == '50000000' and view['record_bytes'] == '16384'
    assert context['count'] == '4096'
    assert context['channel'] == ('1' if ui['source'] == 'ADC1' else '2')
    assert abs(float(ui['view_start']) - float(view['start'])) <= .000501
    assert abs(float(ui['crossing']) - float(view['crossing'])) <= .000501
    if status['replay'] == '0':
        assert adc['id'] == view['display_id']
        assert view['record_address'] in ('13000000', '13004000')
    else:
        assert view['record_address'] not in ('13000000', '13004000')
    return snapshot


def _metadata(record, snapshot):
    meta, ui, memory = record['meta'], snapshot['ui'], snapshot['memory']
    status, view = memory['status'], memory['view']
    for field, value in (('record_id', ui['display_id']), ('rate_hz', view['rate']),
            ('conversion_hz', view['conversion']), ('filter_kind', view['filter']),
            ('capture_epoch', view['display_epoch']), ('trigger_channel', int(ui['trig_channel']) - 1),
            ('trigger_level_mv', ui['level_mv']), ('selected_channel', int(ui['source'][-1]) - 1)):
        assert meta[field] == int(value), (field, meta[field], value)
    assert meta['timebase_ns'] == TIMES_NS[int(ui['time'])]
    assert meta['scale_index'] == [int(ui['ch1_scale']), int(ui['ch2_scale'])]
    assert meta['position_mdiv'] == [int(ui['ch1_pos']), int(ui['ch2_pos'])]
    assert meta['cursor'] == [int(ui['cursor_a']), int(ui['cursor_b'])]
    flags = sum(int(ui[field]) << bit for bit, field in enumerate(
        ('triggered', 'trig_rising', 'trig_normal', 'dual', 'cursors', 'grid')))
    assert meta['flags'] == flags
    assert abs(record['view_start'] - float(view['start'])) <= 2 / 65536 + .00000051
    assert abs(record['crossing'] - float(view['crossing'])) <= 2 / 65536 + .00000051
    _slot(record, status)


def _slot(record, status):
    assert status['valid'] == '1' and status['blob_bytes'] == '16512'
    assert status['blob_crc'].upper() == record['crc32']
    for field, key in (('saved_id', 'record_id'), ('saved_rate', 'rate_hz'),
            ('saved_conversion', 'conversion_hz'), ('saved_filter', 'filter_kind'),
            ('saved_epoch', 'capture_epoch'), ('saved_time_ns', 'timebase_ns'), ('saved_flags', 'flags')):
        assert int(status[field]) == record['meta'][key], (field, status[field], record['meta'][key])


def _cursor(words, snapshot):
    ui, view = snapshot['ui'], snapshot['memory']['view']
    if ui['cursors'] == '0':
        return {}
    assert ui['cursor_valid'] == '1'
    rate = int(view['rate'])
    span = TIMES_NS[int(ui['time'])] * rate / 100000000
    positions = [int(ui[key]) for key in ('cursor_a', 'cursor_b')]
    voltages = []
    for position in positions:
        at = float(view['start']) + span * position / 1000
        assert math.isfinite(at) and 0 <= at <= 4095
        n = math.floor(at)
        fraction = at - n
        shift = 16 * (int(ui['source'][-1]) - 1)
        a = (words[n] >> shift) & 2047
        b = (words[n + 1] >> shift) & 2047 if fraction else a
        assert not (a | b) & 1024
        voltages.append((a + fraction * (b - a)) * 10 / 1023 - 5)
    dt = span * (positions[1] - positions[0]) / 1000 / rate * 1e6
    assert dt != 0
    expected = dict(cursor_dt_us=dt, cursor_inv_hz=1e6 / abs(dt), cursor_dv=voltages[1] - voltages[0])
    for key, value in expected.items():
        got = float(ui[key])
        tolerance = .001 if key == 'cursor_dv' else .000011
        assert math.isfinite(got) and abs(got - value) <= tolerance, (key, got, value)
    return expected


def _held(before, after, epoch=True):
    equal_fields(before['ui'], after['ui'], ('display_id', 'running', 'single_armed', 'single_complete') + (('epoch',) if epoch else ()))
    equal_fields(before['memory']['view'], after['memory']['view'], RECORD)


def audit(directory) -> dict:
    directory = Path(directory)
    session = load(directory / 'session.json')
    assert session['state'].startswith('verified-board-') and session['flash_written'] is False
    log_bytes = (directory / 'uart.log').read_bytes()
    assert hashlib.sha256(log_bytes).hexdigest().upper() == session['uart_sha256'].upper()
    log = log_bytes.decode('utf-8-sig')
    assert not re.search(r'APP_FATAL|APP_LOG_ERROR|ADC_LIVE_FAIL|DDR_FAIL|VIDEO_PRESENT_FAIL|VIDEO_STOP_TIMEOUT', log)
    assert not re.search(r'APP_DIAG[^\r\n]*dma_errors=(?!00000000)[0-9a-fA-F]{8}', log)
    index = _log_index(log)
    checks = session['memory_checks']
    assert [entry['test'] for entry in checks] == list(REQUIRED_CHECKS)
    check = {entry['test']: _snapshot(entry['snapshot'], index) for entry in checks}
    sources = session['memory_sources']
    assert [entry['name'] for entry in sources] == ['memory-source', 'memory-live-before', 'memory-new-live']
    source_raw = {}
    for entry, rate in zip(sources, (6250000, 50000000, 50000000)):
        ui, adc = entry['ui'], entry['adc']
        _bind(index, 'UI_STATUS', ui)
        _bind(index, 'ADC_LIVE_STATUS', adc)
        assert int(entry['rate']) == int(adc['display_rate']) == rate
        assert entry['id'] == ui['display_id'] == adc['display_id'] == adc['id']
        assert ui['running'] == '0' and adc['pending'] == '0'
        assert entry['crc32'].upper() == adc['display_crc'].upper()
        source_raw[entry['name']] = checked_bytes(directory / (entry['name'] + '.bin'), entry['crc32'], entry['sha256'], 16384)
    _forward(sources[0]['id'], sources[1]['id'])
    _forward(sources[1]['id'], sources[2]['id'])
    entries = session['memory_exports']
    assert [entry['name'] for entry in entries] == ['memory-saved', 'memory-replay', 'memory-replay-cursor', 'memory-updated-live']
    records, snapshots, reports = [], [], []
    for entry in entries:
        name = entry['name']
        snapshot = _snapshot(entry, index)
        memory, view = entry['memory'], entry['memory']['view']
        assert int(entry['rate']) == int(view['rate'])
        assert entry['id'] == view['display_id']
        assert entry['blob_crc'].upper() == memory['status']['blob_crc'].upper()
        assert entry['record_crc'].upper() == view['record_crc'].upper()
        blob = checked_bytes(directory / (name + '.scw'), entry['blob_crc'], entry['blob_sha256'], 16512)
        record = parse_record(blob)
        raw = checked_bytes(directory / (name + '.bin'), entry['record_crc'], entry['record_sha256'], 16384)
        assert record['raw_data'] == raw
        assert record['payload_crc32'] == entry['record_crc'].upper()
        _slot(record, memory['status'])
        values = check_values(record['words'], int(entry['rate']), entry['measurement'])
        cursor = _cursor(record['words'], snapshot)
        if 'frame_crc' in entry:
            checked_bytes(directory / (name + '-frame.bin'), entry['frame_crc'], entry['frame_sha256'], 1843200)
            assert re.search(r'SNAPSHOT_READY[^\r\n]*bytes=1843200[^\r\n]*crc=' + re.escape(entry['frame_crc']), log, re.I)
        else:
            raise AssertionError('missing required memory DDR frame: ' + name)
        equal_fields(snapshot['ui'], check[name + '-independent-blob-raw-frame']['ui'], PREFS + ('epoch', 'display_id', 'running', 'single_complete'))
        equal_fields(view, check[name + '-independent-blob-raw-frame']['memory']['view'], RECORD)
        reports.append(dict(name=name, sha256=record['sha256'], record_id=record['meta']['record_id'],
            rate=record['meta']['rate_hz'], cursor=cursor, values=values))
        records.append(record)
        snapshots.append(snapshot)
    saved, replay, cursor_replay, updated = snapshots
    initial = records[0]
    assert initial['raw_data'] == source_raw['memory-source']
    assert [record['sha256'] for record in records[:3]] == [initial['sha256']] * 3
    assert records[3]['sha256'] != initial['sha256']
    _metadata(initial, saved)
    _metadata(records[3], updated)
    assert initial['meta']['trigger_after_index'] == int(sources[0]['adc']['hw_after'])
    for name, snapshot in check.items():
        _slot(records[3] if name in REQUIRED_CHECKS[-2:] else initial, snapshot['memory']['status'])
    initial_snapshot = _snapshot(session['memory_saved_snapshot'], index)
    _held(initial_snapshot, saved)
    equal_fields(sources[0]['ui'], saved['ui'], PREFS + ('epoch', 'display_id', 'single_armed', 'single_complete'))
    equal_fields(sources[0]['adc'], saved['adc'], ('display_address', 'display_crc', 'display_rate', 'display_epoch', 'display_filter', 'display_conversion_hz'))
    before = _snapshot(session['memory_before_replay'], index)
    assert before['ui']['display_id'] == sources[1]['id'] and before['memory']['view']['rate'] == '50000000'
    equal_fields(sources[1]['adc'], before['adc'], ADC_RECORD)
    for item in (replay, cursor_replay):
        assert item['memory']['status']['replay'] == '1'
        assert item['memory']['view']['rate'] == '6250000'
        assert item['memory']['view']['conversion'] == item['memory']['view']['actual_conversion'] == '50000000'
        assert item['memory']['view']['display_epoch'] == str(initial['meta']['capture_epoch'])
        assert item['ui']['display_id'] == str(initial['meta']['record_id'])
        equal_fields(before['adc'], item['adc'], ('id', 'address', 'crc32'))
        assert int(item['ui']['seen_id']) >= int(before['ui']['seen_id'])
        check_epoch(before['ui'], item['ui'], 1)
        assert item['measurement']['channels'] == saved['measurement']['channels']
    assert replay['memory']['view']['record_address'] != before['memory']['view']['record_address']
    protected_entry = entries[1]
    assert protected_entry['protected_address'] == before['memory']['view']['record_address']
    protected = checked_bytes(directory / 'memory-replay-live-held.bin', protected_entry['protected_crc'], protected_entry['protected_sha256'], 16384)
    assert protected == source_raw['memory-live-before']
    assert protected_entry['protected_sha256'].upper() == sources[1]['sha256'].upper()
    wait = session['memory_replay_wait']
    assert math.isfinite(float(wait['elapsed_ms'])) and float(wait['elapsed_ms']) >= 1000
    wait_before, wait_after = (_snapshot(wait[key], index) for key in ('before', 'after'))
    _held(wait_before, wait_after)
    equal_fields(wait_before['adc'], wait_after['adc'], ('id', 'pending'))
    equal_fields(wait_before['memory']['status'], wait_after['memory']['status'], ('blob_crc', 'saved_id'))
    _slot(initial, wait_before['memory']['status'])
    _slot(initial, wait_after['memory']['status'])
    assert wait_after['adc']['pending'] == '0'
    assert int(wait_after['ui']['seen_id']) >= int(wait_before['ui']['seen_id'])
    empty = check['memory-replay-CH1-has-no-period']
    assert empty['ui']['source'] == 'ADC1' and empty['measurement']['channels'][0]['frequency_valid'] == '0'
    assert float(empty['measurement']['channels'][0]['period_us']) == 0
    _held(replay, empty)
    rejected = session['memory_replay_rejections']
    rb, ra = (_snapshot(rejected[key], index) for key in ('before', 'after'))
    _held(rb, ra)
    equal_fields(rb['ui'], ra['ui'], PREFS)
    equal_fields(rb['memory']['status'], ra['memory']['status'], ('blob_crc', 'saved_id', 'saved_rate', 'saved_epoch'))
    markers = ['MEMORY_COMMAND memsave REJECTED', 'SINGLE_COMMAND REJECTED', 'AUTOSET_COMMAND REJECTED',
        'TRIGGER_REJECTED', 'TRIGGER_SOURCE_REJECTED', 'TRIGGERMODE_REJECTED']
    assert rejected['markers'] == markers
    at = 0
    for marker in markers:
        found = log.find(marker, at)
        assert found >= at, ('missing replay rejection', marker)
        at = found + len(marker)
    returning = session['memory_return']
    returned = _snapshot(returning['after'], index)
    _snapshot(returning['before'], index)
    equal_fields(before['ui'], returning['before']['ui'], PREFS + ('epoch', 'display_id'))
    assert returning['start_ack']['running'] == '1' and returning['stop_ack']['running'] == '0'
    _bind(index, 'UI_STATUS', returning['start_ack'])
    _bind(index, 'UI_STATUS', returning['stop_ack'])
    check_epoch(ra['ui'], returning['start_ack'], 1)
    check_epoch(returning['start_ack'], returning['stop_ack'], 1)
    check_epoch(ra['ui'], returned['ui'], 2)
    equal_fields(before['ui'], returned['ui'], PREFS + ('display_id',))
    equal_fields(before['memory']['view'], returned['memory']['view'], RECORD)
    assert before['measurement']['channels'] == returned['measurement']['channels']
    assert returned['memory']['status']['replay'] == '0' and returned['ui']['single_complete'] == '0'
    assert int(returned['ui']['seen_id']) >= int(ra['ui']['seen_id'])
    assert sources[2]['adc']['display_epoch'] == sources[2]['ui']['epoch']
    _forward(returned['ui']['epoch'], sources[2]['ui']['epoch'])
    restoring = session['memory_settings_restore']
    restore_before, restore_after = (_snapshot(restoring[key], index) for key in ('before', 'after'))
    assert restore_before['ui']['display_id'] == restore_after['ui']['display_id'] == sources[2]['id']
    equal_fields(sources[2]['adc'], restore_before['adc'], ADC_RECORD)
    equal_fields(restore_before['memory']['view'], restore_after['memory']['view'], RECORD)
    equal_fields(saved['ui'], restore_after['ui'], PREFS)
    check_epoch(restore_before['ui'], restore_after['ui'], 1)
    assert restore_after['ui']['single_armed'] == restore_after['ui']['single_complete'] == '0'
    assert restore_after['memory']['view']['rate'] == '50000000'
    assert restore_before['measurement']['channels'] == restore_after['measurement']['channels']
    update = session['memory_live_update']
    update_snapshot = _snapshot(update['snapshot'], index)
    running_before = update['running_before']
    _bind(index, 'UI_STATUS', running_before['ui'])
    _bind(index, 'ADC_LIVE_STATUS', running_before['adc'])
    assert running_before['ui']['running'] == '1'
    assert running_before['ui']['epoch'] == running_before['adc']['display_epoch']
    assert running_before['adc']['display_rate'] == '50000000'
    _held(update_snapshot, updated)
    _bind(index, 'UI_STATUS', update['save_ack'])
    _bind(index, 'UI_STATUS', update['stop_ack'])
    assert update['save_ack']['running'] == '1' and update['stop_ack']['running'] == '0'
    check_epoch(running_before['ui'], update['save_ack'], 0)
    check_epoch(update['save_ack'], update['stop_ack'], 1)
    equal_fields(update['stop_ack'], updated['ui'], ('epoch', 'display_id', 'running'))
    assert update['save_ack']['display_id'] == updated['ui']['display_id']
    assert updated['ui']['display_id'] != saved['ui']['display_id']
    _forward(sources[2]['id'], updated['ui']['display_id'])
    assert records[3]['meta']['rate_hz'] == 50000000 and updated['memory']['status']['replay'] == '0'
    assert records[3]['meta']['capture_epoch'] == int(update['save_ack']['epoch'])
    final = session['memory_final']
    _bind(index, 'MEMORY_STATUS', final['status'])
    _bind(index, 'MEMORY_VIEW', final['view'])
    assert final['status']['replay'] == final['view']['replay'] == '0'
    assert final['view']['running'] == '1' and final['view']['rate'] == '6250000'
    return dict(state='verified-offline-independent-memory-evidence', blobs=4, raw_exports=7, protected_live_exports=1,
        ddr_frames=4, controls=len(checks), records=reports, persistent_storage='not-implemented-RAM-only',
        calibrated=False, physical_panel='separate-user-feedback')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run', type=Path)
    args = parser.parse_args()
    result = audit(args.run)
    (args.run / 'memory-evidence-audit.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print('MEMORY_EVIDENCE_PASS', json.dumps(result))


if __name__ == '__main__':
    main()
