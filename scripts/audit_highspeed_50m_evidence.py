"""Independent ABI7 record/metadata/known50kHz/cursor audit; no board access."""
from pathlib import Path
import argparse
import hashlib
import json
import math
import re
import struct

from audit_hardware_trigger_evidence import load, checked_bytes, check_designated, DIVISIONS
from analyze_adc_live_record import analyze

TIMES = [.0000001, .0000002, .0000005, .000001, .000002, .000005, .00001, .00002] + DIVISIONS
RECORDS = {
    'raw50m-5us': (5, True),
    'raw50m-100ns-cursor': (0, True),
    'cic6m25-10us': (6, True),
    'cic6m25-50us': (8, False),
    'cic625k-50mhz': (10, True),
    'raw50m-fall': (5, False),
    'raw50m-cursors': (5, True),
}


def profile(index):
    assert 0 <= index < 21
    if index < 6:
        return 50000000, 4, 0
    if index < 9:
        return 6250000, 5, 3
    if index < 12:
        return 625000, 0, 3
    if index < 15:
        return 62500, 1, 3
    if index < 18:
        return 6250, 2, 3
    return 625, 3, 3


def residual(codes, rate, hz):
    """Reject gross digital sample outliers; this is not voltage calibration."""
    n = len(codes)
    mean = sum(codes)/n
    sine = [math.sin(2*math.pi*hz*i/rate) for i in range(n)]
    cosine = [math.cos(2*math.pi*hz*i/rate) for i in range(n)]
    sm, cm = sum(sine)/n, sum(cosine)/n
    ss = sum(x*x for x in sine)-n*sm*sm
    cc = sum(x*x for x in cosine)-n*cm*cm
    sc = sum(a*b for a, b in zip(sine, cosine))-n*sm*cm
    ys = sum((y-mean)*v for y, v in zip(codes, sine))
    yc = sum((y-mean)*v for y, v in zip(codes, cosine))
    det = ss*cc-sc*sc
    a, b = (ys*cc-yc*sc)/det, (yc*ss-ys*sc)/det
    errors = [y-(mean+a*(s-sm)+b*(c-cm)) for y, s, c in zip(codes, sine, cosine)]
    peak = max(abs(e) for e in errors)
    assert math.isfinite(peak) and peak < 32, ('large sample outlier', peak)
    return dict(max_residual_code=peak, rms_residual_code=math.sqrt(sum(e*e for e in errors)/n),
                outlier_guard_code=32, purpose='gross bit/data corruption guard, not precision calibration')


def known_tone_fit(codes, rate):
    """DC+sin+cos least squares over40..60kHz, not ripple upcrossing counts."""
    count = len(codes)
    assert count == 4096 and rate > 120000
    mean = sum(codes)/count
    centered = [v-mean for v in codes]
    energy = sum(v*v for v in centered)
    assert math.isfinite(energy) and energy > 0

    def fit(hz):
        step = 2*math.pi*hz/rate
        sine = [math.sin(step*i) for i in range(count)]
        cosine = [math.cos(step*i) for i in range(count)]
        sm, cm = sum(sine)/count, sum(cosine)/count
        ss = sum(v*v for v in sine)-count*sm*sm
        cc = sum(v*v for v in cosine)-count*cm*cm
        sc = sum(a*b for a, b in zip(sine, cosine))-count*sm*cm
        ys = sum(y*v for y, v in zip(centered, sine))
        yc = sum(y*v for y, v in zip(centered, cosine))
        determinant = ss*cc-sc*sc
        assert determinant > 0 and math.isfinite(determinant)
        a, b = (ys*cc-yc*sc)/determinant, (yc*ss-ys*sc)/determinant
        return (a*ys+b*yc)/energy, hz, 2*math.hypot(a, b)*10/1023

    best = max(fit(hz) for hz in range(40000, 60001, 100))
    best = max(fit(best[1]+i) for i in range(-100, 101) if 40000 <= best[1]+i <= 60000)
    assert 49000 <= best[1] <= 51000 and .90 < best[0] <= 1.0000001, best
    return dict(frequency_hz=best[1], explained_variance=best[0],
                fundamental_vpp_nominal=best[2], search_hz=[40000, 60000],
                resolution_hz=1, calibrated=False, residual=residual(codes, rate, best[1]))


def check_frozen_snapshot(item, index, rising=True):
    """Validate a frozen UART snapshot even when no DDR export follows it."""
    ui, adc = item['ui'], item['adc']
    rate, code, kind = profile(index)
    assert int(ui['time']) == index
    assert ui['running'] == '0' and ui['single_complete'] == '1' and ui['single_armed'] == '0'
    assert ui['source'] == 'ADC2' and ui['trig_channel'] == '2' and ui['level_mv'] == '500'
    assert ui['trig_rising'] == ('1' if rising else '0') and ui['triggered'] == '1'
    assert adc['available'] == '1' and adc['pending'] == '0'
    assert adc['id'] == adc['display_id'] == ui['display_id'] == ui['seen_id']
    assert adc['capture_epoch'] == adc['display_epoch'] == ui['epoch']
    assert all(0 <= int(value) < 2**32 for value in [adc['id'], ui['epoch']])
    assert adc['address'] == adc['display_address'] and adc['display_address'] in ['13000000', '13004000']
    assert re.fullmatch(r'[0-9a-fA-F]{8}', adc['display_crc'])
    assert adc['display_crc'].upper() == adc['crc32'].upper()
    assert int(adc['bytes']) == 16384
    assert int(adc['applied_rate']) == int(adc['display_rate']) == int(adc['rate']) == int(adc['requested_rate']) == rate
    assert int(adc['rate_index']) == code and adc['configbusy'] == '0'
    assert int(adc['conversion_hz']) == int(adc['display_conversion_hz']) == 50000000
    assert int(adc['filter_kind']) == int(adc['record_filter']) == int(adc['display_filter']) == kind
    assert adc['hw_matched'] == '1' and adc['hw_auto'] == '0' and adc['hw_waiting'] == '0'
    assert int(adc['status'], 16) == 0x82
    seen, ring_start = int(adc['hw_seen']), int(adc['hw_start'])
    assert 4096 <= seen <= 0xffffffff and 0 <= ring_start < 4096
    if seen != 0xffffffff:
        assert ring_start == seen % 4096
    span = 10*TIMES[index]*rate
    start, cross = float(ui['view_start']), float(ui['crossing'])
    assert all(math.isfinite(value) for value in [span, start, cross])
    assert start >= 0 and start+span <= 4095.004
    after = math.ceil(.2*span)+1
    assert int(adc['hw_after']) == after and after-1 <= cross <= after
    assert abs(cross-start-.2*span) < .004


def check_record(words, item):
    ui, adc = item['ui'], item['adc']
    index, frame_expected = RECORDS[item['name']]
    rate, _, _ = profile(index)
    check_frozen_snapshot(item, index, item['name'] != 'raw50m-fall')
    assert item['rate'] == rate and adc['id'] == item['id']
    assert adc['display_crc'].upper() == item['crc32'].upper()
    assert ('frame_crc' in item) == frame_expected
    assert not any(w & 0xf800f800 for w in words)
    return check_designated(words, item, TIMES)


def check_cursor(words, item):
    ui, rate = item['ui'], item['rate']
    expected = {'raw50m-100ns-cursor': ([250, 270], .020), 'raw50m-cursors': ([250, 650], 20)}
    if item['name'] not in expected:
        assert ui['cursors'] == '0'
        return {}
    positions, expected_us = expected[item['name']]
    assert ui['cursors'] == ui['cursor_valid'] == '1'
    assert [int(ui['cursor_a']), int(ui['cursor_b'])] == positions
    dt, inverse, dv = (float(ui[key]) for key in ['cursor_dt_us', 'cursor_inv_hz', 'cursor_dv'])
    assert all(math.isfinite(v) for v in [dt, inverse, dv])
    span = 10*TIMES[int(ui['time'])]*rate
    voltages = []
    for pos in positions:
        at = float(ui['view_start'])+span*pos/1000
        assert math.isfinite(at) and 0 <= at <= 4095
        n, fraction = math.floor(at), at-math.floor(at)
        a = (words[n] >> 16) & 2047
        b = (words[n+1] >> 16) & 2047 if fraction else a
        assert not (a | b) & 1024
        voltages.append((a+fraction*(b-a))*10/1023-5)
    actual_us = span*(positions[1]-positions[0])/1000/rate*1e6
    assert abs(actual_us-expected_us) < 1e-9 and abs(dt-actual_us) < 1e-6
    assert abs(inverse-1e6/abs(actual_us)) < 1e-5
    assert abs(dv-(voltages[1]-voltages[0])) < .001
    return dict(cursor_dt_us=actual_us, cursor_inverse_hz=1e6/abs(actual_us),
                cursor_voltage_v=voltages, cursor_delta_v=voltages[1]-voltages[0],
                cursor_reported_delta_v=dv, cursor_voltage_tolerance_v=.001)


def control(session, name):
    matches = [item for item in session['controls'] if item['test'] == name]
    assert len(matches) == 1, (name, len(matches))
    return matches[0]


def check_held(before, after, target_index):
    assert after['ui']['time'] == str(target_index) and after['ui']['running'] == '0'
    assert before['ui']['display_id'] == after['ui']['display_id']
    for key in ['display_id', 'display_address', 'display_crc', 'display_rate',
                'display_epoch', 'display_filter', 'display_conversion_hz']:
        assert before['adc'][key] == after['adc'][key], key
    delta = (int(after['ui']['epoch'])-int(before['ui']['epoch'])) % 2**32
    assert 0 < delta < 2**31


def check_controls(session, records):
    required = [f'raw50M-fast-timebase-{i}-single-complete' for i in range(6)] + [
        'raw50M-cursor20ns-frozen', 'RAW50M-held-while-requesting-CIC6m25',
        'CIC6m25-timebase-6-single-complete', 'CIC6m25-timebase-8-single-complete',
        'CIC6m25-held-while-requesting-CIC625k', 'CIC625k-restored-on50MHz-converter',
        'CIC625k-held-while-requesting-RAW50M', 'RAW50M-falling-trigger-positive',
        'RAW50M-empty-CH1-waits-while-samples-progress',
        'RAW50M-measurement-switch-keeps-waiting-epoch',
        'RAW50M-trigger-source-new-epoch-completes-single',
        'RAW50M-5us-cursor20us-and50kHz', 'final-RAW50M-CH2-5us-div-auto-running',
    ] + [name+'-independent-DDR-CRC-and-timebase' for name in RECORDS]
    assert len(required) == 26
    for name in required:
        control(session, name)
    snapshots = [(f'raw50M-fast-timebase-{index}-single-complete', index, True)
                 for index in [5, 4, 3, 2, 1, 0]] + [
        ('raw50M-cursor20ns-frozen', 0, True),
        ('CIC6m25-timebase-6-single-complete', 6, True),
        ('CIC6m25-timebase-8-single-complete', 8, True),
        ('CIC625k-restored-on50MHz-converter', 10, True),
        ('RAW50M-falling-trigger-positive', 5, False),
        ('RAW50M-trigger-source-new-epoch-completes-single', 5, True),
        ('RAW50M-5us-cursor20us-and50kHz', 5, True),
    ]
    for name, index, rising in snapshots:
        check_frozen_snapshot(control(session, name), index, rising)
    fast = [control(session, f'raw50M-fast-timebase-{index}-single-complete')
            for index in [5, 4, 3, 2, 1, 0]]
    for before, after in zip(fast, fast[1:]):
        for key in ['display_id', 'epoch']:
            delta = (int(after['ui'][key])-int(before['ui'][key])) % 2**32
            assert 0 < delta < 2**31, ('Fast single did not advance', key)
    for name, item in records.items():
        exported = control(session, name+'-independent-DDR-CRC-and-timebase')
        assert exported['ui'] == item['ui'] and exported['adc'] == item['adc'], name
    for record, name, target in [
        ('raw50m-100ns-cursor', 'RAW50M-held-while-requesting-CIC6m25', 6),
        ('cic6m25-50us', 'CIC6m25-held-while-requesting-CIC625k', 10),
        ('cic625k-50mhz', 'CIC625k-held-while-requesting-RAW50M', 5),
    ]:
        check_held(records[record], control(session, name), target)
    waiting = control(session, 'RAW50M-empty-CH1-waits-while-samples-progress')
    selected = control(session, 'RAW50M-measurement-switch-keeps-waiting-epoch')
    for item in [waiting, selected]:
        assert item['ui']['single_armed'] == item['ui']['running'] == '1'
        assert item['ui']['single_complete'] == '0' and item['ui']['trig_channel'] == '1'
        assert item['adc']['hw_waiting'] == '1' and item['adc']['pending'] == '1'
        assert item['adc']['capture_epoch'] == item['ui']['epoch']
        for key in ['id', 'display_id', 'display_crc', 'display_rate', 'display_filter', 'display_conversion_hz']:
            assert item['adc'][key] == records['raw50m-fall']['adc'][key]
    progress = session['highspeed50m_wait_progress']
    anchor, observed = progress['anchor'], progress['after']
    assert observed['ui'] == waiting['ui'] and observed['adc'] == waiting['adc']
    assert progress['minimum_samples'] == 32768
    assert anchor['adc']['pending'] == anchor['adc']['hw_waiting'] == '1'
    assert anchor['ui']['epoch'] == anchor['adc']['capture_epoch'] == waiting['ui']['epoch']
    assert anchor['ui']['display_id'] == waiting['ui']['display_id']
    for key in ['id', 'display_id', 'display_crc', 'display_rate', 'display_filter',
                'display_conversion_hz', 'display_epoch', 'display_address']:
        assert anchor['adc'][key] == waiting['adc'][key], key
    first, last = int(anchor['adc']['hw_progress']), int(waiting['adc']['hw_progress'])
    assert 0 <= first < 2**32 and 0 <= last < 2**32
    assert 32768 <= (last-first) % 2**32 < 2**31
    assert selected['ui']['epoch'] == waiting['ui']['epoch']
    after = control(session, 'RAW50M-trigger-source-new-epoch-completes-single')
    assert int(after['ui']['epoch']) == (int(selected['ui']['epoch'])+1) % 2**32
    delta = (int(after['ui']['display_id'])-int(selected['ui']['seen_id'])) % 2**32
    assert 0 < delta < 2**31 and after['ui']['trig_channel'] == '2'
    assert after['ui']['single_complete'] == '1' and after['ui']['running'] == '0'
    assert after['adc']['display_epoch'] == after['ui']['epoch']
    final = control(session, 'final-RAW50M-CH2-5us-div-auto-running')
    assert final['ui']['time'] == '5' and final['ui']['source'] == 'ADC2' and final['ui']['trig_channel'] == '2'
    assert final['ui']['running'] == final['ui']['dual_visible'] == final['ui']['triggered'] == '1'
    assert final['ui']['trig_normal'] == final['ui']['cursors'] == '0'
    assert final['adc']['display_rate'] == '50000000' and final['adc']['display_filter'] == '0'
    assert final['adc']['display_epoch'] == final['ui']['epoch']


def audit(directory):
    session = load(directory/'session.json')
    assert session['state'].startswith('verified-board-') and session['flash_written'] is False
    log_bytes = (directory/'uart.log').read_bytes()
    assert hashlib.sha256(log_bytes).hexdigest().upper() == session['uart_sha256'].upper()
    log = log_bytes.decode('utf-8-sig')
    assert re.search(r'ADC_LIVE_READY:[^\r\n]*conversion_hz=50000000[^\r\n]*ABI=00070000', log)
    assert not re.search(r'APP_FATAL|APP_LOG_ERROR|ADC_LIVE_FAIL|DDR_FAIL|VIDEO_PRESENT_FAIL|VIDEO_STOP_TIMEOUT', log)
    assert not re.search(r'APP_DIAG[^\r\n]*dma_errors=(?!00000000)[0-9a-fA-F]{8}', log)
    raw_count = frame_count = 0
    baseline_quality = []

    def check_baseline(data, name):
        # The shared pre-highspeed suite runs at its fixed 200us/div profile:
        # index10 /625kSa/s on the ABI7 platform, before the RAW-specific suite.
        words = struct.unpack('<4096I', data)
        assert not any(w & (1 << 26) for w in words), ('CH2 overrange', name)
        tone = known_tone_fit([(w >> 16) & 1023 for w in words], 625000)
        baseline_quality.append(dict(name=name, rate=625000, known_tone_fit=tone))

    assert len(session['records']) == 2
    for item in session['records']:
        data = checked_bytes(directory/item['file'], item['crc32'], item['sha256'], 16384)
        check_baseline(data, item['file'])
        raw_count += 1
    for stem in ['dual', 'cursor', 'channel', 'trigsrc']:
        item = session[stem+'_frame']
        data = checked_bytes(directory/(stem+'-single-record.bin'), item['record_crc'], item['record_sha256'], 16384)
        check_baseline(data, stem+'-single-record.bin')
        checked_bytes(directory/item['file'], item['crc32'], item['sha256'], 1843200)
        raw_count += 1
        frame_count += 1
    assert len(session['long_records']) == len(RECORDS)
    records = {item['name']: item for item in session['long_records']}
    assert records.keys() == RECORDS.keys()
    check_controls(session, records)
    details = []
    for name, item in records.items():
        data = checked_bytes(directory/(name+'.bin'), item['crc32'], item['sha256'], 16384)
        words = struct.unpack('<4096I', data)
        detail = check_record(words, item)
        stats = analyze(data, item['rate'])
        assert stats['channels'][1]['otr_count'] == 0
        tone = known_tone_fit([(w >> 16) & 1023 for w in words], item['rate'])
        detail.update(check_cursor(words, item))
        detail.update(name=name, rate=item['rate'], filter_kind=int(item['adc']['display_filter']),
                      crc32=item['crc32'], sha256=item['sha256'], known_tone_fit=tone,
                      samples_per_cycle=item['rate']/tone['frequency_hz'], ui=item['ui'], adc=item['adc'])
        raw_count += 1
        if 'frame_crc' in item:
            checked_bytes(directory/(name+'-frame.bin'), item['frame_crc'], item['frame_sha256'], 1843200)
            assert re.search(r'addr=10[02]00000 bytes=1843200 format=BGR888 crc='+re.escape(item['frame_crc']), log, re.I)
            detail.update(frame_crc32=item['frame_crc'], frame_sha256=item['frame_sha256'])
            frame_count += 1
        details.append(detail)
    assert raw_count == 13 and frame_count == 9
    return dict(state='verified-offline-ABI7-evidence', raw_exports=raw_count, ddr_frames=frame_count,
                controls=len(session['controls']), new_controls=26, records=details, baseline_quality=baseline_quality,
                known_input_hz=50000, calibrated=False, analog_bandwidth='not-measured',
                physical_panel='separate-user-feedback')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('run', type=Path)
    directory = parser.parse_args().run
    result = audit(directory)
    (directory/'highspeed-50m-evidence-audit.json').write_text(json.dumps(result, indent=2)+'\n')
    print('HIGHSPEED_50M_EVIDENCE_PASS', json.dumps(result))


if __name__ == '__main__':
    main()
