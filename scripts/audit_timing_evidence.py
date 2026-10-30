"""Check new timing raw, UART, ownership, replay payload and frames independently."""
from pathlib import Path
import hashlib
import json
import re
import struct
import sys
import zlib
from timing_reference import compare_timing
from audit_memory_evidence import _log_index, _bind
from audit_settings_evidence import _snapshot


def atomic_timing_frames(log):
    frames, context, rows = set(), None, []
    for line in log.splitlines():
        if line.startswith('MEASURE_STATUS '):
            context = dict(re.findall(r'(\w+)=([^\s]+)', line))
            rows = []
        elif line.startswith('MEASURE_TIMING ') and context is not None:
            rows.append(dict(re.findall(r'(\w+)=([^\s]+)', line)))
        elif line.startswith('MEASURE_END ') and context is not None:
            end = dict(re.findall(r'(\w+)=([^\s]+)', line))
            assert end['display_id'] == context['display_id'], 'Mixed UART timing frame'
            if len(rows) == 2:
                assert [r['ch'] for r in rows] == ['1', '2']
                frames.add((tuple(sorted(context.items())),
                            tuple(tuple(sorted(r.items())) for r in rows)))
            context = None
    assert frames, 'No complete UART timing frames'
    return frames


def bind_timing(snapshot, index, frames):
    _snapshot(snapshot, index)
    timing = snapshot['timing']
    assert timing['context'] == snapshot['measurement']['context'], 'Timing/basic mixed frame context'
    _bind(index, 'MEASURE_STATUS', timing['context'])
    _bind(index, 'MEASURE_END', {'display_id': timing['context']['display_id']})
    for row in timing['channels']:
        _bind(index, 'MEASURE_TIMING', row)
    identity = (tuple(sorted(timing['context'].items())),
                tuple(tuple(sorted(row.items())) for row in timing['channels']))
    assert identity in frames, 'Timing values not from the same UART frame'


def check_export_tcl(directory, name, snapshot):
    script = (directory / ('export-' + name + '.tcl')).read_text(encoding='ascii')
    reads = {Path(p.replace('\\', '/')).name: (int(a, 16), int(n)) for p, a, n in
             re.findall(r'mrd\s+-bin\s+-file\s+\{([^}]+)\}\s+0x([a-fA-F0-9]+)\s+(\d+)', script)}
    assert reads[name + '.bin'] == (int(snapshot['memory']['view']['record_address'], 16), 4096), 'Export Tcl raw address'
    assert reads[name + '-frame.bin'][0] in (0x10000000, 0x10200000)
    assert reads[name + '-frame.bin'][1] == 460800
    if snapshot['memory']['status']['replay'] == '1':
        assert reads[name + '.scw'] == (int(snapshot['memory']['status']['blob_address'], 16), 4128)


def checked(path, sha, crc, size):
    data = path.read_bytes()
    assert len(data) == size, (path.name, 'length')
    assert hashlib.sha256(data).hexdigest().lower() == sha.lower(), (path.name, 'SHA')
    assert f'{zlib.crc32(data):08x}' == crc.lower(), (path.name, 'CRC')
    return data


def audit(directory, session=None):
    directory = Path(directory)
    if session is None:
        session = json.loads((directory / 'session.json').read_text(encoding='utf-8-sig'))
    assert session['state'].startswith('verified-board-')
    assert session['flash_written'] is False
    log = (directory / 'uart.log').read_text(encoding='utf-8-sig')
    index, frames = _log_index(log), atomic_timing_frames(log)
    entries = session['timing_exports']
    assert [e['name'] for e in entries] == [
        'timing-raw50m-ch2', 'timing-raw50m-ch1', 'timing-cic6m25',
        'memory-timing-replay', 'timing-cic625k']
    assert [int(e['rate']) for e in entries] == [50000000, 50000000, 6250000, 50000000, 625000]
    reports, payloads = [], []
    for entry in entries:
        name, rate = entry['name'], int(entry['rate'])
        s = entry['timing_snapshot']
        bind_timing(s, index, frames)
        check_export_tcl(directory, name, s)
        context = s['timing']['context']
        assert context['panel'] == context['timing'] == '1'
        assert context['running'] == s['ui']['running'] == '0'
        assert context['display_id'] == s['ui']['display_id'] == str(entry['id'])
        assert context['epoch'] == s['ui']['epoch']
        assert int(context['rate']) == rate == int(s['memory']['view']['rate'])
        assert context['count'] == '4096'
        crc = entry.get('record_crc', entry.get('crc32'))
        sha = entry.get('record_sha256', entry.get('sha256'))
        assert crc.lower() == s['memory']['view']['record_crc'].lower()
        raw = checked(directory / (name + '.bin'), sha, crc, 16384)
        payloads.append(raw)
        words = struct.unpack('<4096I', raw)
        assert [c['ch'] for c in s['timing']['channels']] == ['1', '2']
        values = [compare_timing(words, rate, ch, s['timing']['channels'][ch]) for ch in range(2)]
        assert not values[0]['pulse_valid'], 'Empty CH1 invented timing'
        if rate >= 6250000:
            assert values[1]['pulse_valid'] and abs(values[1]['period_us'] - 20) < 1
        checked(directory / (name + '-frame.bin'), entry['frame_sha256'], entry['frame_crc'], 1843200)
        if name.startswith('memory-'):
            blob = checked(directory / (name + '.scw'), entry['blob_sha256'], entry['blob_crc'], 16512)
            assert blob[128:] == raw and raw == payloads[0], 'Replay changed original payload'
            assert s['memory']['status']['replay'] == '1'
        reports.append(dict(name=name, rate=rate, id=entry['id'], channels=values))
    assert payloads[0] == payloads[1] == payloads[3]
    returned = session['timing_return']
    before, after = returned['before'], returned['after']
    bind_timing(before, index, frames)
    bind_timing(after, index, frames)
    for key in ('record_address', 'record_crc', 'rate', 'conversion', 'filter', 'display_id', 'display_epoch'):
        assert before['memory']['view'][key] == after['memory']['view'][key], ('return changed raw', key)
    assert before['timing']['channels'] == after['timing']['channels'], 'return changed timing cache'
    names = [c['test'] for c in session['timing_checks']]
    for check in session['timing_checks']:
        bind_timing(check['snapshot'], index, frames)
    for required in ('timing-frozen50m-keeps-source-rate', 'timing-invalid-page-command-rejected',
                     'timing-return-live-cache-restored'):
        assert required in names
    frozen = next(c['snapshot'] for c in session['timing_checks']
                  if c['test'] == 'timing-frozen50m-keeps-source-rate')
    first = entries[0]['timing_snapshot']
    assert frozen['ui']['time'] == '6' and first['ui']['time'] == '5'
    assert (int(frozen['ui']['epoch']) - int(first['ui']['epoch'])) % 2**32 == 1
    for key in ('record_address', 'record_crc', 'rate', 'conversion', 'filter', 'display_id', 'display_epoch'):
        assert frozen['memory']['view'][key] == first['memory']['view'][key], ('frozen timing changed raw', key)
    assert frozen['timing']['channels'] == first['timing']['channels'], 'frozen profile changed timing cache'
    result = dict(state='verified-independent-timing-evidence', records=reports,
                  physical_panel='awaiting-current-feedback', calibrated=False)
    return result


if __name__ == '__main__':
    result = audit(sys.argv[1])
    path = Path(sys.argv[1]) / 'timing-evidence-audit.json'
    path.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print('TIMING_EVIDENCE_AUDIT_PASS:', len(result['records']), 'records')
