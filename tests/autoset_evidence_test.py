"""Independent planning oracle covers current signal, offset and narrow pulses."""
from pathlib import Path
import math
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from audit_autoset_evidence import make_plan


class Oracle(unittest.TestCase):
    def test_sine_and_pulse(self):
        words=[504|(round(563+51*math.sin(2*math.pi*50000*i/50000000))<<16) for i in range(4096)]
        p=make_plan(words,50000000,50000)
        self.assertEqual(p['time'],6)
        self.assertEqual(p['rate'],6250000)
        self.assertEqual(p['scales'][1],1)
        pulse=[504|((614 if i%100<10 else 512)<<16) for i in range(4096)]
        p=make_plan(pulse,50000000,50000)
        self.assertEqual(p['scales'][1],2)
        for frequency in (0,1,float('nan'),float('inf'),20000000):
            with self.subTest(frequency=frequency),self.assertRaises(AssertionError):make_plan(words,50000000,frequency)
        words[100]|=1<<26
        with self.assertRaises(AssertionError):make_plan(words,50000000,50000)


if __name__=='__main__':unittest.main()
