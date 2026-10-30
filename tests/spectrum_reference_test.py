"""Independent mathematical FFT contracts and optional C-fixture cross checks."""
import importlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
try:
    reference = importlib.import_module('spectrum_reference')
except ModuleNotFoundError:
    reference = None
N = 4096
EVIDENCE = Path('D:/FPGA_Project/SP2_Spectrum_Build/oracle')


def words_for(bin_number=32, amplitude=80, dc=563, second=None, phase=.31):
    at = np.arange(N)
    raw = dc + amplitude * np.sin(2 * np.pi * bin_number * at / N + phase)
    if second:
        raw += second[1] * np.sin(2 * np.pi * second[0] * at / N + .57)
    if np.min(raw) < 0 or np.max(raw) > 1023:
        raise ValueError('test input would clip')
    ch2 = np.rint(raw).astype(np.uint32)
    ch1 = np.rint(512 + 25 * np.cos(2 * np.pi * 95 * at / N)).astype(np.uint32)
    return ch1 | (ch2 << 16)


class SpectrumReferenceContracts(unittest.TestCase):
    def setUp(self):
        if reference is None and self._testMethodName != 'test_reference_exists':
            self.skipTest('reference not implemented')

    def test_reference_exists(self):
        self.assertIsNotNone(reference, 'independent numpy FFT reference missing')

    def test_dc_and_mean_removal(self):
        words = np.full(N, 512 | (614 << 16), np.uint32)
        kept = reference.reference_spectrum(words, 6250000, 1, 'rect', False)
        value = 614 * 10 / 1023 - 5
        self.assertAlmostEqual(kept['mean_v'], value, places=12)
        self.assertAlmostEqual(kept['vrms'][0], abs(value), places=12)
        self.assertLess(np.max(kept['vrms'][1:]), 1e-12)
        removed = reference.reference_spectrum(words, 6250000, 1, 'hann', True)
        self.assertLess(np.max(removed['vrms']), 1e-12)
        self.assertFalse(removed['peak_valid'])

    def test_coherent_sine_normalized_vrms_both_windows(self):
        words = words_for(64, 80)
        expected = 80 * 10 / 1023 / np.sqrt(2)
        for window, gain in (('rect', 1), ('hann', .5)):
            got = reference.reference_spectrum(words, 6250000, 1, window, True)
            self.assertEqual(got['count'], N)
            self.assertAlmostEqual(got['coherent_gain'], gain, places=12)
            self.assertAlmostEqual(got['vrms'][64], expected, delta=.0005)
            self.assertAlmostEqual(got['peak_bin'], 64, delta=.002)
            self.assertAlmostEqual(got['peak_dbv'], 20 * np.log10(got['peak_vrms']), places=12)

    def test_rectangular_parseval_is_not_applied_to_hann(self):
        words = words_for(43.28, 75, second=(155.3, 24))
        samples = ((words >> 16) & 1023).astype(float) * 10 / 1023 - 5
        got = reference.reference_spectrum(words, 10000000, 1, 'rect', False)
        self.assertAlmostEqual(np.sum(got['vrms'] ** 2), np.mean(samples ** 2), places=12)
        hann = reference.reference_spectrum(words, 10000000, 1, 'hann', False)
        self.assertGreater(abs(np.sum(hann['vrms'] ** 2) - np.mean(samples ** 2)), .01)

    def test_noncoherent_hann_peak_and_two_tones(self):
        words = words_for(38.37, 80, second=(177.2, 22))
        got = reference.reference_spectrum(words, 6250000, 1, 'hann', True)
        self.assertTrue(got['peak_valid'])
        self.assertAlmostEqual(got['peak_bin'], 38.37, delta=.018)
        self.assertGreater(got['vrms'][177], .12)

    def test_adjacent_two_tones_never_inflate_line_amplitude(self):
        at = np.arange(N)
        codes = np.rint(512 + 100 * np.sin(2 * np.pi * 37 * at / N + .1)
                        + 99 * np.sin(2 * np.pi * 38 * at / N + .3)).astype(np.uint32)
        words = np.uint32(512) | (codes << 16)
        for window in ('rect', 'hann'):
            got = reference.reference_spectrum(words, 6250000, 1, window, True)
            self.assertTrue(got['peak_valid'])
            # Frequency may be a sub-bin estimate; amplitude is an observed line.
            self.assertAlmostEqual(got['peak_vrms'], np.max(got['vrms'][1:]), places=14)
            self.assertAlmostEqual(got['peak_dbv'], 20 * np.log10(np.max(got['vrms'][1:])), places=12)

    def test_actual_rate_changes_only_frequency_axis(self):
        words = words_for(32)
        slow = reference.reference_spectrum(words, 625, 1)
        fast = reference.reference_spectrum(words, 50000000, 1)
        np.testing.assert_array_equal(slow['vrms'], fast['vrms'])
        self.assertEqual(slow['bin_hz'], 625 / N)
        self.assertAlmostEqual(fast['peak_hz'] / slow['peak_hz'], 50000000 / 625)

    def test_nyquist_endpoint_is_not_sqrt2_scaled(self):
        at = np.arange(N)
        codes = np.where(at % 2, 412, 612).astype(np.uint32)
        words = np.full(N, 512, np.uint32) | (codes << 16)
        for window in ('rect', 'hann'):
            got = reference.reference_spectrum(words, 6250000, 1, window, True)
            self.assertEqual(got['peak_bin'], N / 2)
            self.assertEqual(got['peak_hz'], 3125000)
            self.assertAlmostEqual(got['peak_vrms'], 100 * 10 / 1023, places=12)

    def test_alias_boundary_uses_record_nyquist(self):
        at = np.arange(N)
        codes = np.rint(512 + 90 * np.cos(2 * np.pi * .75 * at)).astype(np.uint32)
        got = reference.reference_spectrum(512 | (codes << 16), 1000000, 1, 'hann', True)
        self.assertAlmostEqual(got['peak_hz'], 250000, delta=.01)
        self.assertLessEqual(got['peak_hz'], got['rate_hz'] / 2)

    def test_channels_and_selected_otr(self):
        words = words_for(32)
        a = reference.reference_spectrum(words, 6250000, 0)
        b = reference.reference_spectrum(words, 6250000, 1)
        self.assertAlmostEqual(a['peak_bin'], 95, delta=.002)
        self.assertAlmostEqual(b['peak_bin'], 32, delta=.002)
        words[0] |= np.uint32(1024)
        self.assertTrue(reference.reference_spectrum(words, 6250000, 1)['peak_valid'])
        selected = reference.reference_spectrum(words, 6250000, 0)
        self.assertTrue(selected['overrange'])
        self.assertFalse(selected['peak_valid'])

    def test_weak_input_and_first_bin_do_not_claim_peak(self):
        self.assertFalse(reference.reference_spectrum(words_for(32, .2), 6250000, 1)['peak_valid'])
        self.assertFalse(reference.reference_spectrum(words_for(1, 70), 6250000, 1)['peak_valid'])
        # A positive DC offsets a Hann window into bin1 when mean is retained.
        got = reference.reference_spectrum(words_for(32, 25, 700), 6250000, 1, 'hann', False)
        self.assertFalse(got['peak_valid'])

    def test_reserved_bits_and_malformed_inputs_rejected(self):
        good = words_for()
        for channel in (-1, 2):
            with self.assertRaises(ValueError): reference.reference_spectrum(good, 6250000, channel)
        for rate in (0, 50000001, 1.2):
            with self.assertRaises(ValueError): reference.reference_spectrum(good, rate, 1)
        for window in ('blackman', 3):
            with self.assertRaises(ValueError): reference.reference_spectrum(good, 6250000, 1, window)
        for words in (good[:-1], good.astype(float), np.full(N, -1), b'bad'):
            with self.assertRaises(ValueError): reference.reference_spectrum(words, 6250000, 1)
        for bit in (11, 15, 27, 31):
            bad = good.copy(); bad[200] |= np.uint32(1 << bit)
            with self.assertRaises(ValueError): reference.reference_spectrum(bad, 6250000, 1)

    def test_full_bins_comparison_rejects_corruption_and_wrong_metadata(self):
        words = words_for(32.2)
        result = reference.reference_spectrum(words, 6250000, 1)
        bins = result['vrms'].astype('<f4').tobytes()
        metadata = {key: value for key, value in result.items() if key != 'vrms'}
        report = reference.compare_spectrum(words, bins, metadata)
        self.assertEqual(report['bins'], 2049)
        corrupted = np.frombuffer(bins, '<f4').copy(); corrupted[32] *= 1.1
        with self.assertRaises(AssertionError): reference.compare_spectrum(words, corrupted, metadata)
        wrong = dict(metadata, peak_hz=result['peak_hz'] + 100)
        with self.assertRaises(AssertionError): reference.compare_spectrum(words, bins, wrong)
        with self.assertRaises(ValueError): reference.compare_spectrum(words, bins[:-1], metadata)
        negative = np.frombuffer(bins, '<f4').copy(); negative[900] = -1
        with self.assertRaises(ValueError): reference.compare_spectrum(words, negative, metadata)

    def test_cli_binary_comparison(self):
        words = words_for(29.4)
        result = reference.reference_spectrum(words, 6250000, 1)
        EVIDENCE.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=EVIDENCE) as name:
            folder = Path(name)
            raw, bins, meta, output = (folder / v for v in ('source.bin', 'bins.bin', 'meta.json', 'report.json'))
            raw.write_bytes(words.astype('<u4').tobytes())
            bins.write_bytes(result['vrms'].astype('<f4').tobytes())
            meta.write_text(json.dumps({k:v for k,v in result.items() if k != 'vrms'}))
            run = subprocess.run([sys.executable, '-B', str(ROOT / 'scripts/spectrum_reference.py'),
                str(raw), '--bins', str(bins), '--metadata', str(meta), '--output', str(output)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertEqual(json.loads(output.read_text())['bins'], 2049)


def cross_check(executable):
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    cases = [('coherent', words_for(32, 80)), ('off-bin', words_for(38.37, 80)),
             ('two-tone', words_for(38.37, 70, second=(177.2, 35))),
             ('low', words_for(32, .2)), ('dc', np.full(N, 512 | (700 << 16), np.uint32))]
    cases.extend((f'boundary-{at}', words_for(at, 70)) for at in (1, 2, 2046, 2047))
    nyq = np.where(np.arange(N) % 2, 412, 612).astype(np.uint32)
    cases.append(('nyquist', np.uint32(512) | (nyq << 16)))
    cases.append(('alias', words_for(3072, 70)))
    selected_otr = words_for(32, 70); selected_otr[100] |= np.uint32(1024 << 16)
    cases.append(('selected-otr', selected_otr))
    at = np.arange(N)
    adjacent = np.rint(512 + 100 * np.sin(2 * np.pi * 37 * at / N + .1)
                       + 99 * np.sin(2 * np.pi * 38 * at / N + .3)).astype(np.uint32)
    cases.append(('adjacent-two-tone', np.uint32(512) | (adjacent << 16)))
    reports = []
    for name, words in cases:
        raw = EVIDENCE / (name + '.raw'); raw.write_bytes(words.astype('<u4').tobytes())
        for window in ('rect', 'hann'):
            for mean in (0, 1):
                for channel in (0, 1):
                    rate = 6250000 if channel else 50000000
                    command = [str(executable), str(raw), window, str(mean), str(channel), str(rate)]
                    proc = subprocess.run(command, capture_output=True, text=True)
                    if proc.returncode:
                        raise AssertionError((command, proc.returncode, proc.stderr))
                    result = json.loads(proc.stdout)
                    bins = result.pop('vrms')
                    report = reference.compare_spectrum(words, np.asarray(bins), result)
                    reports.append(dict(case=name, window=window, mean=mean, channel=channel, **report))
    (EVIDENCE / 'core-cross-check.json').write_text(json.dumps(reports, indent=2) + '\n')
    print('SPECTRUM_NUMPY_CROSS_PASS', len(reports), 'cases;', len(reports) * 2049, 'bins')


if __name__ == '__main__':
    if len(sys.argv) > 1 and Path(sys.argv[1]).is_file():
        cross_check(Path(sys.argv[1]));sys.exit(0)
    unittest.main()
