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
from audit_highspeed_50m_evidence import TIMES, known_tone_fit, residual


def audit_run(directory):
    s = load(directory/'session.json')
    phase = s['phase_deg']
    assert phase in [180, 195, 210] and s['state'] == 'verified-board-phase-records-awaiting-independent-audit'
    assert not s['flash_written'] and s['known_input_hz'] == 50000
    assert datetime.fromisoformat(s['started_utc']) < datetime.fromisoformat(s['completed_utc'])
    assert hashlib.sha256((directory/'platform.bit').read_bytes()).hexdigest().upper() == s['bit_sha256']
    assert hashlib.sha256((directory/'application.elf').read_bytes()).hexdigest().upper() == s['elf_sha256']
    manifest = s['phase_manifest']
    if phase == 195:
        assert s['bit_sha256'] == manifest['nominal_bit_sha256']
    else:
        entries = [e for e in manifest['entries'] if e['phase_deg'] == phase]
        assert len(entries) == 1 and entries[0]['bit_sha256'] == s['bit_sha256']
    log_bytes = (directory/'uart.log').read_bytes()
    assert hashlib.sha256(log_bytes).hexdigest().upper() == s['uart_sha256']
    log = log_bytes.decode('utf-8-sig')
    assert re.search(r'ADC_LIVE_READY:[^\r\n]*conversion_hz=50000000[^\r\n]*ABI=00070000', log)
    assert not re.search(r'APP_FATAL|APP_LOG_ERROR|ADC_LIVE_FAIL|DDR_FAIL|VIDEO_PRESENT_FAIL|VIDEO_STOP_TIMEOUT', log)
    assert not re.search(r'APP_DIAG[^\r\n]*dma_errors=(?!00000000)[0-9a-fA-F]{8}', log)
    assert 'BOOT_MODE=0' in (directory/'boot.log').read_text(encoding='utf-8-sig')
    assert len(s['long_records']) == 6
    names = {f'phase{phase}-{kind}-{rep}' for kind in ['raw', 'cic6m25'] for rep in [1, 2, 3]}
    assert {e['name'] for e in s['long_records']} == names
    details, frames = [], 0
    controls = {c['test']: c for c in s['controls']}
    assert len(controls) == len(s['controls']) == (21 if phase == 195 else 13)
    previous = None
    for e in s['long_records']:
        u, a = e['ui'], e['adc']
        raw = '-raw-' in e['name']
        index, rate, code, kind = (5, 50000000, 4, 0) if raw else (6, 6250000, 5, 3)
        assert u['time'] == str(index) and e['rate'] == rate and u['source'] == 'ADC2' and u['trig_channel'] == '2'
        assert u['trig_rising'] == '1' and u['level_mv'] == '500' and u['running'] == '0' and u['single_complete'] == '1'
        assert a['pending'] == a['configbusy'] == '0' and int(a['status'], 16) == 0x82
        assert a['id'] == a['display_id'] == u['display_id'] == e['id']
        assert a['capture_epoch'] == a['display_epoch'] == u['epoch']
        assert int(a['rate_index']) == code and int(a['rate']) == int(a['applied_rate']) == int(a['display_rate']) == rate
        assert int(a['conversion_hz']) == int(a['display_conversion_hz']) == 50000000
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
            assert re.search(r'addr=10[02]00000 bytes=1843200 format=BGR888 crc='+re.escape(e['frame_crc']), log, re.I)
            frames += 1
        details.append(detail)
    slow_details = []
    if phase == 195:
        assert len(s['slow_records']) == 3 and float(s['slow_publish_seconds_20']) >= 6
        for e, index, rate in zip(s['slow_records'], [12, 15, 20], [62500, 6250, 625]):
            u, a = e['ui'], e['adc']
            assert e['name'] == f'nominal50m-slow-{index}' and e['rate'] == rate
            assert u['time'] == str(index) and u['running'] == '0' and u['source'] == 'ADC2'
            assert u['level_mv'] == '4500' and u['trig_normal'] == '0' and u['triggered'] == '0'
            assert a['id'] == a['display_id'] == u['display_id'] == e['id']
            assert a['address'] == a['display_address'] and a['display_address'] in ['13000000', '13004000']
            assert int(a['bytes']) == 16384 and a['pending'] == '0'
            assert e['crc32'].upper() == a['display_crc'].upper() == a['crc32'].upper()
            assert int(a['display_rate']) == int(a['rate']) == int(a['requested_rate']) == int(a['applied_rate']) == rate
            assert a['rate_index'] == str({62500: 1, 6250: 2, 625: 3}[rate])
            assert a['filter_kind'] == '3' and a['conversion_hz'] == '50000000'
            assert a['display_filter'] == a['record_filter'] == '3' and a['display_conversion_hz'] == '50000000'
            # acqstop creates a cancellation epoch while preserving old data.
            assert (int(u['epoch'])-int(a['display_epoch'])) % 2**32 == 1
            data = checked_bytes(directory/(e['name']+'.bin'), e['crc32'], e['sha256'], 16384)
            words = struct.unpack('<4096I', data)
            assert not any(w & 0xf800f800 or w & ((1 << 10) | (1 << 26)) for w in words)
            d = check_designated(words, e, TIMES)
            assert d['auto_fallback'] and a['hw_after'] == '0' and a['hw_matched'] == '0' and a['hw_auto'] == '1'
            exported = controls[e['name']+'-independent-DDR-CRC-and-timebase']
            assert exported['ui'] == u and exported['adc'] == a
            live = controls[f'nominal50M-slow-{index}-full-AUTO-fallback']
            assert live['ui']['time'] == str(index) and live['ui']['running'] == '1' and live['ui']['triggered'] == '0'
            assert live['adc']['display_epoch'] == live['ui']['epoch'] == a['display_epoch']
            delta = (int(u['display_id'])-int(live['ui']['display_id'])) % 2**32
            assert delta < 2**31
            if index == 20:
                checked_bytes(directory/(e['name']+'-frame.bin'), e['frame_crc'], e['frame_sha256'], 1843200)
                assert re.search(r'addr=10[02]00000 bytes=1843200 format=BGR888 crc='+re.escape(e['frame_crc']), log, re.I)
                frames += 1
            else:
                assert 'frame_crc' not in e
            d.update(name=e['name'], rate=rate, crc32=e['crc32'], calibrated=False)
            slow_details.append(d)
        wait = s['slow_wait_progress']
        assert wait['minimum_samples'] == 8192 and wait['elapsed_seconds'] >= 12
        before, after = wait['anchor'], wait['after']
        advance = (int(after['adc']['hw_progress'])-int(before['adc']['hw_progress'])) % 2**32
        assert 8192 <= advance < 2**31
        for item in [before, after]:
            assert item['ui']['time'] == '20' and item['ui']['trig_normal'] == '1' and item['ui']['level_mv'] == '4500'
            assert item['adc']['rate_index'] == '3' and item['adc']['requested_rate'] == item['adc']['applied_rate'] == '625'
            assert item['adc']['filter_kind'] == item['adc']['display_filter'] == '3'
            assert item['adc']['conversion_hz'] == item['adc']['display_conversion_hz'] == '50000000'
            assert item['ui']['single_armed'] == item['adc']['pending'] == item['adc']['hw_waiting'] == '1'
            assert item['ui']['single_complete'] == '0' and item['adc']['capture_epoch'] == item['ui']['epoch']
            for key in ['id', 'display_id', 'display_address', 'display_crc', 'display_rate', 'display_filter', 'display_conversion_hz', 'display_epoch']:
                assert item['adc'][key] == s['slow_records'][-1]['adc'][key]
        assert after['ui']['epoch'] == before['ui']['epoch']
        waited = controls['nominal50M-slow-single-two-rings-keeps-waiting']
        assert waited['ui'] == after['ui'] and waited['adc'] == after['adc']
        canceled = controls['nominal50M-slow-cancel-keeps-frozen-record']
        assert canceled['ui']['running'] == canceled['ui']['single_armed'] == canceled['adc']['pending'] == '0'
        assert int(canceled['ui']['epoch']) == (int(after['ui']['epoch'])+1) % 2**32
        assert canceled['ui']['display_id'] == after['ui']['display_id']
        for key in ['id', 'display_id', 'display_address', 'display_crc', 'display_rate', 'display_filter', 'display_conversion_hz', 'display_epoch']:
            assert canceled['adc'][key] == after['adc'][key]
    else:
        assert not s.get('slow_records')
    final = controls[f'phase{phase}-final-RAW50M-auto-running']
    u, a = final['ui'], final['adc']
    assert u['time'] == '5' and u['source'] == 'ADC2' and u['trig_channel'] == '2'
    assert u['running'] == u['dual_visible'] == u['triggered'] == '1'
    assert u['trig_normal'] == u['cursors'] == '0'
    assert a['display_rate'] == a['applied_rate'] == '50000000' and a['display_filter'] == '0'
    assert a['display_conversion_hz'] == '50000000' and a['display_epoch'] == u['epoch']
    result = dict(state='verified-offline-phase-lab-evidence', phase_deg=phase,
                  raw_exports=6+len(slow_details), ddr_frames=frames, records=details, slow_records=slow_details,
                  started_utc=s['started_utc'], completed_utc=s['completed_utc'],
                  elf_sha256=s['elf_sha256'], phase_source={key: manifest[key] for key in ['source_routed_sha256','script_sha256','nominal_bit_sha256','only_design_property_changed','conversion_hz']},
                  final_control=final,
                  scope='current wiring and50kHz source; not PVT or analog bandwidth proof')
    (directory/'phase-evidence-audit.json').write_text(json.dumps(result, indent=2)+'\n')
    return result


def main():
    p = argparse.ArgumentParser()
    p.add_argument('runs', nargs='+', type=Path)
    a = p.parse_args()
    results = [audit_run(r) for r in a.runs]
    assert [r['phase_deg'] for r in results] == [180, 210, 195], 'One run per phase, nominal final restore required'
    assert all(r['elf_sha256'] == results[0]['elf_sha256'] and r['phase_source'] == results[0]['phase_source'] for r in results)
    for before, after in zip(results, results[1:]):
        assert datetime.fromisoformat(before['completed_utc']) <= datetime.fromisoformat(after['started_utc']), 'Phase run time order mismatch'
    report = dict(state='verified-three-phase-lab-comparison', phases=results,
                  raw_exports=sum(r['raw_exports'] for r in results),
                  ddr_frames=sum(r['ddr_frames'] for r in results), final_phase_deg=195,
                  guaranteed_ADC_minmax='unavailable', analog_bandwidth='not-measured')
    (a.runs[-1]/'three-phase-comparison.json').write_text(json.dumps(report, indent=2)+'\n')
    print('THREE_PHASE_EVIDENCE_PASS', json.dumps(report))


if __name__ == '__main__':
    main()
