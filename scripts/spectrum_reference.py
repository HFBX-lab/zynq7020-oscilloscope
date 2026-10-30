"""Independent NumPy reference for one 4096-point ADC line-amplitude spectrum.

This uses NumPy's rfft, not the firmware radix-2 implementation. Input voltage
units match the existing nominal ADC mapping. The output is Vrms per spectral
line after coherent-gain correction, not PSD or integrated noise power.
"""
import argparse
import hashlib
import json
from pathlib import Path
import zlib

import numpy as np

N = 4096
BINS = N // 2 + 1
LSB_V = 10.0 / 1023.0
PEAK_FLOOR_VRMS = 2 * LSB_V


def _require(condition, message):
    if not condition:
        raise ValueError(message)


def _boolean(value):
    _require(value in (False, True, 0, 1, '0', '1'), 'invalid Boolean metadata')
    return value in (True, 1, '1')


def _window(value):
    _require(value in (0, 1, 'rect', 'hann', '0', '1'), 'unsupported FFT window')
    return int(value in (1, 'hann', '1'))


def _words(value):
    if isinstance(value, (bytes, bytearray, memoryview)):
        _require(len(value) == N * 4, 'source raw must contain exactly 16384 bytes')
        words = np.frombuffer(value, dtype='<u4')
    else:
        words = np.asarray(value)
        _require(words.dtype.kind in 'ui', 'ADC words must be unsigned/integer values')
        _require(words.shape == (N,), 'source raw must contain exactly 4096 words')
        _require(np.all(words >= 0) and np.all(words <= 0xFFFFFFFF), 'ADC word out of uint32 range')
        words = words.astype(np.uint32, copy=False)
    _require(words.shape == (N,), 'source raw must contain exactly 4096 words')
    _require(not np.any(words & np.uint32(0xF800F800)), 'ADC source contains reserved bits')
    return words


def reference_spectrum(words, rate_hz, channel, window='hann', remove_mean=True):
    """Return complete reference metadata and all 2049 one-sided Vrms bins.

    Channel is zero based. Hann is periodic. Peak search intentionally includes
    bin1 then invalidates a maximum below bin2: retained DC's Hann main lobe may
    therefore prevent an AC-peak claim when mean removal is off.
    """
    words = _words(words)
    _require(isinstance(rate_hz, (int, np.integer)) and 1 <= rate_hz <= 50000000, 'invalid record sample rate')
    _require(isinstance(channel, (int, np.integer)) and channel in (0, 1), 'invalid zero-based channel')
    kind = _window(window)
    mean_on = _boolean(remove_mean)
    codes = (words >> (16 * channel)) & np.uint32(1023)
    voltage = codes.astype(np.float64) * LSB_V - 5.0
    mean = float(np.mean(voltage))
    samples = voltage - mean if mean_on else voltage
    weights = np.ones(N) if kind == 0 else .5 - .5 * np.cos(2 * np.pi * np.arange(N) / N)
    transform = np.fft.rfft(samples * weights)
    rms = np.abs(transform) / np.sum(weights)
    rms[1:-1] *= np.sqrt(2)
    overrange = bool(np.any(words & np.uint32(1024 << (16 * channel))))
    bin_hz = rate_hz / N
    result = dict(valid=True, overrange=overrange, peak_valid=False,
        remove_mean=mean_on, count=N, rate_hz=int(rate_hz), channel=int(channel), window=kind,
        bin_hz=bin_hz, mean_v=mean, coherent_gain=float(np.mean(weights)),
        peak_bin=0.0, peak_hz=0.0, peak_vrms=0.0, peak_dbv=-120.0, vrms=rms)
    peak = int(np.argmax(rms[1:])) + 1
    if peak < 2 or overrange or rms[peak] < PEAK_FLOOR_VRMS:
        return result

    estimate = float(peak)
    if 2 <= peak <= 2046 and rms[peak - 1] > 0 and rms[peak + 1] > 0:
        a, b, c = np.log(rms[peak - 1:peak + 2])
        curvature = a - 2 * b + c
        if curvature < 0:
            displacement = float(np.clip(.5 * (a - c) / curvature, -.5, .5))
            estimate += displacement
    # Only frequency is interpolated. A log-parabola vertex can grossly inflate
    # unresolved adjacent tones, so report the actual strongest spectral line.
    amplitude = float(rms[peak])
    result.update(peak_valid=True, peak_bin=estimate, peak_hz=estimate * bin_hz,
                  peak_vrms=amplitude, peak_dbv=float(20 * np.log10(amplitude)))
    return result


def compare_spectrum(words, bins, metadata, *, absolute_tolerance=2e-6, relative_tolerance=3e-4):
    """Verify every exported bin and complete metadata against an rfft result.

    Metadata uses ScopeSpectrumResult names and zero-based channel/window. CRC,
    actual hardware rate and snapshot ownership are bound by the board auditor.
    """
    words = _words(words)
    if isinstance(bins, (bytes, bytearray, memoryview)):
        _require(len(bins) == BINS * 4, 'spectrum output must contain exactly 8196 bytes')
        values = np.frombuffer(bins, dtype='<f4').astype(np.float64)
    else:
        values = np.asarray(bins, dtype=np.float64)
    _require(values.shape == (BINS,), 'spectrum output must contain exactly 2049 bins')
    _require(np.all(np.isfinite(values)) and np.all(values >= 0), 'invalid nonfinite/negative spectral amplitude')
    for key in ('rate_hz', 'channel', 'window', 'remove_mean'):
        _require(key in metadata, 'missing spectrum metadata: ' + key)
    expected = reference_spectrum(words, int(metadata['rate_hz']), int(metadata['channel']),
                                  metadata['window'], metadata['remove_mean'])
    errors = np.abs(values - expected['vrms'])
    limits = absolute_tolerance + relative_tolerance * expected['vrms']
    assert np.all(errors <= limits), ('FFT bin mismatch', int(np.argmax(errors / limits)), float(np.max(errors)))
    for key in ('valid', 'overrange', 'peak_valid', 'remove_mean'):
        assert key in metadata and _boolean(metadata[key]) == expected[key], ('FFT Boolean metadata mismatch', key)
    for key in ('count', 'rate_hz', 'channel', 'window'):
        assert key in metadata and int(metadata[key]) == expected[key], ('FFT integer metadata mismatch', key)
    for key, tolerance in (('bin_hz', 1e-6), ('mean_v', 2e-6), ('coherent_gain', 2e-6)):
        assert key in metadata and abs(float(metadata[key]) - expected[key]) <= tolerance, ('FFT normalization metadata mismatch', key)
    if expected['peak_valid']:
        for key, tolerance in (('peak_bin', .002), ('peak_hz', .002 * expected['bin_hz']),
                                ('peak_vrms', max(2e-6, 3e-4 * expected['peak_vrms'])), ('peak_dbv', .002)):
            assert key in metadata and abs(float(metadata[key]) - expected[key]) <= tolerance, ('FFT peak mismatch', key, metadata.get(key), expected[key])
        assert abs(float(metadata['peak_vrms']) - float(np.max(values[1:]))) <= 2e-6, 'Peak amplitude must be an actual spectral line'
    else:
        for key in ('peak_bin', 'peak_hz', 'peak_vrms', 'peak_dbv'):
            assert key in metadata and abs(float(metadata[key]) - expected[key]) < 1e-6, ('Invalid peak must have sentinel values', key)
    raw = words.astype('<u4', copy=False).tobytes()
    return dict(state='verified-numpy-independent-spectrum', bins=BINS,
        max_absolute_error_vrms=float(np.max(errors)),
        max_error_limit_ratio=float(np.max(errors / limits)),
        source_crc32=f'{zlib.crc32(raw):08X}', source_sha256=hashlib.sha256(raw).hexdigest().upper(),
        peak_valid=expected['peak_valid'], peak_hz=expected['peak_hz'],
        peak_vrms=expected['peak_vrms'], peak_dbv=expected['peak_dbv'],
        bin_hz=expected['bin_hz'], coherent_gain=expected['coherent_gain'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('raw', type=Path)
    parser.add_argument('--bins', type=Path)
    parser.add_argument('--metadata', type=Path)
    parser.add_argument('--rate', type=int, default=6250000)
    parser.add_argument('--channel', type=int, choices=(0, 1), default=1)
    parser.add_argument('--window', choices=('rect', 'hann'), default='hann')
    parser.add_argument('--keep-mean', action='store_true')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    try:
        raw = args.raw.read_bytes()
        if args.bins:
            _require(args.metadata is not None, '--bins requires --metadata')
            metadata = json.loads(args.metadata.read_text(encoding='utf-8-sig'))
            result = compare_spectrum(raw, args.bins.read_bytes(), metadata)
        else:
            result = reference_spectrum(raw, args.rate, args.channel, args.window, not args.keep_mean)
            result['vrms'] = result['vrms'].tolist()
        text = json.dumps(result, indent=2, allow_nan=False) + '\n'
        if args.output:
            args.output.write_text(text, encoding='utf-8')
        else:
            print(text, end='')
    except (OSError, ValueError, AssertionError, KeyError) as error:
        parser.exit(1, f'Independent spectrum validation failed: {error}\n')


if __name__ == '__main__':
    main()
