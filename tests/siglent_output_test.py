"""Output-only controller tests: injected byte streams, never real sockets."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
SCRIPT = ROOT / 'scripts' / 'siglent_output.py'
if SCRIPT.exists():
    spec = importlib.util.spec_from_file_location('siglent_output', SCRIPT)
    output = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(output)
else:
    output = None
spec = importlib.util.spec_from_file_location('sdg_test_wire', ROOT / 'tests' / 'siglent_sdg_test.py')
wire_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(wire_module)
Wire = wire_module.Wire
SQUARE = 'C2:BSWV WVTP,SQUARE,FRQ,50000HZ,AMP,1V,OFST,0V,DUTY,50,PHSE,0'
OFF = 'C2:OUTP OFF,LOAD,HZ,PLRT,NOR'
ON = 'C2:OUTP ON,LOAD,HZ,PLRT,NOR'


class OutputTests(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(output, 'Output-only controller is not implemented')

    def client(self, wire=None):
        wire = wire or Wire(bswv=SQUARE, outp=OFF)
        client = output.SDGClient('offline.invalid', socket_factory=lambda address, timeout: wire)
        client.connect()
        self.addCleanup(client.close)
        return client, wire

    def writes(self, wire):
        return [cmd for cmd in wire.commands if not cmd.endswith('?')]

    def test_square_on_only_sends_complete_output_and_preserves_all_raw_waveform_fields(self):
        c, w = self.client()
        result = output.set_output(c, True)
        self.assertTrue(result['output_on'])
        self.assertEqual(result['raw_bswv'], SQUARE)
        self.assertEqual(self.writes(w), [ON])
        reads = ['*IDN?', 'C2:BSWV?', 'C2:OUTP?', 'C2:MDWV?', 'C2:SWWV?', 'C2:BTWV?', 'COUP?']
        self.assertEqual(w.commands, reads + [ON, '*OPC?'] + reads)

    def test_square_off_can_restore_original_output_without_changing_waveform(self):
        c, w = self.client()
        original = c.snapshot()
        output.set_output(c, True)
        restored = output.set_output(c, original['output_on'])
        self.assertEqual((restored['raw_bswv'], restored['raw_outp']), (SQUARE, OFF))
        self.assertEqual(self.writes(w), [ON, OFF])

    def test_sine_and_dc_are_supported_without_waveform_writes(self):
        for bswv in (wire_module.SINE, 'C2:BSWV WVTP,DC,OFST,-2V'):
            with self.subTest(bswv=bswv):
                c, w = self.client(Wire(bswv=bswv, outp=OFF))
                self.assertEqual(output.set_output(c, True)['raw_bswv'], bswv)
                self.assertEqual(self.writes(w), [ON])

    def test_non_boolean_enabled_is_rejected_before_any_io(self):
        for enabled in (None, 0, 1, 'OFF'):
            c, w = self.client()
            with self.assertRaises(ValueError): output.set_output(c, enabled)
            self.assertEqual(w.commands, [])

    def test_square_inclusive_frequency_amplitude_offset_and_duty_limits(self):
        for freq, amp, offset, duty in ((1, '.2', '2.4', 5), (10000000, 4, '-.5', 95)):
            bswv = f'C2:BSWV WVTP,SQUARE,FRQ,{freq}HZ,AMP,{amp}V,OFST,{offset}V,DUTY,{duty}'
            c, w = self.client(Wire(bswv=bswv, outp=OFF))
            self.assertEqual(output.set_output(c, True)['raw_bswv'], bswv)

    def test_missing_bad_nonfinite_or_out_of_range_square_duty_refuses_all_writes(self):
        for bad in (SQUARE.replace(',DUTY,50', ''), *(SQUARE.replace('DUTY,50', 'DUTY,' + value)
                     for value in ('4.99', '95.01', 'nan', 'inf', '-inf', '50%', 'bad'))):
            with self.subTest(bad=bad):
                c, w = self.client(Wire(bswv=bad, outp=OFF))
                with self.assertRaises(output.SDGError): output.set_output(c, True)
                self.assertEqual(self.writes(w), [])

    def test_unsafe_waveform_or_output_refuses_all_writes_including_requested_off(self):
        for bswv in (SQUARE.replace('50000HZ', '0HZ'), SQUARE.replace('50000HZ', '10000001HZ'),
                     SQUARE.replace('AMP,1V', 'AMP,0.19V'), SQUARE.replace('AMP,1V', 'AMP,4.01V'),
                     SQUARE.replace('OFST,0V', 'OFST,2.1V'), SQUARE.replace('SQUARE', 'RAMP'),
                     SQUARE.replace(',FRQ,50000HZ', ''), SQUARE.replace(',AMP,1V', ''),
                     SQUARE.replace(',OFST,0V', ''), 'C2:BSWV WVTP,DC,OFST,2.1V'):
            c, w = self.client(Wire(bswv=bswv, outp=ON))
            with self.assertRaises(output.SDGError): output.set_output(c, False)
            self.assertEqual(self.writes(w), [])
        for outp in (ON.replace('HZ', '50'), ON.replace('NOR', 'INVT')):
            c, w = self.client(Wire(bswv=SQUARE, outp=outp))
            with self.assertRaises(output.SDGError): output.set_output(c, False)
            self.assertEqual(self.writes(w), [])

    def test_wrong_identity_refuses_channel_access_or_write(self):
        for idn in ('Other,SDG2122X,S,V', 'Siglent,SDG2122X Plus,S,V'):
            c, w = self.client(Wire(bswv=SQUARE, outp=OFF, identity=idn))
            with self.assertRaises(output.SDGError): output.set_output(c, True)
            self.assertEqual(w.commands, ['*IDN?'])

    def test_active_or_unknown_modes_and_coupling_refuse_writes(self):
        for cmd in ('C2:MDWV?', 'C2:SWWV?', 'C2:BTWV?'):
            for state in ('ON', 'UNKNOWN'):
                c, w = self.client()
                w.override[cmd] = cmd[:-1] + ' STATE,' + state
                with self.assertRaises(output.SDGError): output.set_output(c, True)
                self.assertEqual(self.writes(w), [])
        base = 'COUP TRACE,OFF,FCOUP,OFF,PCOUP,OFF,ACOUP,OFF,TRDUCH,OFF'
        for key in ('TRACE', 'FCOUP', 'PCOUP', 'ACOUP', 'TRDUCH'):
            c, w = self.client()
            w.override['COUP?'] = base.replace(key + ',OFF', key + ',ON')
            with self.assertRaises(output.SDGError): output.set_output(c, True)
            self.assertEqual(self.writes(w), [])

    def test_optional_trduch_off_is_accepted(self):
        c, w = self.client()
        w.override['COUP?'] = 'COUP TRACE,OFF,FCOUP,OFF,PCOUP,OFF,ACOUP,OFF,TRDUCH,OFF'
        self.assertTrue(output.set_output(c, True)['output_on'])

    def test_failed_output_and_opc_readback_attempt_off(self):
        for cmd, reply in ((ON, None), ('*OPC?', '0'), ('*OPC?', None)):
            c, w = self.client()
            w.override[cmd] = reply
            with self.assertRaises(output.SDGError): output.set_output(c, True)
            self.assertEqual(self.writes(w), [ON, OFF])

    def test_changed_waveform_identity_load_or_modes_after_write_attempt_off(self):
        for failure in ('waveform', 'identity', 'load', 'polarity', 'mode', 'coupling'):
            class ChangedWire(Wire):
                def sendall(self, data):
                    super().sendall(data)
                    if data == (ON + '\n').encode():
                        if failure == 'waveform': self.bswv += ',RISE,1e-8S'
                        elif failure == 'identity': self.identity = self.identity.replace('TESTSERIAL', 'OTHER')
                        elif failure == 'load': self.outp = self.outp.replace('HZ', '50')
                        elif failure == 'polarity': self.outp = self.outp.replace('NOR', 'INVT')
                        elif failure == 'mode': self.override['C2:SWWV?'] = 'C2:SWWV STATE,ON'
                        else: self.override['COUP?'] = 'COUP TRACE,ON,FCOUP,OFF,PCOUP,OFF,ACOUP,OFF'
            c, w = self.client(ChangedWire(bswv=SQUARE, outp=OFF))
            with self.subTest(failure=failure), self.assertRaises(output.SDGError): output.set_output(c, True)
            self.assertEqual(self.writes(w), [ON, OFF])

    def test_keyboard_interrupt_or_system_exit_after_write_preserved_and_attempts_off(self):
        for interruption in (KeyboardInterrupt(), SystemExit(9)):
            class InterruptWire(Wire):
                def sendall(self, data):
                    if data == b'*OPC?\n': raise interruption
                    return super().sendall(data)
            c, w = self.client(InterruptWire(bswv=SQUARE, outp=OFF))
            with self.assertRaises(type(interruption)) as caught: output.set_output(c, True)
            self.assertIs(caught.exception, interruption)
            self.assertEqual(self.writes(w), [ON, OFF])

    def test_interruption_during_output_send_attempts_off_even_if_cleanup_also_fails(self):
        interruption = KeyboardInterrupt()
        class SendInterruptedWire(Wire):
            def sendall(self, data):
                if data == (ON + '\n').encode():
                    self.commands.append(ON)
                    raise interruption
                if data == (OFF + '\n').encode():
                    self.commands.append(OFF)
                    raise OSError('scripted cleanup failure')
                return super().sendall(data)
        c, w = self.client(SendInterruptedWire(bswv=SQUARE, outp=OFF))
        with self.assertRaises(KeyboardInterrupt) as caught: output.set_output(c, True)
        self.assertIs(caught.exception, interruption)
        self.assertEqual(self.writes(w), [ON, OFF])

    def test_preflight_interrupt_refuses_all_writes(self):
        class PreflightInterruptedWire(Wire):
            def sendall(self, data):
                if data == b'C2:MDWV?\n': raise KeyboardInterrupt()
                return super().sendall(data)
        c, w = self.client(PreflightInterruptedWire(bswv=SQUARE, outp=OFF))
        with self.assertRaises(KeyboardInterrupt): output.set_output(c, False)
        self.assertEqual(self.writes(w), [])

    def test_cli_default_is_readonly_and_optional_json_file_matches_stdout(self):
        c, w = self.client()
        artifacts = Path('D:/FPGA_Project/SP2_Storage_Build/source-output-tests')
        artifacts.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=artifacts) as folder:
            target = Path(folder) / 'snapshot.json'
            stdout = io.StringIO()
            with mock.patch.object(output, 'SDGClient', return_value=c), contextlib.redirect_stdout(stdout):
                self.assertEqual(output.main(['offline.invalid', '--output', str(target)]), 0)
            self.assertEqual(json.loads(target.read_text(encoding='utf-8')), json.loads(stdout.getvalue()))
            self.assertEqual(self.writes(w), [])

    def test_cli_explicit_on_off_and_mutual_exclusion(self):
        for flag, command in (('--on', ON), ('--off', OFF)):
            c, w = self.client()
            with mock.patch.object(output, 'SDGClient', return_value=c), contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(output.main(['offline.invalid', flag]), 0)
            self.assertEqual(self.writes(w), [command])
        with mock.patch.object(output, 'SDGClient') as factory, contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit): output.main(['offline.invalid', '--on', '--off'])
            factory.assert_not_called()


if __name__ == '__main__': unittest.main(verbosity=2)
