"""Read-only FFT board audit: NumPy bins, actual lines, plot, and snapshot provenance."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import struct

import numpy as np

from audit_hardware_trigger_evidence import load, checked_bytes
from audit_memory_evidence import _bind, _log_index, equal_fields, check_epoch, _slot
from audit_settings_evidence import _snapshot, _held, _same_saved, _acq, _parameter, PREFS
from record_blob import parse_record
from spectrum_reference import compare_spectrum

EXPORTS = ('fft-hann-single', 'fft-hann-full', 'fft-rect-single',
           'fft-hann-mean-off', 'fft-empty-ch1', 'fft-live-raw50m', 'fft-replay')
CASE_CONFIG = ((1, 1, 1, 1, 6250000), (1, 1, 1, 0, 6250000),
               (1, 0, 1, 0, 6250000), (1, 1, 0, 0, 6250000),
               (0, 1, 1, 0, 6250000), (1, 1, 1, 0, 50000000),
               (1, 1, 1, 0, 50000000))
CHECKS = ('fft-pointer-open-preserves-single', 'fft-range-only-preserves-spectrum',
          'fft-rect-window-preserves-single', 'fft-keep-mean-actual-bins',
          'fft-independent-empty-CH1-no-peak', 'fft-replay-uses-stored-source-metadata')
STATUS = tuple('valid panel seq channel window remove_mean auto_range end_bin count rate id '
               'source_epoch conversion actual_conversion filter raw_address raw_bytes raw_crc '
               'bins_address bins_bytes bins_crc plot_address plot_bytes plot_crc elapsed_ms'.split())
VALUES = tuple('bin_hz mean_v gain peak_valid peak_bin peak_hz peak_vrms peak_dbv overrange '
               'ui_epoch running display_id live_rate replay wave_crc param_crc'.split())
CACHE_STATUS = tuple('valid seq channel window remove_mean count rate id source_epoch conversion '
                     'actual_conversion filter raw_address raw_bytes raw_crc bins_address bins_bytes '
                     'bins_crc elapsed_ms'.split())
CACHE_VALUES = tuple('bin_hz mean_v gain peak_valid peak_bin peak_hz peak_vrms peak_dbv '
                     'overrange wave_crc param_crc'.split())
PROJECTION = ('auto_range', 'end_bin', 'plot_address', 'plot_bytes', 'plot_crc')
COLORS = ((64, 224, 192), (255, 200, 87))


def metadata_from_fft(fft):
    h, v = fft['status'], fft['values']
    result = {key: int(h[field]) for key, field in (
        ('count', 'count'), ('rate_hz', 'rate'), ('channel', 'channel'), ('window', 'window'))}
    result.update(valid=h['valid'] == '1', remove_mean=h['remove_mean'] == '1',
                  overrange=v['overrange'] == '1', peak_valid=v['peak_valid'] == '1')
    for key, field in (('bin_hz', 'bin_hz'), ('mean_v', 'mean_v'), ('coherent_gain', 'gain'),
                       ('peak_bin', 'peak_bin'), ('peak_hz', 'peak_hz'),
                       ('peak_vrms', 'peak_vrms'), ('peak_dbv', 'peak_dbv')):
        result[key] = float(v[field])
    return result


def allowed_end_bins(auto_range, peak_valid, printed_peak_bin):
    if not auto_range or not peak_valid:
        return {2048}
    ends = set()
    # UART prints nine fractional digits. A true value just above an integer
    # may print as that integer; allow only its half-unit rounding interval.
    for value in (printed_peak_bin - 5.1e-10, printed_peak_bin + 5.1e-10):
        need = 4 * math.ceil(value)
        end = 16
        while end < need and end < 2048:
            end *= 2
        ends.add(end)
    return ends


def project_bins(bins, end_bin):
    values = np.asarray(bins, dtype=np.float64)
    assert values.shape == (2049,) and np.all(np.isfinite(values)) and np.all(values >= 0)
    assert end_bin in (16, 32, 64, 128, 256, 512, 1024, 2048)
    out = []
    for column in range(720):
        first = column * end_bin // 719
        # Each column covers a closed bin interval, including both endpoints.
        last = min(end_bin, math.ceil((column + 1) * end_bin / 719))
        maximum = float(np.max(values[first:last + 1]))
        db = max(-100.0, min(20.0, 20 * math.log10(maximum) if maximum > 0 else -100.0))
        scaled = db * 1000
        out.append(math.floor(scaled + .5) if scaled >= 0 else math.ceil(scaled - .5))
    return np.asarray(out, dtype=np.int32)


def check_plot(bins, plot, status, values):
    end = int(status['end_bin'])
    assert end in allowed_end_bins(status['auto_range'] == '1', values['peak_valid'] == '1',
                                  float(values['peak_bin'])), 'wrong auto/full frequency extent'
    actual = np.asarray(plot)
    assert actual.shape == (720,) and np.all(actual >= -100000) and np.all(actual <= 20000)
    expected = project_bins(bins, end)
    assert np.all(np.abs(actual.astype(np.int64) - expected) <= 1), 'FFT plot does not preserve bin maxima / dBV'
    return dict(end_bin=end, maximum_hz=end * float(values.get('bin_hz', 0)),
                max_plot_error_millidb=int(np.max(np.abs(actual.astype(np.int64) - expected))))


def check_ranges(status, segments):
    ranges = []
    for field, size in (('raw', 16384), ('bins', 8196), ('plot', 2880)):
        address = status[field + '_address']
        assert re.fullmatch('[0-9A-Fa-f]{8}', address) and int(status[field + '_bytes']) == size
        start = int(address, 16)
        assert start % 4 == 0 and start + size <= 2 ** 32
        assert any(low <= start < start + size <= high for low, high in segments), (field, 'outside ELF')
        ranges.append((start, start + size))
    for a, b in ((0, 1), (0, 2), (1, 2)):
        assert ranges[a][1] <= ranges[b][0] or ranges[b][1] <= ranges[a][0], 'FFT buffers overlap'


def _segments(directory, session):
    pins = [entry['Hash'] for entry in session['files'] if Path(entry['Path']).name == 'application.elf']
    assert len(pins) == 1, 'no immutable ELF identity in session'
    path = directory / 'application.elf'
    if not path.exists():
        proof = load(directory / 'validation.json')
        assert proof['elf_sha256'].upper() == pins[0].upper()
        return [(int(low, 16), int(high, 16)) for low, high in proof['load_segments']]
    data = path.read_bytes()
    assert hashlib.sha256(data).hexdigest().upper() == pins[0].upper()
    assert data[:6] == b'\x7fELF\x01\x01' and struct.unpack_from('<H', data, 18)[0] == 40
    offset = struct.unpack_from('<I', data, 28)[0]
    size, count = struct.unpack_from('<HH', data, 42)
    segments = []
    for at in range(count):
        kind, _, virt, phys, _, memory_size, _, _ = struct.unpack_from('<8I', data, offset + at * size)
        if kind == 1:
            assert virt == phys and 0x100000 <= virt < virt + memory_size <= 0x1000000
            segments.append((virt, virt + memory_size))
    assert segments
    return segments


def _atomic_frames(log):
    pattern = r'(?m)^FFT_STATUS ([^\r\n]+)\r?\nFFT_VALUES ([^\r\n]+)\r?\nFFT_END seq=(\d+)\r?$'
    out = set()
    for head, values, sequence in re.findall(pattern, log):
        h = dict(re.findall(r'(\w+)=([^\s]+)', head))
        v = dict(re.findall(r'(\w+)=([^\s]+)', values))
        assert h['seq'] == sequence, 'UART FFT frame end has another sequence'
        out.add((tuple(sorted(h.items())), tuple(sorted(v.items())), sequence))
    assert out, 'no complete atomic FFT diagnostic frames'
    return out


def _fft(fft, index, atomic, segments):
    h, v = fft['status'], fft['values']
    assert set(h) == set(STATUS) and set(v) == set(VALUES), 'FFT diagnostic schema mismatch'
    _bind(index, 'FFT_STATUS', h); _bind(index, 'FFT_VALUES', v)
    _bind(index, 'FFT_END', {'seq': h['seq']})
    assert (tuple(sorted(h.items())), tuple(sorted(v.items())), h['seq']) in atomic, 'mixed atomic FFT frame'
    for key in ('valid', 'panel', 'channel', 'window', 'remove_mean', 'auto_range'):
        assert h[key] in ('0', '1')
    for key in ('peak_valid', 'overrange', 'running', 'replay'):
        assert v[key] in ('0', '1')
    for key in ('raw_crc', 'bins_crc', 'plot_crc'):
        assert re.fullmatch('[0-9a-fA-F]{8}', h[key])
    for key in ('wave_crc', 'param_crc'):
        assert re.fullmatch('[0-9a-fA-F]{8}', v[key])
    for key in ('bin_hz', 'mean_v', 'gain', 'peak_bin', 'peak_hz', 'peak_vrms', 'peak_dbv'):
        assert math.isfinite(float(v[key]))
    assert h['actual_conversion'] == '50000000'
    check_ranges(h, segments)
    if h['valid'] == '1':
        assert h['count'] == '4096' and 0 < int(h['rate']) <= 50000000
        assert int(h['end_bin']) in allowed_end_bins(h['auto_range'] == '1', v['peak_valid'] == '1', float(v['peak_bin']))
        assert abs(float(v['bin_hz']) - int(h['rate']) / 4096) <= 1e-6
        assert int(h['conversion']) in (5000000, 10000000, 25000000, 50000000)
        assert int(h['filter']) in (0, 3) and int(h['elapsed_ms']) >= 0
    return fft


def _cache_held(before, after):
    equal_fields(before['status'], after['status'], CACHE_STATUS)
    equal_fields(before['values'], after['values'], CACHE_VALUES)


def _snapshot_fft(snapshot, index, atomic, segments):
    _snapshot(snapshot, index)
    fft = _fft(snapshot['fft'], index, atomic, segments)
    ui, v, parameter = snapshot['ui'], fft['values'], snapshot['parameter']['status']
    equal_fields(ui, v, ('running', 'display_id'))
    assert ui['epoch'] == v['ui_epoch'] and v['live_rate'] == parameter['rate']
    assert v['replay'] == snapshot['memory']['status']['replay']
    assert v['wave_crc'].upper() == parameter['wave_crc'].upper()
    assert v['param_crc'].upper() == parameter['blob_crc'].upper()
    return snapshot


def _source_matches(snapshot):
    h, view = snapshot['fft']['status'], snapshot['memory']['view']
    assert h['valid'] == '1' and h['id'] == view['display_id']
    assert h['rate'] == view['rate'] and h['source_epoch'] == view['display_epoch']
    assert h['conversion'] == view['conversion'] and h['filter'] == view['filter']
    assert h['raw_crc'].upper() == view['record_crc'].upper()


def _frozen_held(before, after):
    _held(before, after, 0); _same_saved(before, after)
    equal_fields(before['ui'], after['ui'], PREFS + ('single_armed', 'single_complete'))
    equal_fields(before['parameter']['status'], after['parameter']['status'], ('wave_valid', 'wave_crc'))


def _export_addresses(directory, entry, index):
    name = entry['name']; h = entry['fft']['status']
    text = (directory / ('export-' + name + '.tcl')).read_text(encoding='utf-8-sig')
    assert len(re.findall(r'(?m)^mrd -bin -file ', text)) == 4, 'unexpected spectrum export operations'
    for suffix, field, count in (('-raw.bin', 'raw_address', 4096), ('-bins.bin', 'bins_address', 2049),
                                  ('-plot.bin', 'plot_address', 720), ('-frame.bin', None, 460800)):
        matches = re.findall(r'mrd -bin -file \{[^{}\r\n]*[/\\]' + re.escape(name + suffix)
                             + r'\} 0x([0-9a-fA-F]{8}) (\d+)', text)
        assert len(matches) == 1 and int(matches[0][1]) == count, ('wrong FFT exported range', suffix)
        address = matches[0][0].lower()
        if field:
            assert address == h[field].lower(), ('FFT address not bound to source', suffix)
        else:
            assert address in ('10000000', '10200000')
            assert any(dict(fields).get('addr', '').lower() == address
                       and dict(fields).get('bytes') == '1843200'
                       and dict(fields).get('format') == 'BGR888'
                       and dict(fields).get('crc', '').upper() == entry['frame_crc'].upper()
                       for fields in index.get('SNAPSHOT_READY', ())), 'FFT frame not in UART'


def _raster(frame, plot, channel):
    image = np.frombuffer(frame, dtype=np.uint8).reshape(600, 1024, 3)[:, :, ::-1]
    rgb = np.asarray(COLORS[channel])
    # LVGL spectrum plot: global (136,220), 792x196, one-pixel border.
    chosen = [20, 80, 160, 240, 320, 400, 520, 640, 700, int(np.argmax(plot))]
    matches = []
    for point in chosen:
        x = 137 + 790 * point // 719
        y = 415 - (int(plot[point]) + 100000) * 194 // 120000
        region = image[max(220, y - 4):min(416, y + 5), max(136, x - 3):min(928, x + 4)]
        matches.append(bool(np.any(np.all(region == rgb, axis=2))))
    assert sum(matches) >= 7 and matches[-1], ('actual FFT trace disagrees with projected samples', matches)
    return dict(points=len(matches), matched=sum(matches), maximum_point=chosen[-1])


def audit(directory):
    directory = Path(directory)
    session = load(directory / 'session.json')
    assert session['state'].startswith('verified-board-') and session['flash_written'] is False
    data = (directory / 'uart.log').read_bytes()
    assert hashlib.sha256(data).hexdigest().upper() == session['uart_sha256'].upper()
    log = data.decode('utf-8-sig')
    assert not re.search(r'APP_FATAL|APP_LOG_ERROR|ADC_LIVE_FAIL|DDR_FAIL|VIDEO_PRESENT_FAIL|VIDEO_STOP_TIMEOUT', log)
    index, atomic, segments = _log_index(log), _atomic_frames(log), _segments(directory, session)
    for control in session['controls']:
        _acq(control, index)
    assert [entry['name'] for entry in session['spectrum_exports']] == list(EXPORTS)
    assert [entry['test'] for entry in session['spectrum_checks']] == list(CHECKS)
    before = _snapshot_fft(session['spectrum_opened']['before'], index, atomic, segments)
    opened = _snapshot_fft(session['spectrum_opened']['after'], index, atomic, segments)
    checks = [_snapshot_fft(entry['snapshot'], index, atomic, segments) for entry in session['spectrum_checks']]
    assert opened == checks[0] and before['ui']['single_complete'] == '1'
    assert before['fft']['status']['valid'] == '0'
    for snapshot in checks[:5]:
        _frozen_held(before, snapshot); _source_matches(snapshot)
        assert snapshot['ui']['source'] == 'ADC2' and snapshot['ui']['trig_channel'] == '2'
        assert snapshot['ui']['single_complete'] == '1' and snapshot['fft']['status']['panel'] == '1'
    first, full, rect, mean_off, empty, replay = checks
    assert first['fft']['status']['channel'] == first['fft']['status']['window'] == first['fft']['status']['remove_mean'] == '1'
    assert first['fft']['status']['auto_range'] == '1' and first['fft']['values']['peak_valid'] == '1'
    assert abs(float(first['fft']['values']['peak_hz']) - 50000) < 1500
    _cache_held(first['fft'], full['fft'])
    assert full['fft']['status']['auto_range'] == '0' and full['fft']['status']['end_bin'] == '2048'
    assert rect['fft']['status']['window'] == '0' and int(rect['fft']['status']['seq']) == int(full['fft']['status']['seq']) + 1
    assert mean_off['fft']['status']['window'] == '1' and mean_off['fft']['status']['remove_mean'] == '0'
    assert int(mean_off['fft']['status']['seq']) == int(rect['fft']['status']['seq']) + 2
    assert empty['fft']['status']['channel'] == '0' and empty['fft']['values']['peak_valid'] == '0'
    assert int(empty['fft']['status']['seq']) == int(mean_off['fft']['status']['seq']) + 2

    hold = session['spectrum_live_hold']
    for at in ('before', 'after'):
        _acq(hold[at], index); _fft(hold['fft_' + at], index, atomic, segments)
        assert hold[at]['ui']['running'] == '1' and hold[at]['adc']['display_rate'] == '50000000'
    _cache_held(hold['fft_before'], hold['fft_after'])
    equal_fields(hold['fft_before']['status'], hold['fft_after']['status'], PROJECTION)
    assert hold['fft_after']['status']['rate'] == '6250000'
    assert int(hold['after']['ui']['display_id']) > int(hold['before']['ui']['display_id'])
    assert hold['fft_after']['status']['id'] != hold['after']['ui']['display_id']
    update = session['spectrum_live_update']
    _acq(update['before'], index); _acq(update['after'], index); _fft(update['fft'], index, atomic, segments)
    assert update['before'] == hold['after']
    check_epoch(update['before']['ui'], update['after']['ui'], 0)
    equal_fields(update['before']['ui'], update['after']['ui'], PREFS + ('running',))
    assert update['after']['ui']['running'] == update['fft']['values']['running'] == '1'
    assert update['fft']['status']['rate'] == update['fft']['status']['conversion'] == '50000000'
    assert int(update['fft']['status']['seq']) > int(hold['fft_after']['status']['seq'])

    pre_replay = _snapshot_fft(session['spectrum_before_replay'], index, atomic, segments)
    assert pre_replay['memory']['status']['replay'] == '0' and pre_replay['memory']['view']['rate'] == '6250000'
    _source_matches(replay)
    assert replay['memory']['status']['replay'] == replay['fft']['values']['replay'] == '1'
    assert replay['fft']['status']['rate'] == '50000000' and replay['fft']['status']['rate'] != pre_replay['memory']['view']['rate']
    assert replay['fft']['status']['id'] != pre_replay['ui']['display_id']
    assert int(replay['fft']['status']['source_epoch']) < int(replay['ui']['epoch'])
    check_epoch(pre_replay['ui'], replay['ui'], 1)
    _same_saved(before, replay)
    equal_fields(before['parameter']['status'], replay['parameter']['status'], ('valid', 'blob_crc', 'wave_valid', 'wave_crc'))
    demo = session['spectrum_demo']
    _acq(demo['before'], index); _acq(demo['after'], index)
    _fft(demo['fft_before'], index, atomic, segments); _fft(demo['fft_after'], index, atomic, segments)
    _cache_held(demo['fft_before'], demo['fft_after'])
    equal_fields(demo['fft_before']['status'], demo['fft_after']['status'], PROJECTION)
    equal_fields(demo['before']['ui'], demo['after']['ui'], PREFS + ('epoch', 'running', 'display_id'))
    # The instrument calls this DEMO on screen; its UART source enum is SIM.
    assert demo['after']['ui']['source'] == 'SIM' and demo['fft_after']['status']['panel'] == '0'
    for command in ('fft', 'fftupdate', 'fftchannel', 'fftwindow', 'fftmean'):
        assert re.search(r'(?m)^FFT_COMMAND ' + command + r' REJECTED\r?$', log)

    reports, raws, bins_all, elapsed = [], [], [], []
    for entry, configuration in zip(session['spectrum_exports'], CASE_CONFIG):
        name, fft = entry['name'], _fft(entry['fft'], index, atomic, segments)
        h, v = fft['status'], fft['values']
        assert h['valid'] == h['panel'] == '1'
        assert tuple(int(h[key]) for key in ('channel', 'window', 'remove_mean', 'auto_range', 'rate')) == configuration, ('FFT case changed other analysis options', name)
        assert v['wave_crc'].upper() == before['parameter']['status']['wave_crc'].upper()
        assert v['param_crc'].upper() == before['parameter']['status']['blob_crc'].upper()
        for field in ('raw', 'bins', 'plot'):
            assert entry[field + '_crc'].upper() == h[field + '_crc'].upper()
        expected_meta = metadata_from_fft(fft)
        assert entry['metadata'] == expected_meta
        assert load(directory / (name + '-metadata.json')) == expected_meta
        raw = checked_bytes(directory / (name + '-raw.bin'), entry['raw_crc'], entry['raw_sha256'], 16384)
        bins = checked_bytes(directory / (name + '-bins.bin'), entry['bins_crc'], entry['bins_sha256'], 8196)
        plot_bytes = checked_bytes(directory / (name + '-plot.bin'), entry['plot_crc'], entry['plot_sha256'], 2880)
        frame = checked_bytes(directory / (name + '-frame.bin'), entry['frame_crc'], entry['frame_sha256'], 1843200)
        values = np.frombuffer(bins, dtype='<f4').astype(np.float64)
        plot = np.frombuffer(plot_bytes, dtype='<i4')
        numerical = compare_spectrum(raw, bins, expected_meta)
        projected = check_plot(values, plot, h, v)
        _export_addresses(directory, entry, index)
        raster = _raster(frame, plot, int(h['channel']))
        raws.append(raw); bins_all.append(bins); elapsed.append(int(h['elapsed_ms']))
        reports.append(dict(name=name, numerical=numerical, projection=projected, raster=raster,
                            seq=int(h['seq']), source_id=int(h['id']), source_epoch=int(h['source_epoch']),
                            source_rate=int(h['rate']), source_conversion=int(h['conversion']),
                            analysis_ms=int(h['elapsed_ms']), raw_sha256=entry['raw_sha256']))
    for entry, snapshot in zip(session['spectrum_exports'][:5], checks[:5]):
        _cache_held(snapshot['fft'], entry['fft'])
        equal_fields(snapshot['fft']['status'], entry['fft']['status'], ('auto_range', 'end_bin', 'plot_crc'))
    assert all(raw == raws[0] for raw in raws[:5]), 'FFT options altered the frozen source snapshot'
    assert bins_all[0] == bins_all[1], 'frequency-range-only selection changed FFT bins'
    assert session['spectrum_exports'][0]['raw_crc'].upper() == before['parameter']['status']['record_crc'].upper()
    equal_fields(session['spectrum_exports'][5]['fft']['status'], update['fft']['status'], CACHE_STATUS)
    _cache_held(session['spectrum_exports'][6]['fft'], replay['fft'])
    slot_entry = session['memory_exports'][-1]
    blob = checked_bytes(directory / (slot_entry['name'] + '.scw'), slot_entry['blob_crc'], slot_entry['blob_sha256'], 16512)
    stored = parse_record(blob); _slot(stored, replay['memory']['status'])
    assert raws[6] == stored['raw_data'], 'Replay FFT did not use the saved waveform payload'

    final = session['spectrum_final']; _acq(final, index); _parameter(final['parameter'], index)
    _fft(final['fft'], index, atomic, segments)
    assert final['ui']['running'] == '1' and final['ui']['source'] == 'ADC2' and final['ui']['time'] == '6'
    assert final['ui']['dual'] == '1' and final['ui']['cursors'] == final['ui']['trig_normal'] == '0'
    assert final['adc']['display_rate'] == '6250000' and final['adc']['display_epoch'] == final['ui']['epoch']
    assert final['fft']['status']['panel'] == '0' and final['fft']['values']['replay'] == '0'
    equal_fields(before['parameter']['status'], final['parameter']['status'], ('valid', 'blob_crc', 'wave_valid', 'wave_crc'))
    assert before['parameter']['config'] == final['parameter']['config'], 'FFT changed saved parameter metadata'
    equal_fields(replay['fft']['status'], final['fft']['status'], CACHE_STATUS)
    equal_fields(replay['fft']['status'], final['fft']['status'], PROJECTION)
    return dict(state='verified-independent-numpy-spectrum-evidence', spectra=7, bins_verified=7 * 2049,
                plot_values_verified=7 * 720, ddr_frames=7, analysis_ms_max=max(elapsed),
                scope_storage='RAM-only-no-SD-or-Flash', physical_panel='separate-user-feedback', records=reports)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run', type=Path)
    args = parser.parse_args()
    result = audit(args.run)
    (args.run / 'spectrum-evidence-audit.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print('SPECTRUM_EVIDENCE_PASS', json.dumps(result))


if __name__ == '__main__':
    main()
