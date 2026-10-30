"""Validate ADC module bit order and adapter-to-board numbering independently."""
from pathlib import Path
import hashlib
import json
import re
import unittest
import fitz

ROOT = Path(__file__).resolve().parents[1]


class PinmapTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = json.loads((ROOT/'hardware/adc/pinmap.json').read_text(encoding='utf-8'))

    def test_module_bit_order_and_nc(self):
        for bank in self.data['banks'].values():
            ports={x['port']:x for x in bank['signals']}
            self.assertEqual(len(ports),26)
            for channel,start in [(1,6),(2,20)]:
                expected=[start+i if i%2==0 else start+i-2 for i in range(10)]
                self.assertEqual([ports[f'ad_data_{channel}[{i}]']['module_pin'] for i in range(10)],expected)
                for name,pin in [('oe',15 if channel==1 else 29),('otr',16 if channel==1 else 30),('clk',18 if channel==1 else 32)]:
                    self.assertEqual(ports[f'ad_{name}_{channel}']['module_pin'],pin)
            self.assertEqual(len({x['package_pin'] for x in ports.values()}),26)
            self.assertFalse({1,2,3,4,17,31,33,34,35,36,37,38,39,40} & {x['module_pin'] for x in ports.values()})

    def test_specific_corrected_pins(self):
        a={x['port']:x['package_pin'] for x in self.data['banks']['33']['signals']}
        b={x['port']:x['package_pin'] for x in self.data['banks']['35']['signals']}
        self.assertEqual((a['ad_data_1[0]'],a['ad_clk_1'],a['ad_clk_2']),('T22','Y19','AB17'))
        self.assertEqual((b['ad_data_1[0]'],b['ad_clk_1'],b['ad_clk_2']),('G15','D20','C17'))

    def test_base_connector_numbering(self):
        for key,bank in self.data['banks'].items():
            self.assertEqual(bank['base_header'],'J6' if key=='33' else 'J5')
            for x in bank['signals']:
                self.assertEqual(x['adapter_pin'],x['module_pin'])
                self.assertEqual(x['base_pin'],x['module_pin']-4 if key=='33' else 45-x['module_pin'])

    def test_xdc_and_rgb_disjoint(self):
        lcd=set(re.findall(r'PACKAGE_PIN\s+(\w+)',(ROOT/'hardware/ps/panel.xdc').read_text()))
        for key,bank in self.data['banks'].items():
            xdc=(ROOT/f'hardware/adc/constraints/adc_bank{key}.xdc').read_text()
            actual=dict(re.findall(r'PACKAGE_PIN (\w+)\} \[get_ports \{([^}]+)\}',xdc))
            expected={x['package_pin']:x['port'] for x in bank['signals']}
            self.assertEqual(actual,expected)
            self.assertFalse(lcd & expected.keys())

    def test_source_and_schematic_pin_numbers(self):
        source=self.data['board_source']
        pdf=Path(source['path'])
        self.assertEqual(hashlib.sha256(pdf.read_bytes()).hexdigest(),source['sha256'])
        image=ROOT/self.data['user_image']['snapshot']
        self.assertEqual(hashlib.sha256(image.read_bytes()).hexdigest(),self.data['user_image']['sha256'])
        with fitz.open(pdf) as d:
            page=d[9]
            words=[(fitz.Rect(w[:4])*page.rotation_matrix,w[4]) for w in page.get_text('words')]
        # Use external connector pin numbers and the drawn wires' common Y level.
        for key,bank in self.data['banks'].items():
            for x in bank['signals']:
                net=[r for r,t in words if t==x['net'] and (r.y0>300 if key=='33' else r.y0<300) and r.x0<400]
                self.assertEqual(len(net),1,(key,x))
                # Pin number is inside connector; wire labels are vertically
                # offset slightly in this PDF. Compare text baseline positions.
                candidates=[(abs(r.y0-net[0].y0),int(t)) for r,t in words if t.isdigit()
                            and (r.y0>300 if key=='33' else r.y0<300)
                            and (140<r.x0<160 if x['base_pin']%2==0 else 245<r.x0<265)]
                self.assertLess(min(candidates)[0],2.0,(key,x))
                self.assertEqual(min(candidates)[1],x['base_pin'],(key,x))


if __name__=='__main__':unittest.main()
