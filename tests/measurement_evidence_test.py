"""Reject plausible but wrong RMS/period metadata, independently of UI code."""
from pathlib import Path
import copy
import math
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from audit_measurement_evidence import check_values


class Evidence(unittest.TestCase):
    def test_values_and_corruption(self):
        words=[500|((560+(40 if i%2 else -40))<<16) for i in range(4096)]
        m=dict(context=dict(rate='625000',count='4096',channel='2',panel='1'),channels=[])
        for ch in range(2):
            v=[((w>>(16*ch))&1023)*10/1023-5 for w in words]
            mean=math.fsum(v)/len(v)
            m['channels'].append(dict(ch=str(ch+1),valid='1',overrange='0',min_v=str(min(v)),max_v=str(max(v)),mean_v=str(mean),
                vpp_v=str(max(v)-min(v)),rms_total_v=str(math.sqrt(math.fsum(x*x for x in v)/len(v))),
                rms_ac_v=str(math.sqrt(math.fsum((x-mean)**2 for x in v)/len(v))),frequency_valid='0',frequency_hz='0',period_us='0'))
        check_values(words,625000,m)
        for field,value in [('rms_ac_v','0.1'),('mean_v','NaN'),('rms_total_v','Infinity'),('overrange','1'),('period_us','20'),('frequency_valid','1')]:
            bad=copy.deepcopy(m);bad['channels'][1][field]=value
            with self.subTest(field=field),self.assertRaises(AssertionError):check_values(words,625000,bad)
        for field,value in [('rate','50000000'),('count','4095')]:
            bad=copy.deepcopy(m);bad['context'][field]=value
            with self.assertRaises(AssertionError):check_values(words,625000,bad)


if __name__=='__main__':unittest.main()
