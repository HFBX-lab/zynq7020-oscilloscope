"""A single digital carry/capture error must fail despite a good global fit."""
import math
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from audit_highspeed_50m_evidence import known_tone_fit


class CaptureQuality(unittest.TestCase):
    def test_clean_sine_and_single_corrupt_sample(self):
        for rate in (50000000, 6250000, 625000):
            with self.subTest(rate=rate):
                codes = [round(565+51*math.sin(2*math.pi*50000*i/rate)) for i in range(4096)]
                fit = known_tone_fit(codes, rate)
                self.assertAlmostEqual(fit['frequency_hz'], 50000, delta=2)
                for corrupt in (1023, codes[100] ^ 64):
                    broken = codes.copy()
                    broken[100] = corrupt
                    with self.assertRaises(AssertionError):
                        known_tone_fit(broken, rate)


if __name__ == '__main__':
    unittest.main()
