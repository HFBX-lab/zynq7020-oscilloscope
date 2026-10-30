"""FFT evidence contracts plus mutations of one actual board session."""
import importlib
import copy
import hashlib
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
import zlib

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
try:
    auditor = importlib.import_module('audit_spectrum_evidence')
except ModuleNotFoundError:
    auditor = None


class SpectrumEvidenceContracts(unittest.TestCase):
    def setUp(self):
        if auditor is None and self._testMethodName != 'test_auditor_exists':
            self.skipTest('FFT auditor not implemented')

    def test_auditor_exists(self):
        self.assertIsNotNone(auditor, 'independent FFT board auditor missing')

    def test_projection_preserves_narrow_and_endpoint_lines(self):
        bins = np.full(2049, 1e-8)
        bins[33] = .5; bins[2048] = .8
        full = auditor.project_bins(bins, 2048)
        self.assertEqual(full.shape, (720,))
        self.assertEqual(int(np.max(full)), round(20 * np.log10(.8) * 1000))
        self.assertTrue(np.any(full[:30] == round(20 * np.log10(.5) * 1000)))
        zoom = auditor.project_bins(bins, 128)
        self.assertEqual(int(np.max(zoom)), round(20 * np.log10(.5) * 1000))
        self.assertTrue(np.all(full >= -100000))

    def test_projection_check_rejects_wrong_range_or_points(self):
        bins = np.full(2049, 1e-8); bins[33] = .5
        status = dict(auto_range='1', end_bin='256')
        values = dict(peak_valid='1', peak_bin='32.76')
        plot = auditor.project_bins(bins, 256)
        auditor.check_plot(bins, plot, status, values)
        bad = plot.copy(); bad[0] = 20000
        with self.assertRaises(AssertionError): auditor.check_plot(bins, bad, status, values)
        with self.assertRaises(AssertionError): auditor.check_plot(bins, plot, dict(status, end_bin='128'), values)

    def test_range_accounts_only_for_printed_bin_rounding(self):
        self.assertEqual(auditor.allowed_end_bins(True, True, 32.7), {256})
        self.assertEqual(auditor.allowed_end_bins(True, True, 32.0), {128, 256})
        self.assertEqual(auditor.allowed_end_bins(False, True, 32), {2048})
        self.assertEqual(auditor.allowed_end_bins(True, False, 32), {2048})

    def test_metadata_maps_actual_source_and_flags(self):
        fft = dict(status=dict(valid='1', count='4096', rate='6250000', channel='1',
            window='1', remove_mean='1'), values=dict(overrange='0', peak_valid='1',
            bin_hz='1525.878906250', mean_v='.5', gain='.5', peak_bin='32.7',
            peak_hz='49900', peak_vrms='.35', peak_dbv='-9.118639'))
        meta = auditor.metadata_from_fft(fft)
        self.assertEqual(meta['rate_hz'], 6250000)
        self.assertEqual(meta['channel'], 1)
        self.assertEqual(meta['coherent_gain'], .5)
        self.assertFalse(meta['overrange'])
        self.assertTrue(meta['remove_mean'])

    def test_export_ranges_rejects_overlap_and_outside_elf(self):
        segments = [(0x100000, 0x400000)]
        good = dict(raw_address='00110000', raw_bytes='16384',
                    bins_address='00120000', bins_bytes='8196',
                    plot_address='00130000', plot_bytes='2880')
        auditor.check_ranges(good, segments)
        for bad in (dict(good, raw_address='13000000'),
                    dict(good, bins_address='00110020'), dict(good, plot_bytes='2881'),
                    dict(good, plot_address='00130001')):
            with self.assertRaises(AssertionError): auditor.check_ranges(bad, segments)

    def test_fft_reply_cannot_mix_individually_valid_uart_lines(self):
        h = dict.fromkeys(auditor.STATUS, '0')
        h.update(seq='1', actual_conversion='50000000', raw_address='00110000', raw_bytes='16384',
                 bins_address='00120000', bins_bytes='8196', plot_address='00130000', plot_bytes='2880',
                 raw_crc='00000000', bins_crc='00000000', plot_crc='00000000')
        v = dict.fromkeys(auditor.VALUES, '0'); v.update(wave_crc='00000000', param_crc='00000000')
        a = dict(status=h, values=v)
        b = dict(status=dict(h, seq='2'), values=dict(v, mean_v='1'))
        def packet(fft):
            return ('FFT_STATUS ' + ' '.join(k+'='+x for k,x in fft['status'].items()) + '\r\n'
                    + 'FFT_VALUES ' + ' '.join(k+'='+x for k,x in fft['values'].items()) + '\r\n'
                    + 'FFT_END seq=' + fft['status']['seq'] + '\r\n')
        log = packet(a) + packet(b)
        index, atomic = auditor._log_index(log), auditor._atomic_frames(log)
        auditor._fft(a, index, atomic, [(0x100000, 0x400000)])
        with self.assertRaises(AssertionError):
            auditor._fft(dict(status=h, values=b['values']), index, atomic, [(0x100000, 0x400000)])


def actual_negatives(directory):
    """Mutate loaded copies or read buffers only; never edit original evidence."""
    directory = directory.resolve()
    baseline = auditor.audit(directory)
    session = auditor.load(directory / 'session.json')
    real_load = auditor.load
    results = []
    rejected = (AssertionError, ValueError, KeyError, IndexError, TypeError)

    def trial(name, action, expected_message=None):
        try:
            action()
        except rejected as error:
            if expected_message is not None:
                assert expected_message in str(error), (name, 'failed before intended semantic assertion', error)
            results.append(dict(name=name, rejection=str(error)[:240]))
        else:
            raise AssertionError('Malformed spectrum evidence accepted: ' + name)

    def session_trial(name, mutate):
        bad = copy.deepcopy(session)
        mutate(bad)
        def load_bad(path):
            return bad if Path(path).resolve() == directory / 'session.json' else real_load(path)
        with patch.object(auditor, 'load', load_bad):
            trial(name, lambda: auditor.audit(directory))

    mutations = [
        ('missing-spectrum-export', lambda s:s['spectrum_exports'].pop()),
        ('wrong-export-order', lambda s:s['spectrum_exports'].reverse()),
        ('missing-frozen-check', lambda s:s['spectrum_checks'].pop(0)),
        ('wrong-source-rate', lambda s:s['spectrum_exports'][0]['fft']['status'].__setitem__('rate','50000000')),
        ('wrong-saved-source-epoch', lambda s:s['spectrum_exports'][0]['fft']['status'].__setitem__('source_epoch','999999')),
        ('wrong-source-id', lambda s:s['spectrum_exports'][0]['fft']['status'].__setitem__('id','999999')),
        ('actual-clock-relabelled', lambda s:s['spectrum_exports'][0]['fft']['status'].__setitem__('actual_conversion','25000000')),
        ('wrong-channel', lambda s:s['spectrum_exports'][0]['fft']['status'].__setitem__('channel','0')),
        ('wrong-window', lambda s:s['spectrum_exports'][0]['fft']['status'].__setitem__('window','0')),
        ('wrong-mean-setting', lambda s:s['spectrum_exports'][0]['fft']['status'].__setitem__('remove_mean','0')),
        ('wrong-bin-size', lambda s:s['spectrum_exports'][0]['fft']['status'].__setitem__('bins_bytes','8192')),
        ('wrong-plot-size', lambda s:s['spectrum_exports'][0]['fft']['status'].__setitem__('plot_bytes','2876')),
        ('fft-source-not-static', lambda s:s['spectrum_exports'][0]['fft']['status'].__setitem__('raw_address','13000000')),
        ('fft-buffers-overlap', lambda s:s['spectrum_exports'][0]['fft']['status'].__setitem__('bins_address',s['spectrum_exports'][0]['fft']['status']['raw_address'])),
        ('wrong-numpy-metadata-rate', lambda s:s['spectrum_exports'][0]['metadata'].__setitem__('rate_hz',50000000)),
        ('wrong-max-line-amplitude', lambda s:s['spectrum_exports'][0]['metadata'].__setitem__('peak_vrms',2.4375)),
        ('full-range-recomputed-sequence', lambda s:s['spectrum_checks'][1]['snapshot']['fft']['status'].__setitem__('seq','999999')),
        ('fft-open-cancelled-single', lambda s:s['spectrum_opened']['after']['ui'].__setitem__('single_complete','0')),
        ('fft-open-restarted-epoch', lambda s:s['spectrum_opened']['after']['ui'].__setitem__('epoch','999999')),
        ('fft-open-overwrote-wave-slot', lambda s:s['spectrum_opened']['after']['parameter']['status'].__setitem__('wave_crc','deadbeef')),
        ('background-live-did-not-advance', lambda s:s['spectrum_live_hold']['after']['ui'].__setitem__('display_id',s['spectrum_live_hold']['before']['ui']['display_id'])),
        ('background-relabeled-old-rate', lambda s:s['spectrum_live_hold']['fft_after']['status'].__setitem__('rate','50000000')),
        ('background-relabeled-old-id', lambda s:s['spectrum_live_hold']['fft_after']['status'].__setitem__('id',s['spectrum_live_hold']['after']['ui']['display_id'])),
        ('live-update-restarted', lambda s:s['spectrum_live_update']['after']['ui'].__setitem__('epoch','999999')),
        ('missing-cross-rate-replay', lambda s:s['spectrum_before_replay']['memory']['view'].__setitem__('rate','50000000')),
        ('replay-used-current-epoch', lambda s:s['spectrum_checks'][-1]['snapshot']['fft']['status'].__setitem__('source_epoch',s['spectrum_checks'][-1]['snapshot']['ui']['epoch'])),
        ('empty-channel-invented-peak', lambda s:s['spectrum_checks'][4]['snapshot']['fft']['values'].__setitem__('peak_valid','1')),
        ('demo-rejection-mutated-cache', lambda s:s['spectrum_demo']['fft_after']['status'].__setitem__('seq','999999')),
        ('final-left-fft-open', lambda s:s['spectrum_final']['fft']['status'].__setitem__('panel','1')),
        ('final-clock-metadata-wrong', lambda s:s['spectrum_final']['parameter']['status'].__setitem__('actual_conversion','25000000')),
        ('source-crc-mismatch', lambda s:s['spectrum_exports'][0].__setitem__('raw_crc','deadbeef')),
        ('bins-sha-mismatch', lambda s:s['spectrum_exports'][0].__setitem__('bins_sha256','F'*64)),
        ('plot-sha-mismatch', lambda s:s['spectrum_exports'][0].__setitem__('plot_sha256','F'*64)),
        ('frame-crc-mismatch', lambda s:s['spectrum_exports'][0].__setitem__('frame_crc','deadbeef')),
    ]
    for name, mutate in mutations:
        session_trial(name, mutate)

    read_bytes = Path.read_bytes
    first = session['spectrum_exports'][0]['name']
    for suffix in ('-raw.bin', '-bins.bin', '-plot.bin', '-frame.bin'):
        target = directory / (first + suffix)
        original = read_bytes(target)
        broken = bytearray(original); broken[len(broken)//2] ^= 1
        def altered(path, target=target, broken=bytes(broken)):
            return broken if path.resolve() == target else read_bytes(path)
        with patch.object(Path, 'read_bytes', altered):
            trial('actual-file-byte-' + suffix, lambda: auditor.audit(directory))

    text_reader = Path.read_text
    tcl = directory / ('export-' + first + '.tcl')
    def altered_tcl(path, *args, **kwargs):
        text = text_reader(path, *args, **kwargs)
        return text.replace(' 2049', ' 2048') if path.resolve() == tcl else text
    with patch.object(Path, 'read_text', altered_tcl):
        trial('actual-export-tcl-bin-count', lambda: auditor.audit(directory))
    meta = directory / (first + '-metadata.json')
    def altered_metadata(path, *args, **kwargs):
        text = text_reader(path, *args, **kwargs)
        if path.resolve() == meta:
            data = json.loads(text); data['peak_vrms'] = 2.4375
            return json.dumps(data)
        return text
    with patch.object(Path, 'read_text', altered_metadata):
        trial('actual-metadata-file-amplitude', lambda: auditor.audit(directory))

    def replace_values(value, old, new):
        if isinstance(value, dict): return {key:replace_values(item,old,new) for key,item in value.items()}
        if isinstance(value, list): return [replace_values(item,old,new) for item in value]
        # Firmware and parent-session CRC fields use %08x, including the case
        # where an old checksum happens to contain digits only.
        if isinstance(value, str) and value.upper() == old.upper(): return new.lower()
        return value

    # Re-sign both file hashes and UART state; only numerical/projection meaning
    # remains wrong, proving the audit goes beyond CRC checks or JSON consistency.
    for field, message in (('bins', 'FFT bin mismatch'), ('plot', 'FFT plot does not preserve')):
        target = directory / (first + '-' + field + '.bin')
        original = read_bytes(target)
        if field == 'bins':
            numbers = np.frombuffer(original, '<f4').copy()
            numbers[int(np.argmax(numbers[1:]))+1] *= 1.2
        else:
            numbers = np.frombuffer(original, '<i4').copy()
            numbers[int(np.argmax(numbers))] = -100000
        broken = numbers.tobytes()
        old_crc = session['spectrum_exports'][0][field + '_crc']
        new_crc = f'{zlib.crc32(broken):08X}'
        bad = replace_values(copy.deepcopy(session), old_crc, new_crc)
        bad['spectrum_exports'][0][field + '_sha256'] = hashlib.sha256(broken).hexdigest().upper()
        uart = read_bytes(directory/'uart.log')
        pattern = (field + '_crc=' + old_crc).encode('ascii')
        signed_uart = uart.replace(pattern, (field + '_crc=' + new_crc.lower()).encode('ascii'))
        assert uart != signed_uart, ('CRC must occur in actual UART', field)
        bad['uart_sha256'] = hashlib.sha256(signed_uart).hexdigest().upper()
        def signed_load(path, bad=bad):
            return bad if Path(path).resolve() == directory/'session.json' else real_load(path)
        def signed_bytes(path, target=target, broken=broken, signed_uart=signed_uart):
            if path.resolve() == target: return broken
            if path.resolve() == directory/'uart.log': return signed_uart
            return read_bytes(path)
        with patch.object(auditor, 'load', signed_load), patch.object(Path, 'read_bytes', signed_bytes):
            trial('resigned-' + field + '-semantic-corruption', lambda: auditor.audit(directory), message)

    print('SPECTRUM_NEGATIVES_PASS', len(results), 'spectra', baseline['spectra'])
    return results


if __name__ == '__main__':
    if len(sys.argv) > 1 and Path(sys.argv[1]).is_dir():
        actual_negatives(Path(sys.argv[1]));sys.exit(0)
    unittest.main()
