"""Read-only audit of frozen main+low-frequency board sessions."""
from pathlib import Path
import argparse
import hashlib
import json
import math
import re
import struct
import zlib


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def raw(path, crc, digest):
    data = path.read_bytes()
    assert len(data) == 16384 and f'{zlib.crc32(data):08X}' == crc.upper(), path
    assert sha(path) == digest.upper(), path
    words = struct.unpack('<4096I', data)
    assert not any(w & 0xf800f800 for w in words), path
    return words


def frame(path, crc, digest):
    data = path.read_bytes()
    assert len(data) == 1843200 and f'{zlib.crc32(data):08X}' == crc.upper(), path
    assert sha(path) == digest.upper(), path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('main_run', type=Path)
    parser.add_argument('lowfreq_run', type=Path)
    args = parser.parse_args()
    total_raw, total_frames = 0, 0
    for directory in [args.main_run, args.lowfreq_run]:
        state = read(directory/'session.json')
        assert state['state'].startswith('verified-board-') and not state['flash_written']
        log = (directory/'uart.log').read_text(encoding='utf-8-sig')
        assert not re.search(r'APP_FATAL|APP_LOG_ERROR|ADC_LIVE_FAIL|DDR_FAIL', log)
        assert not re.search(r'APP_DIAG[^\r\n]*dma_errors=(?!00000000)[0-9a-fA-F]{8}', log)
        for item in state.get('records', []):
            raw(directory/item['file'], item['crc32'], item['sha256'])
            total_raw += 1
        for stem in ['dual', 'cursor', 'channel', 'trigsrc']:
            item = state.get(stem+'_frame')
            if item:
                raw(directory/(stem+'-single-record.bin'), item['record_crc'], item['record_sha256'])
                frame(directory/item['file'], item['crc32'], item['sha256'])
                total_raw += 1
                total_frames += 1
        for item in state['long_records']:
            name = item['name']
            words = raw(directory/(name+'.bin'), item['crc32'], item['sha256'])
            total_raw += 1
            ui = item['ui']
            start, crossing = float(ui['view_start']), float(ui['crossing'])
            assert math.isfinite(start) and math.isfinite(crossing) and start >= 0
            divisions = [.00005, .0001, .0002, .0005, .001, .002, .005, .01, .02, .05, .1, .2, .5]
            span = 10*divisions[int(ui['time'])]*item['rate']
            assert start+span <= 4095.004
            if ui['triggered'] == '1':
                assert abs(crossing-start-.2*span) <= .004
                channel = int(ui['trig_channel'])-1
                threshold = (int(ui['level_mv'])/1000+5)*102.3
                index = math.floor(crossing)
                a, b = (words[index] >> (16*channel)) & 1023, (words[index+1] >> (16*channel)) & 1023
                assert a <= threshold < b and abs(index+(threshold-a)/(b-a)-crossing) <= .004
            if 'frame_crc' in item:
                frame(directory/(name+'-frame.bin'), item['frame_crc'], item['frame_sha256'])
                # UART must identify the exact frame under the established
                # framebuffer size/address/format contract, not just a CRC.
                assert re.search(r'addr=10[02]00000 bytes=1843200 format=BGR888 crc='+item['frame_crc'], log, re.I)
                total_frames += 1
    main_state, low_state = read(args.main_run/'session.json'), read(args.lowfreq_run/'session.json')
    assert len(main_state['controls']) == 83 and len(low_state['controls']) == 7
    assert 6 <= low_state['slow_capture_elapsed_seconds'] <= 9
    assert .95 <= low_state['independent_ch2_frequency_hz'] <= 1.05
    assert not low_state['download_performed']
    a, b = low_state['long_records']
    assert a['id'] == b['id'] and a['crc32'] == b['crc32'] and a['rate'] == b['rate'] == 625
    assert abs(float(b['ui']['cursor_dt_us'])-2500000) <= .0001
    assert abs(float(b['ui']['cursor_inv_hz'])-.4) <= .000001
    result = dict(state='verified-offline-evidence-audit', raw_exports=total_raw, actual_ddr_frames=total_frames,
                  main_controls=83, lowfreq_controls=7, lowfreq_hz=low_state['independent_ch2_frequency_hz'],
                  slow_capture_elapsed_seconds=low_state['slow_capture_elapsed_seconds'],
                  physical_panel_feedback='separate-user-evidence')
    (args.lowfreq_run/'independent-evidence-audit.json').write_text(json.dumps(result, indent=2)+'\n')
    print('LONG_TIMEBASE_EVIDENCE_AUDIT_PASS', json.dumps(result))


if __name__ == '__main__':
    main()
