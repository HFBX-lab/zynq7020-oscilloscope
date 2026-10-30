"""Analytic plus independent event-list/C cross-checks; all outputs stay on D:."""
from pathlib import Path
import ctypes as C
import math
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from timing_reference import timing_reference, compare_timing

root = Path(__file__).resolve().parents[1]
folder = Path('D:/FPGA_Project/SP2_Timing_Build/reference')
folder.mkdir(parents=True, exist_ok=True)
library = folder / 'timing.dll'
subprocess.run(['D:/msys64/ucrt64/bin/gcc.exe', '-std=c11', '-shared', '-O2',
                '-Wall', '-Wextra', '-Werror', '-Isrc', 'src/scope_measurement.c',
                '-lm', '-o', str(library)], cwd=root, check=True)


class Timing(C.Structure):
    _fields_ = [(x, C.c_bool) for x in ('pulse_valid', 'rise_valid', 'fall_valid')] + [
        ('cycles', C.c_uint)] + [(x, C.c_double) for x in (
        'duty_percent', 'positive_s', 'negative_s', 'period_s', 'rise_s', 'fall_s',
        'low_v', 'mid_v', 'high_v')]


lib = C.CDLL(str(library))
function = lib.scope_measurement_timing
function.argtypes = [C.POINTER(C.c_uint32), C.c_uint, C.c_uint, C.c_uint, C.POINTER(Timing)]
function.restype = C.c_int
checks = 0


def check(words, rate, ch):
    global checks
    out = Timing()
    array = (C.c_uint32 * len(words))(*words)
    assert function(array, len(words), rate, ch, C.byref(out)) == 0
    measured = {}
    for key, _ in Timing._fields_:
        value = getattr(out, key)
        if key.endswith('_s'):
            key = key[:-2] + '_us'
            value *= 1e6
        measured[key] = value
    result = compare_timing(words, rate, ch, measured)
    checks += 1
    return result


for period in (16, 32, 100, 125, 400):
    for phase in (0, .25, 3.4, 7.9):
        for duty in (.2, .5, .8):
            words = [(700 if (i + phase) % period < period * duty else 300) |
                     ((850 if (i + phase + period * .2) % period < period * (1 - duty)
                       else 650) << 16) for i in range(4096)]
            for rate in (625, 6250000, 50000000):
                for ch in (0, 1):
                    result = check(words, rate, ch)
                    assert result['pulse_valid']
                    assert not result['rise_valid'] and not result['fall_valid']
                    expected = (duty if ch == 0 else 1 - duty) * 100
                    assert abs(result['duty_percent'] - expected) <= 100 / period + 1e-8
for period in (16, 32, 100, 400, 1000):
    for phase in (0, .25, 3.4, period * .8):
        words = [round(520 + 120 * math.sin(2 * math.pi * (i + phase) / period)) |
                 (600 << 16) for i in range(4096)]
        result = check(words, 50000000, 0)
        assert result['pulse_valid']
        assert abs(result['duty_percent'] - 50) < .1
        check(words, 6250000, 1)
        if result['rise_valid']:
            expected = math.asin(.8) / math.pi * period / 50000000 * 1e6
            assert abs(result['rise_us'] - expected) < .015
for count in (1, 2, 7, 12, 60, 400, 4096):
    check([512 | (550 << 16)] * count, 50000000, 0)
for flag in (0, 1 << 10, 1 << 26):
    words = [(700 if i % 100 < 20 else 300) | (550 << 16) for i in range(4096)]
    words[123] |= flag
    check(words, 6250000, 0)
    check(words, 6250000, 1)
print('TIMING_REFERENCE_PASS:', checks, 'independent comparisons')
