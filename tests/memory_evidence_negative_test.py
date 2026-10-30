"""Offline auditor contracts and mutations of a real, immutable board session."""
from pathlib import Path
import copy
import importlib
import json
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
try:
    auditor = importlib.import_module('audit_memory_evidence')
except ModuleNotFoundError:
    auditor = None


class AuditContracts(unittest.TestCase):
    def test_required_auditor_exists(self):
        self.assertIsNotNone(auditor, 'independent memory evidence auditor is missing')

    def test_epoch_exact_increment_and_wrap(self):
        self.assertIsNotNone(auditor, 'independent memory evidence auditor is missing')
        auditor.check_epoch({'epoch': '4294967295'}, {'epoch': '0'}, 1)
        with self.assertRaises(AssertionError):
            auditor.check_epoch({'epoch': '9'}, {'epoch': '11'}, 1)

    def test_preference_comparison_ignores_frame_counter(self):
        self.assertIsNotNone(auditor, 'independent memory evidence auditor is missing')
        auditor.equal_fields({'time': '6', 'frames': '3'}, {'time': '6', 'frames': '4'}, ('time',))
        with self.assertRaises(AssertionError):
            auditor.equal_fields({'time': '6'}, {'time': '5'}, ('time',))

    def test_cursor_uses_record_rate_and_record_axis(self):
        self.assertIsNotNone(auditor, 'independent memory evidence auditor is missing')
        words = tuple((n % 1024) << 16 for n in range(4096))
        snapshot = {'ui': {'cursors': '1', 'cursor_valid': '1', 'cursor_a': '250',
            'cursor_b': '650', 'source': 'ADC2', 'time': '3', 'cursor_dt_us': '4.000000',
            'cursor_inv_hz': '250000.000000', 'cursor_dv': str(200 * 10 / 1023)},
            'memory': {'view': {'rate': '50000000', 'start': '0.000000'}}}
        auditor._cursor(words, snapshot)
        snapshot['memory']['view']['rate'] = '6250000'
        with self.assertRaises(AssertionError):
            auditor._cursor(words, snapshot)

    def test_snapshot_must_be_present_in_actual_uart(self):
        self.assertIsNotNone(auditor, 'independent memory evidence auditor is missing')
        index = auditor._log_index('UI_STATUS epoch=8 running=0\n')
        auditor._bind(index, 'UI_STATUS', {'epoch': '8', 'running': '0'})
        with self.assertRaises(AssertionError):
            auditor._bind(index, 'UI_STATUS', {'epoch': '9', 'running': '0'})


def real_negatives(directory):
    original = auditor.load(directory / 'session.json')
    baseline = auditor.audit(directory)
    mutations = [
        ('missing-control', lambda s: s['memory_checks'].pop()),
        ('blob-crc', lambda s: s['memory_exports'][0].update(blob_crc='00000000')),
        ('blob-hash', lambda s: s['memory_exports'][0].update(blob_sha256='0' * 64)),
        ('payload-hash', lambda s: s['memory_exports'][1].update(record_sha256='0' * 64)),
        ('frame-hash', lambda s: s['memory_exports'][1].update(frame_sha256='0' * 64)),
        ('replay-label', lambda s: s['memory_exports'][1]['memory']['status'].update(replay='0')),
        ('replay-rate', lambda s: s['memory_exports'][1]['memory']['view'].update(rate='50000000')),
        ('replay-converter', lambda s: s['memory_exports'][1]['memory']['view'].update(actual_conversion='6250000')),
        ('replay-epoch', lambda s: s['memory_exports'][1]['ui'].update(epoch='0')),
        ('replay-pending', lambda s: s['memory_replay_wait']['after']['adc'].update(pending='1')),
        ('replay-raw-id', lambda s: s['memory_replay_wait']['after']['adc'].update(id='0')),
        ('replay-wait-too-short', lambda s: s['memory_replay_wait'].update(elapsed_ms=0)),
        ('protected-live-hash', lambda s: s['memory_exports'][1].update(protected_sha256='0' * 64)),
        ('return-epoch', lambda s: s['memory_return']['after']['ui'].update(epoch='0')),
        ('return-cache', lambda s: s['memory_return']['after']['measurement']['channels'][1].update(mean_v='123.000000')),
        ('settings-rate', lambda s: s['memory_settings_restore']['after']['memory']['view'].update(rate='6250000')),
        ('settings-current-id', lambda s: s['memory_settings_restore']['after']['ui'].update(display_id='0')),
        ('running-save-ack', lambda s: s['memory_live_update']['save_ack'].update(running='0')),
        ('running-before-epoch', lambda s: s['memory_live_update']['running_before']['ui'].update(epoch='0')),
        ('slot-source-id', lambda s: s['memory_exports'][3]['memory']['status'].update(saved_id='0')),
        ('source-raw-hash', lambda s: s['memory_sources'][0].update(sha256='0' * 64)),
        ('cursor-time', lambda s: s['memory_exports'][2]['ui'].update(cursor_dt_us='0.000000')),
        ('measurement-rms', lambda s: s['memory_exports'][1]['measurement']['channels'][1].update(rms_total_v='0.000000')),
    ]
    rejected = []
    for name, mutation in mutations:
        candidate = copy.deepcopy(original)
        mutation(candidate)
        with patch.object(auditor, 'load', return_value=candidate):
            try:
                auditor.audit(directory)
            except (AssertionError, KeyError, ValueError):
                rejected.append(name)
            else:
                raise AssertionError('False positive: ' + name)
    result = dict(state='offline-only-real-memory-evidence-negative-tests',
        count=len(rejected), rejected=rejected, original_run=str(directory), baseline=baseline['state'])
    (directory / 'memory-negative-tests.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print('MEMORY_NEGATIVES_PASS', len(rejected))


if __name__ == '__main__':
    if len(sys.argv) == 2 and Path(sys.argv[1]).is_dir():
        real_negatives(Path(sys.argv[1]))
    else:
        unittest.main()
