"""Independent frozen-record verification for ABI4 ring trigger sessions."""
from pathlib import Path
import argparse
import hashlib
import json
import math
import re
import struct
import zlib

DIVISIONS = [.00005, .0001, .0002, .0005, .001, .002, .005, .01, .02, .05, .1, .2, .5]


def load(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def checked_bytes(path, crc, digest, size):
    data = path.read_bytes()
    assert len(data) == size and f'{zlib.crc32(data):08X}' == crc.upper(), path
    assert hashlib.sha256(data).hexdigest().upper() == digest.upper(), path
    return data


def check_designated(words, item, divisions=DIVISIONS):
    ui, adc = item['ui'], item['adc']
    assert adc['id'] == ui['display_id'] == adc['display_id']
    assert int(adc['hw_seen']) >= 4096 and 0 <= int(adc['hw_start']) < 4096
    if int(adc['hw_seen']) != 0xffffffff:
        assert int(adc['hw_start']) == int(adc['hw_seen']) % 4096
    index = int(ui['time'])
    assert 0 <= index < len(divisions) and int(adc['display_rate']) == item['rate']
    span = 10*divisions[index]*item['rate']
    start, cross = float(ui['view_start']), float(ui['crossing'])
    assert all(math.isfinite(x) for x in [start, cross]) and start >= 0 and start+span <= 4095.004
    matched = adc['hw_matched'] == '1'
    assert (matched, adc['hw_auto']) in [(True, '0'), (False, '1')]
    if matched:
        assert ui['triggered'] == '1'
        after = int(adc['hw_after'])
        assert after == math.ceil(.2*span)+1 and 1 <= after < 4096
        ch = int(ui['trig_channel'])-1
        assert ch in [0, 1]
        assert not (words[after-1] | words[after]) & (1 << (16*ch+10))
        a, b = ((words[i] >> (16*ch)) & 1023 for i in [after-1, after])
        level = math.floor((int(ui['level_mv'])/1000+5)*102.3*256+.5)/256
        if ui['trig_rising'] == '1':
            assert a <= level < b
        else:
            assert a >= level > b
        actual = after-1+(level-a)/(b-a)
        assert abs(actual-cross) < .001 and abs(cross-start-.2*span) < .004
        if ui['single_complete'] == '1':
            assert adc['display_epoch'] == ui['epoch']
        # Count earlier raw crossings; they are not substitutes for hardware's
        # nominated pair, even though several cycles precede the event.
        codes = [(w >> (16*ch)) & 1023 for w in words]
        earlier = sum(x <= level < y if ui['trig_rising'] == '1' else x >= level > y
                      for x, y in zip(codes[:after-1], codes[1:after]))
        return dict(after=after, actual_crossing=actual, earlier_raw_edges=earlier,
                    ring_start=int(adc['hw_start']), samples_seen=int(adc['hw_seen']))
    assert ui['triggered'] == '0' and adc['hw_after'] == '0' and cross == start == 0
    return dict(auto_fallback=True, samples_seen=int(adc['hw_seen']))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('runs', nargs='+', type=Path)
    args = parser.parse_args()
    raw_count = frame_count = 0
    details = []
    for directory in args.runs:
        session = load(directory/'session.json')
        assert session['state'].startswith('verified-board-') and not session['flash_written']
        log = (directory/'uart.log').read_text(encoding='utf-8-sig')
        assert not re.search(r'APP_FATAL|APP_LOG_ERROR|ADC_LIVE_FAIL|DDR_FAIL', log)
        assert not re.search(r'APP_DIAG[^\r\n]*dma_errors=(?!00000000)[0-9a-fA-F]{8}', log)
        for item in session.get('records', []):
            checked_bytes(directory/item['file'], item['crc32'], item['sha256'], 16384)
            raw_count += 1
        for stem in ['dual', 'cursor', 'channel', 'trigsrc']:
            item = session.get(stem+'_frame')
            if item:
                checked_bytes(directory/(stem+'-single-record.bin'), item['record_crc'], item['record_sha256'], 16384)
                checked_bytes(directory/item['file'], item['crc32'], item['sha256'], 1843200)
                raw_count += 1
                frame_count += 1
        for item in session['long_records']:
            name = item['name']
            data = checked_bytes(directory/(name+'.bin'), item['crc32'], item['sha256'], 16384)
            words = struct.unpack('<4096I', data)
            assert not any(w & 0xf800f800 for w in words)
            detail = check_designated(words, item)
            detail.update(run=directory.name, name=name, crc32=item['crc32'], rate=item['rate'])
            details.append(detail)
            raw_count += 1
            if 'frame_crc' in item:
                checked_bytes(directory/(name+'-frame.bin'), item['frame_crc'], item['frame_sha256'], 1843200)
                assert re.search(r'addr=10[02]00000 bytes=1843200 format=BGR888 crc='+item['frame_crc'], log, re.I)
                frame_count += 1
        if 'slow_wait_elapsed_seconds' in session:
            assert session['slow_wait_elapsed_seconds'] >= 12 and not session['download_performed']
            assert .95 <= session['independent_ch2_frequency_hz'] <= 1.05
            fast = next(d for d in details if d['run'] == directory.name and d['name'] == 'hw-fast-1hz')
            assert fast['samples_seen'] >= 12288
    result = dict(state='verified-offline-ring-evidence', raw_exports=raw_count, ddr_frames=frame_count,
                  control_count=sum(len(load(r/'session.json')['controls']) for r in args.runs), designated_records=details,
                  physical_panel='separate-user-feedback')
    (args.runs[-1]/'hardware-trigger-evidence-audit.json').write_text(json.dumps(result, indent=2)+'\n')
    print('HARDWARE_TRIGGER_EVIDENCE_PASS', json.dumps(result))


if __name__ == '__main__':
    main()
