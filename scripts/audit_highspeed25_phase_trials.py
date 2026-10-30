"""Independent three-phase lab comparison, no hardware access or PVT claim."""
from pathlib import Path
import argparse
import hashlib
import json
import math
import re
import struct
from datetime import datetime
from audit_hardware_trigger_evidence import checked_bytes, check_designated, load
from audit_highspeed_25m_evidence import TIMES, known_tone_fit


def residual(codes, rate, hz):
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


def audit_run(directory):
    s = load(directory/'session.json')
    phase = s['phase_deg']
    assert phase in [300, 315, 330] and s['state'] == 'verified-board-phase-records-awaiting-independent-audit'
    assert not s['flash_written'] and s['known_input_hz'] == 50000
    assert datetime.fromisoformat(s['started_utc']) < datetime.fromisoformat(s['completed_utc'])
    assert hashlib.sha256((directory/'platform.bit').read_bytes()).hexdigest().upper() == s['bit_sha256']
    assert hashlib.sha256((directory/'application.elf').read_bytes()).hexdigest().upper() == s['elf_sha256']
    manifest = s['phase_manifest']
    if phase == 315:
        assert s['bit_sha256'] == manifest['nominal_bit_sha256']
    else:
        entries = [e for e in manifest['entries'] if e['phase_deg'] == phase]
        assert len(entries) == 1 and entries[0]['bit_sha256'] == s['bit_sha256']
    log_bytes = (directory/'uart.log').read_bytes()
    assert hashlib.sha256(log_bytes).hexdigest().upper() == s['uart_sha256']
    log = log_bytes.decode('utf-8-sig')
    assert re.search(r'ADC_LIVE_READY:[^\r\n]*conversion_hz=25000000[^\r\n]*ABI=00060000', log)
    assert not re.search(r'APP_FATAL|APP_LOG_ERROR|ADC_LIVE_FAIL|DDR_FAIL|VIDEO_PRESENT_FAIL|VIDEO_STOP_TIMEOUT', log)
    assert not re.search(r'APP_DIAG[^\r\n]*dma_errors=(?!00000000)[0-9a-fA-F]{8}', log)
    assert 'BOOT_MODE=0' in (directory/'boot.log').read_text(encoding='utf-8-sig')
    assert len(s['long_records']) == 6
    names = {f'phase{phase}-{kind}-{rep}' for kind in ['raw', 'cic6m25'] for rep in [1, 2, 3]}
    assert {e['name'] for e in s['long_records']} == names
    details, frames = [], 0
    controls = {c['test']: c for c in s['controls']}
    assert len(controls) == len(s['controls']) == 13
    previous = None
    for e in s['long_records']:
        u, a = e['ui'], e['adc']
        raw = '-raw-' in e['name']
        index, rate, code, kind = (3, 25000000, 4, 0) if raw else (4, 6250000, 5, 3)
        assert u['time'] == str(index) and e['rate'] == rate and u['source'] == 'ADC2' and u['trig_channel'] == '2'
        assert u['trig_rising'] == '1' and u['level_mv'] == '500' and u['running'] == '0' and u['single_complete'] == '1'
        assert a['pending'] == a['configbusy'] == '0' and int(a['status'], 16) == 0x82
        assert a['id'] == a['display_id'] == u['display_id'] == e['id']
        assert a['capture_epoch'] == a['display_epoch'] == u['epoch']
        assert int(a['rate_index']) == code and int(a['rate']) == int(a['applied_rate']) == int(a['display_rate']) == rate
        assert int(a['conversion_hz']) == int(a['display_conversion_hz']) == 25000000
        assert int(a['record_filter']) == int(a['display_filter']) == int(a['filter_kind']) == kind
        assert e['crc32'].upper() == a['display_crc'].upper() == a['crc32'].upper()
        exported = controls[e['name']+'-independent-DDR-CRC-and-timebase']
        assert exported['ui'] == u and exported['adc'] == a
        if previous is not None:
            for key in ['display_id', 'epoch']:
                delta = (int(u[key])-int(previous[key])) % 2**32
                assert 0 < delta < 2**31, ('phase single reused a record', key)
        previous = u
        data = checked_bytes(directory/(e['name']+'.bin'), e['crc32'], e['sha256'], 16384)
        words = struct.unpack('<4096I', data)
        assert not any(w & 0xf800f800 or w & (1 << 26) for w in words)
        detail = check_designated(words, e, TIMES)
        codes = [(w >> 16) & 1023 for w in words]
        tone = known_tone_fit(codes, rate)
        detail.update(name=e['name'], rate=rate, crc32=e['crc32'], tone=tone,
                      residual=residual(codes, rate, tone['frequency_hz']))
        has_frame = e['name'] == f'phase{phase}-raw-1'
        assert ('frame_crc' in e) == has_frame
        if has_frame:
            checked_bytes(directory/(e['name']+'-frame.bin'), e['frame_crc'], e['frame_sha256'], 1843200)
            frames += 1
        details.append(detail)
    final = controls[f'phase{phase}-final-RAW25M-auto-running']
    u, a = final['ui'], final['adc']
    assert u['time'] == '3' and u['source'] == 'ADC2' and u['trig_channel'] == '2'
    assert u['running'] == u['dual_visible'] == u['triggered'] == '1'
    assert u['trig_normal'] == u['cursors'] == '0'
    assert a['display_rate'] == a['applied_rate'] == '25000000' and a['display_filter'] == '0'
    assert a['display_conversion_hz'] == '25000000' and a['display_epoch'] == u['epoch']
    result = dict(state='verified-offline-phase-lab-evidence', phase_deg=phase,
                  raw_exports=6, ddr_frames=frames, records=details,
                  started_utc=s['started_utc'], completed_utc=s['completed_utc'],
                  final_control=final,
                  scope='current wiring and50kHz source; not PVT or analog bandwidth proof')
    (directory/'phase-evidence-audit.json').write_text(json.dumps(result, indent=2)+'\n')
    return result


def main():
    p = argparse.ArgumentParser()
    p.add_argument('runs', nargs='+', type=Path)
    a = p.parse_args()
    results = [audit_run(r) for r in a.runs]
    assert [r['phase_deg'] for r in results] == [300, 330, 315], 'One run per phase, nominal final restore required'
    for before, after in zip(results, results[1:]):
        assert datetime.fromisoformat(before['completed_utc']) <= datetime.fromisoformat(after['started_utc']), 'Phase run time order mismatch'
    report = dict(state='verified-three-phase-lab-comparison', phases=results,
                  raw_exports=sum(r['raw_exports'] for r in results),
                  ddr_frames=sum(r['ddr_frames'] for r in results), final_phase_deg=315,
                  guaranteed_ADC_minmax='unavailable', analog_bandwidth='not-measured')
    (a.runs[-1]/'three-phase-comparison.json').write_text(json.dumps(report, indent=2)+'\n')
    print('THREE_PHASE_EVIDENCE_PASS', json.dumps(report))


if __name__ == '__main__':
    main()
