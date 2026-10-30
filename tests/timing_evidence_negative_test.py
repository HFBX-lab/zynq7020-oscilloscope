"""Mutate actual new-version evidence metadata without changing any original file."""
import copy
import json
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from audit_timing_evidence import audit

directory = Path(sys.argv[1])
source = json.loads((directory / 'session.json').read_text(encoding='utf-8-sig'))
audit(directory, source)
changes = [
    ('rate', lambda s: s['timing_exports'][0].__setitem__('rate', 6250000)),
    ('ID', lambda s: s['timing_exports'][0]['timing_snapshot']['timing']['context'].__setitem__('display_id', '0')),
    ('epoch', lambda s: s['timing_exports'][0]['timing_snapshot']['timing']['context'].__setitem__('epoch', '0')),
    ('page', lambda s: s['timing_exports'][0]['timing_snapshot']['timing']['context'].__setitem__('timing', '0')),
    ('channel', lambda s: s['timing_exports'][0]['timing_snapshot']['timing']['channels'][1].__setitem__('ch', '1')),
    ('raw hash', lambda s: s['timing_exports'][0].__setitem__('sha256', '0' * 64)),
    ('frame hash', lambda s: s['timing_exports'][0].__setitem__('frame_sha256', '0' * 64)),
    ('replay hash', lambda s: s['timing_exports'][3].__setitem__('blob_sha256', '0' * 64)),
    ('replay rate', lambda s: s['timing_exports'][3]['timing_snapshot']['memory']['view'].__setitem__('rate', '6250000')),
    ('return raw', lambda s: s['timing_return']['after']['memory']['view'].__setitem__('record_crc', '00000000')),
    ('return cache', lambda s: s['timing_return']['after']['timing']['channels'][1].__setitem__('duty_percent', '0.000000')),
]
for field in ('pulse_valid', 'rise_valid', 'fall_valid', 'cycles', 'duty_percent',
              'positive_us', 'negative_us', 'period_us', 'rise_us', 'fall_us',
              'low_v', 'mid_v', 'high_v'):
    def mutate(s, field=field):
        item = s['timing_exports'][0]['timing_snapshot']['timing']['channels'][1]
        item[field] = str(1 - int(item[field])) if field.endswith('valid') else str(float(item[field]) + 1)
    changes.append((field, mutate))
def coordinated_id(s):
    e = s['timing_exports'][0]
    e['id'] = '99999999'
    snap = e['timing_snapshot']
    for obj in (snap['ui'], snap['adc'], snap['memory']['view'], snap['measurement']['context'],
                snap['timing']['context'], snap['parameter']['status']):
        obj['display_id'] = '99999999'
changes.append(('coordinated forged ID absent from UART', coordinated_id))
for name, change in changes:
    broken = copy.deepcopy(source)
    change(broken)
    try:
        audit(directory, broken)
    except (AssertionError, ValueError, KeyError):
        continue
    raise AssertionError('Bad evidence accepted: ' + name)
print('TIMING_NEGATIVE_TEST_PASS:', len(changes), 'actual evidence corruptions rejected')
