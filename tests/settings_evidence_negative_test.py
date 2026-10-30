"""Independent settings-auditor contracts and mutations of an actual board run."""
import copy
import importlib
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
try:
    auditor = importlib.import_module('audit_settings_evidence')
except ModuleNotFoundError:
    auditor = None


class SettingsAuditContracts(unittest.TestCase):
    def test_auditor_exists(self):
        self.assertIsNotNone(auditor, 'independent settings evidence auditor is missing')

    def setUp(self):
        if auditor is None and self._testMethodName != 'test_auditor_exists':
            self.skipTest('auditor not implemented')

    def test_epoch_wrap_and_exact_increment(self):
        auditor.check_epoch({'epoch': '4294967295'}, {'epoch': '0'}, 1)
        with self.assertRaises(AssertionError):
            auditor.check_epoch({'epoch': '8'}, {'epoch': '10'}, 1)

    def test_uart_binding_is_required(self):
        index = auditor._log_index('PARAM_END epoch=4\n')
        auditor._bind(index, 'PARAM_END', {'epoch': '4'})
        with self.assertRaises(AssertionError):
            auditor._bind(index, 'PARAM_END', {'epoch': '5'})

    def test_settings_ui_physical_mapping_and_flags(self):
        settings = dict(timebase_ns=10000, origin_conversion_hz=25000000,
            selected_channel=1, trigger_channel=0, trigger_level_mv=-250,
            flags=31, scale_index=[2, 3], position_mdiv=[-500, 1000],
            cursor=[250, 750], language=1)
        ui = dict(time='6', source='ADC2', trig_channel='1', level_mv='-250',
            trig_rising='1', trig_normal='1', dual='1', grid='1', cursors='1',
            ch1_scale='2', ch2_scale='3', ch1_pos='-500', ch2_pos='1000',
            cursor_a='250', cursor_b='750', lang='en')
        auditor.check_settings_ui(settings, ui, 50000000)
        bad = dict(ui, time='5')
        with self.assertRaises(AssertionError):
            auditor.check_settings_ui(settings, bad, 50000000)
        with self.assertRaises(AssertionError):
            auditor.check_settings_ui(settings, ui, 5000000)

    def test_exact_saved_config_fields(self):
        settings = dict(timebase_ns=200000, origin_conversion_hz=50000000,
            selected_channel=0, trigger_channel=0, trigger_level_mv=500,
            flags=9, scale_index=[2, 2], position_mdiv=[0, 0],
            cursor=[250, 750], language=0)
        config = dict(time_ns='200000', origin_conversion='50000000',
            selected_channel='0', trigger_channel='0', level_mv='500', flags='9',
            ch1_scale='2', ch2_scale='2', ch1_pos='0', ch2_pos='0',
            cursor_a='250', cursor_b='750', language='0')
        auditor.check_config(settings, config)
        for field in config:
            bad = dict(config)
            bad[field] = str(int(bad[field]) + 1)
            with self.subTest(field=field), self.assertRaises(AssertionError):
                auditor.check_config(settings, bad)

    def test_old_memory_run_is_not_settings_proof(self):
        with patch.object(auditor, 'load', return_value={
                'state': 'verified-board-memory', 'flash_written': False}):
            with self.assertRaises((AssertionError, KeyError)):
                auditor.audit(Path('D:/FPGA_Project/SP2_Settings_Build/python-oracle'))

    def test_export_addresses_and_frame_crc_are_bound_to_uart(self):
        name = 'param-saved-en'
        script = '\n'.join('mrd -bin -file {D:/run/' + name + suffix + '} 0x' + address + ' ' + count
            for suffix, address, count in (('.scp', '00100000', '24'),
                ('.scw', '00200000', '4128'), ('.bin', '13004000', '4096'),
                ('-frame.bin', '10200000', '460800')))
        snapshot = {'parameter': {'status': {'blob_address': '00100000',
            'record_address': '13004000'}}, 'memory': {'status': {'blob_address': '00200000'}}}
        index = auditor._log_index('SNAPSHOT_READY addr=10200000 bytes=1843200 format=BGR888 crc=ABCDEF12 frames=4\n')
        with patch.object(Path, 'read_text', return_value=script):
            auditor._export_addresses(Path('D:/run'), name, snapshot, index, 'ABCDEF12')
            with self.assertRaises(AssertionError):
                auditor._export_addresses(Path('D:/run'), name, snapshot, index, '00000000')
        for original, wrong in (('0x00100000 24', '0x00100004 24'),
                ('0x13004000 4096', '0x13000000 4096'),
                ('0x10200000 460800', '0x11000000 460800'),
                ('0x10200000 460800', '0x10200000 16384')):
            with patch.object(Path, 'read_text', return_value=script.replace(original, wrong)):
                with self.subTest(original=original), self.assertRaises(AssertionError):
                    auditor._export_addresses(Path('D:/run'), name, snapshot, index, 'ABCDEF12')


def real_negatives(directory):
    original = auditor.load(directory / 'session.json')
    baseline = auditor.audit(directory)
    mutations = [
        ('missing-control', lambda s: s['parameter_checks'].pop()),
        ('missing-export', lambda s: s['parameter_exports'].pop()),
        ('settings-crc', lambda s: s['parameter_exports'][0].update(settings_crc='00000000')),
        ('settings-hash', lambda s: s['parameter_exports'][0].update(settings_sha256='0' * 64)),
        ('wave-hash', lambda s: s['parameter_exports'][1].update(wave_sha256='0' * 64)),
        ('raw-hash', lambda s: s['parameter_exports'][2].update(raw_sha256='0' * 64)),
        ('frame-hash', lambda s: s['parameter_exports'][3].update(frame_sha256='0' * 64)),
        ('scp-config-time', lambda s: s['parameter_exports'][0]['snapshot']['parameter']['config'].update(time_ns='100')),
        ('scp-config-origin', lambda s: s['parameter_exports'][0]['snapshot']['parameter']['config'].update(origin_conversion='6250000')),
        ('scp-config-signed', lambda s: s['parameter_exports'][0]['snapshot']['parameter']['config'].update(ch1_pos='-3501')),
        ('raw-rate', lambda s: s['parameter_exports'][1]['snapshot']['memory']['view'].update(rate='50000000')),
        ('actual-clock', lambda s: s['parameter_exports'][2]['snapshot']['parameter']['status'].update(actual_conversion='6250000')),
        ('save-epoch', lambda s: s['parameter_saved']['after']['ui'].update(epoch='0')),
        ('load-single', lambda s: s['parameter_loaded']['after']['ui'].update(single_complete='1')),
        ('load-epoch', lambda s: s['parameter_loaded']['after']['ui'].update(epoch='0')),
        ('default-channel', lambda s: s['parameter_default']['after']['ui'].update(source='ADC2')),
        ('default-language', lambda s: s['parameter_default']['after']['ui'].update(lang='en')),
        ('default-slot-cleared', lambda s: s['parameter_default']['after']['parameter']['status'].update(valid='0')),
        ('default-cache', lambda s: s['parameter_default']['after']['measurement']['channels'][1].update(mean_v='123')),
        ('replay-epoch', lambda s: s['parameter_rejected']['after']['ui'].update(epoch='0')),
        ('running-save-stopped', lambda s: s['parameter_running_save']['save_ack'].update(running='0')),
        ('running-save-epoch', lambda s: s['parameter_running_save']['save_ack'].update(epoch='0')),
        ('queued-stop-epoch', lambda s: s['parameter_running_save']['stop_ack'].update(epoch='0')),
        ('demo-slot', lambda s: s['parameter_demo']['parameter']['status'].update(blob_crc='00000000')),
        ('final-stale-epoch', lambda s: s['parameter_final']['adc'].update(display_epoch='0')),
        ('uart-sha', lambda s: s.update(uart_sha256='0' * 64)),
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
    original_read = Path.read_bytes
    for suffix, offset in (('.scp', 35), ('.scw', 128), ('.bin', 0), ('-frame.bin', 1024)):
        target = directory / ('param-saved-en' + suffix)
        def changed_read(path, target=target, offset=offset):
            data = original_read(path)
            if path == target:
                data = bytearray(data)
                data[offset] ^= 1
                return bytes(data)
            return data
        with patch.object(Path, 'read_bytes', changed_read):
            try:
                auditor.audit(directory)
            except (AssertionError, KeyError, ValueError):
                rejected.append('actual-file-corruption' + suffix)
            else:
                raise AssertionError('Actual-file false positive: ' + suffix)
    original_text = Path.read_text
    def wrong_export_range(path, *args, **kwargs):
        content = original_text(path, *args, **kwargs)
        if path.name == 'export-param-saved-en.tcl':
            content = content.replace(' 460800', ' 16384')
        return content
    with patch.object(Path, 'read_text', wrong_export_range):
        try:
            auditor.audit(directory)
        except (AssertionError, KeyError, ValueError):
            rejected.append('actual-export-frame-range')
        else:
            raise AssertionError('False positive: actual-export-frame-range')
    result = dict(state='offline-only-real-settings-evidence-negative-tests',
        count=len(rejected), rejected=rejected, original_run=str(directory),
        baseline=baseline['state'])
    (directory / 'settings-negative-tests.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print('SETTINGS_NEGATIVES_PASS', len(rejected))


if __name__ == '__main__':
    if len(sys.argv) == 2 and Path(sys.argv[1]).is_dir():
        real_negatives(Path(sys.argv[1]))
    else:
        unittest.main()
