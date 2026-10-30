"""Independent ABI5 DDR/metadata/trigger/time-axis audit, no board access."""
from pathlib import Path
import argparse
import json
import math
import re
import struct
from audit_hardware_trigger_evidence import load, checked_bytes, check_designated, DIVISIONS
from analyze_adc_live_record import analyze

TIMES = [.000001, .000002, .000005, .00001, .00002] + DIVISIONS


def known_tone_fit(codes, rate):
    # This validation uses the user-set 10kHz sine. Native RAW retains ripple;
    # counting every mean upcrossing incorrectly counts that ripple as periods.
    # Solve DC+sin+cos least squares, scanning a stated 8..12kHz input interval.
    count = len(codes)
    mean = sum(codes)/count
    centered = [v-mean for v in codes]
    energy = sum(v*v for v in centered)
    assert energy > 0

    def fit(hz):
        step = 2*math.pi*hz/rate
        sin = [math.sin(step*i) for i in range(count)]
        cos = [math.cos(step*i) for i in range(count)]
        sm, cm = sum(sin)/count, sum(cos)/count
        ss = sum(v*v for v in sin)-count*sm*sm
        cc = sum(v*v for v in cos)-count*cm*cm
        sc = sum(a*b for a, b in zip(sin, cos))-count*sm*cm
        ys = sum(y*v for y, v in zip(centered, sin))
        yc = sum(y*v for y, v in zip(centered, cos))
        determinant = ss*cc-sc*sc
        a, b = (ys*cc-yc*sc)/determinant, (yc*ss-ys*sc)/determinant
        return (a*ys+b*yc)/energy, hz, 2*math.hypot(a, b)*10/1023

    best = max(fit(hz) for hz in range(8000, 12001, 10))
    best = max(fit(best[1]+i/10) for i in range(-100, 101))
    assert 9800 < best[1] < 10200 and best[0] > .90, best
    return dict(frequency_hz=best[1], explained_variance=best[0],
                fundamental_vpp_nominal=best[2], search_hz=[8000, 12000],
                resolution_hz=.1, calibrated=False)


def check_source_transition(before, after):
    assert int(after['ui']['epoch']) == (int(before['ui']['epoch'])+1) % 2**32
    delta = (int(after['ui']['display_id'])-int(before['ui']['seen_id'])) % 2**32
    assert 0 < delta < 2**31 and after['ui']['trig_channel'] == '2'
    assert after['adc']['display_epoch'] == after['ui']['epoch']


def main():
    p = argparse.ArgumentParser()
    p.add_argument('run', type=Path)
    r = p.parse_args().run
    s = load(r/'session.json')
    assert s['state'].startswith('verified-board-') and not s['flash_written']
    log = (r/'uart.log').read_text(encoding='utf-8-sig')
    assert not re.search(r'APP_FATAL|APP_LOG_ERROR|ADC_LIVE_FAIL|DDR_FAIL', log)
    assert not re.search(r'APP_DIAG[^\r\n]*dma_errors=(?!00000000)[0-9a-fA-F]{8}', log)
    controls = {x['test']: x for x in s['controls']}
    check_source_transition(controls['RAW-measurement-switch-keeps-waiting-epoch'],
                            controls['RAW-trigger-source-new-epoch-completes-single'])
    raw_count = frame_count = 0
    for x in s['records']:
        checked_bytes(r/x['file'], x['crc32'], x['sha256'], 16384)
        raw_count += 1
    for stem in ['dual', 'cursor', 'channel', 'trigsrc']:
        x = s.get(stem+'_frame')
        if x:
            checked_bytes(r/(stem+'-single-record.bin'), x['record_crc'], x['record_sha256'], 16384)
            checked_bytes(r/x['file'], x['crc32'], x['sha256'], 1843200)
            raw_count += 1
            frame_count += 1
    details = []
    for x in s['long_records']:
        ui, adc = x['ui'], x['adc']
        data = checked_bytes(r/(x['name']+'.bin'), x['crc32'], x['sha256'], 16384)
        words = struct.unpack('<4096I', data)
        assert not any(w & 0xf800f800 for w in words)
        raw_mode = int(ui['time']) < 5
        rate, kind = (10000000, 0) if raw_mode else (625000, 3)
        assert x['rate'] == rate
        assert int(adc['conversion_hz']) == int(adc['display_conversion_hz']) == 10000000
        assert int(adc['record_filter']) == int(adc['display_filter']) == int(adc['filter_kind']) == kind
        assert int(adc['rate_index']) == (4 if raw_mode else 0)
        d = check_designated(words, x, TIMES)
        stats = analyze(data, rate)
        assert stats['channels'][1]['otr_count'] == 0
        tone = known_tone_fit([(w >> 16) & 1023 for w in words], rate)
        frequency = tone['frequency_hz']
        d.update(name=x['name'], rate=rate, filter_kind=kind, ch2_frequency_hz=frequency,
                 samples_per_cycle=rate/frequency, crc32=x['crc32'], known_tone_fit=tone,
                 simple_upcrossing_frequency_hz=stats['channels'][1]['frequency_hz'])
        if ui['cursors'] == '1':
            span = 10*TIMES[int(ui['time'])]*rate
            positions = [int(ui['cursor_a']), int(ui['cursor_b'])]
            volts = []
            ch = int(ui['source'][-1])-1
            for pos in positions:
                at = float(ui['view_start'])+span*pos/1000
                n = math.floor(at)
                f = at-n
                assert 0 <= at <= 4095
                a = (words[n] >> (16*ch)) & 2047
                b = (words[n+1] >> (16*ch)) & 2047 if f else a
                assert not (a | b) & 1024
                volts.append((a+f*(b-a))*10/1023-5)
            dt = 10*TIMES[int(ui['time'])]*(positions[1]-positions[0])/1000*1e6
            assert ui['cursor_valid'] == '1' and abs(dt-float(ui['cursor_dt_us'])) < .000001
            assert abs(1e6/abs(dt)-float(ui['cursor_inv_hz'])) < .00001
            assert abs(volts[1]-volts[0]-float(ui['cursor_dv'])) < .001
            d.update(cursor_dt_us=dt, cursor_delta_v=volts[1]-volts[0])
        raw_count += 1
        if 'frame_crc' in x:
            checked_bytes(r/(x['name']+'-frame.bin'), x['frame_crc'], x['frame_sha256'], 1843200)
            frame_count += 1
        details.append(d)
    assert {x['name'] for x in details} == {'raw10m-20us', 'raw10m-1us-cursor', 'cic625k-10mhz', 'raw10m-fall', 'raw10m-cursors'}
    out = dict(state='verified-offline-ABI5-evidence', raw_exports=raw_count, ddr_frames=frame_count,
               controls=len(s['controls']), records=details, calibrated=False,
               analog_bandwidth='not-measured', physical_panel='separate-user-feedback')
    (r/'highspeed-10m-evidence-audit.json').write_text(json.dumps(out, indent=2)+'\n')
    print('HIGHSPEED_10M_EVIDENCE_PASS', json.dumps(out))


if __name__ == '__main__':
    main()
