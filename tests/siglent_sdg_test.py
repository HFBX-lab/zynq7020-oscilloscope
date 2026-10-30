"""Offline SDG client tests. These tests never contact an instrument."""
import importlib.util
import json
from pathlib import Path
import socket
import subprocess
import sys
import threading
import unittest

sys.dont_write_bytecode = True
SCRIPT = Path(__file__).resolve().parents[1] / 'scripts' / 'siglent_sdg.py'
if SCRIPT.exists():
    spec = importlib.util.spec_from_file_location('siglent_sdg', SCRIPT)
    sdg = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(sdg)
else:
    sdg = None

IDN = 'Siglent Technologies,SDG2122X,TESTSERIAL,1.0'
SINE = 'C2:BSWV WVTP,SINE,FRQ,50000HZ,PERI,2e-5S,AMP,1V,AMPVRMS,0.353553V,OFST,0V,HLEV,0.5V,LLEV,-0.5V,PHSE,0'
OUT = 'C2:OUTP ON,LOAD,HZ,PLRT,NOR'


class Wire:
    """A byte-stream peer, deliberately fragmenting every ASCII response."""
    def __init__(self, bswv=SINE, outp=OUT, identity=IDN):
        self.bswv, self.outp, self.identity = bswv, outp, identity
        self.commands, self.pending = [], bytearray()
        self.closed = False
        self.override = {}
        self.bad_readback = False
        self.configured = False
        self.timeout = 2

    def settimeout(self, timeout):
        self.timeout = timeout

    def sendall(self, data):
        assert data.endswith(b'\n') and data.count(b'\n') == 1
        cmd = data[:-1].decode('ascii')
        self.commands.append(cmd)
        reply = self.override.get(cmd)
        if cmd not in self.override:
            if cmd == '*IDN?': reply = self.identity
            elif cmd == 'C2:BSWV?':
                reply = self.bswv
                if self.bad_readback and self.configured:
                    reply = reply.replace('AMP,1V', 'AMP,2V')
            elif cmd == 'C2:OUTP?': reply = self.outp
            elif cmd in ('C2:MDWV?', 'C2:SWWV?', 'C2:BTWV?'):
                reply = cmd[:-1] + ' STATE,OFF'
            elif cmd == 'COUP?': reply = 'COUP TRACE,OFF,FCOUP,OFF,PCOUP,OFF,ACOUP,OFF'
            elif cmd == '*OPC?': reply = '1'
            elif cmd == 'C2:OUTP OFF': self.outp = self.outp.replace(' ON,', ' OFF,')
            elif cmd == 'C2:OUTP ON': self.outp = self.outp.replace(' OFF,', ' ON,')
            elif cmd in ('C2:OUTP OFF,LOAD,HZ,PLRT,NOR', 'C2:OUTP ON,LOAD,HZ,PLRT,NOR'):
                self.outp = cmd
            elif cmd == 'C2:OUTP LOAD,HZ': pass
            elif cmd.startswith('C2:BSWV '):
                tokens = cmd.split(' ', 1)[1].split(',')
                units = {'FRQ': 'HZ', 'AMP': 'V', 'OFST': 'V'}
                for i in range(0, len(tokens), 2):
                    tokens[i + 1] += units.get(tokens[i], '')
                self.bswv = 'C2:BSWV ' + ','.join(tokens)
                self.configured = True
            else: raise AssertionError('Unexpected command: ' + cmd)
        if reply is not None:
            self.pending.extend(reply.encode('ascii') + b'\r\n')

    def recv(self, size):
        if not self.pending: raise socket.timeout('scripted timeout')
        n = min(size, 7, len(self.pending))
        data = bytes(self.pending[:n])
        del self.pending[:n]
        return data

    def close(self): self.closed = True


class ClientTests(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(sdg, 'SDG client implementation does not exist yet')

    def client(self, wire=None):
        wire = wire or Wire()
        client = sdg.SDGClient('offline.invalid', socket_factory=lambda address, timeout: wire)
        client.connect()
        self.addCleanup(client.close)
        return client, wire

    def test_fragmented_readonly_snapshot_and_close(self):
        c, w = self.client()
        s = c.snapshot()
        self.assertEqual((s['vendor'], s['model']), ('Siglent Technologies', 'SDG2122X'))
        self.assertEqual((s['frequency_hz'], s['vpp'], s['offset_v']), (50000, 1, 0))
        self.assertEqual((s['raw_bswv'], s['raw_outp']), (SINE, OUT))
        self.assertEqual(s['bswv']['AMPVRMS'], '0.353553V')
        self.assertEqual(w.commands, ['*IDN?', 'C2:BSWV?', 'C2:OUTP?'])
        c.close()
        self.assertTrue(w.closed)

    def test_wrong_vendor_or_exact_model_refused_without_channel_access(self):
        for identity in ('Other,SDG2122X,S,V', 'Siglent,SDG2122X Plus,S,V', 'Siglent,SDG2082X,S,V'):
            with self.subTest(identity=identity):
                c, w = self.client(Wire(identity=identity))
                with self.assertRaises(sdg.SDGError): c.configure_dc(0)
                self.assertEqual(w.commands, ['*IDN?'])

    def test_parser_units_missing_duplicate_nonfinite_and_wrong_channel(self):
        for bad in (SINE.replace('C2:', 'C1:'), SINE.replace('AMP,1V', 'AMP,1VRMS'),
                    SINE.replace('FRQ,50000HZ', 'FRQ,NaNHZ'), SINE.replace('OFST,0V', 'OFST,InfV'),
                    SINE.replace('AMP,1V,', ''), SINE + ',AMP,1V', SINE.replace('AMP,1V', 'AMP,1')):
            with self.subTest(bad=bad):
                c, w = self.client(Wire(bswv=bad))
                with self.assertRaises(sdg.SDGError): c.snapshot()
        c, _ = self.client(Wire(bswv=SINE.replace('50000HZ', '5e4HZ').replace('AMP,1V', 'AMP,1000MV')))
        self.assertEqual(c.snapshot()['vpp'], 1)

    def test_unsupported_waveform_visible_but_mutation_refused(self):
        c, w = self.client(Wire(bswv='C2:BSWV WVTP,SQUARE,FRQ,1000HZ,AMP,1V,OFST,0V'))
        self.assertEqual(c.snapshot()['waveform'], 'SQUARE')
        with self.assertRaises(sdg.SDGError): c.configure_dc(0)
        self.assertTrue(all(x.endswith('?') for x in w.commands))

    def test_limits_fail_before_any_io(self):
        c, w = self.client()
        for freq, amp, offset in ((0, 1, 0), (10000001, 1, 0), (1, .19, 0), (1, 4.01, 0),
                                  (1, 4, .6), (float('nan'), 1, 0), (1, float('inf'), 0)):
            with self.subTest(values=(freq, amp, offset)):
                with self.assertRaises((ValueError, sdg.SDGError)): c.configure_sine(freq, amp, offset)
        for offset in (-2.01, 2.01, float('nan')):
            with self.assertRaises((ValueError, sdg.SDGError)): c.configure_dc(offset)
        self.assertEqual(w.commands, [])

    def test_inclusive_limits_are_accepted(self):
        c, _ = self.client()
        for freq, vpp, offset in ((1, .2, 2.4), (10000000, 4, -.5)):
            result = c.configure_sine(freq, vpp, offset)
            self.assertEqual((result['frequency_hz'], result['vpp'], result['offset_v']), (freq, vpp, offset))
        for offset in (-2, 2):
            self.assertEqual(c.configure_dc(offset)['offset_v'], offset)

    def test_safe_order_sine_and_dc(self):
        c, w = self.client()
        result = c.configure_sine(50000, 1, 0)
        first_write = next(i for i, cmd in enumerate(w.commands) if not cmd.endswith('?'))
        self.assertEqual(w.commands[:first_write], ['*IDN?', 'C2:BSWV?', 'C2:OUTP?', 'C2:MDWV?', 'C2:SWWV?', 'C2:BTWV?', 'COUP?'])
        self.assertEqual(w.commands[first_write:], ['C2:OUTP OFF,LOAD,HZ,PLRT,NOR', 'C2:OUTP?', 'C2:BSWV WVTP,SINE,FRQ,50000,AMP,1,OFST,0,PHSE,0', '*OPC?', '*IDN?', 'C2:BSWV?', 'C2:OUTP?', 'C2:MDWV?', 'C2:SWWV?', 'C2:BTWV?', 'COUP?', 'C2:OUTP ON,LOAD,HZ,PLRT,NOR', '*IDN?', 'C2:BSWV?', 'C2:OUTP?', 'C2:MDWV?', 'C2:SWWV?', 'C2:BTWV?', 'COUP?'])
        self.assertTrue(result['output_on'])
        c.configure_dc(-1)
        self.assertIn('C2:BSWV WVTP,DC,OFST,-1', w.commands)
        self.assertFalse(any(x.startswith('C1:') or x in ('*RST', '*CLS') for x in w.commands))

    def test_active_or_unknown_modes_and_unsafe_initial_refuse_all_writes(self):
        for cmd, reply in (('C2:MDWV?', 'C2:MDWV STATE,ON'), ('C2:SWWV?', 'C2:SWWV STATE,ON'),
                           ('C2:BTWV?', 'C2:BTWV STATE,ON'), ('C2:MDWV?', 'C1:MDWV STATE,OFF')):
            c, w = self.client()
            w.override[cmd] = reply
            with self.assertRaises(sdg.SDGError): c.configure_dc(0)
            self.assertTrue(all(x.endswith('?') for x in w.commands))
        for outp in ('C2:OUTP ON,LOAD,50,PLRT,NOR', 'C2:OUTP ON,LOAD,HZ,PLRT,INVT'):
            c, w = self.client(Wire(outp=outp))
            with self.assertRaises(sdg.SDGError): c.configure_dc(0)
            self.assertTrue(all(x.endswith('?') for x in w.commands))

    def test_bad_readback_and_operation_complete_leave_off(self):
        for failure in ('readback', 'opc', 'enable'):
            c, w = self.client()
            if failure == 'readback': w.bad_readback = True
            elif failure == 'opc': w.override['*OPC?'] = '0'
            else: w.override['C2:OUTP ON,LOAD,HZ,PLRT,NOR'] = None
            with self.assertRaises(sdg.SDGError): c.configure_sine(50000, 1)
            self.assertEqual(w.commands[-1], 'C2:OUTP OFF,LOAD,HZ,PLRT,NOR')
            self.assertIn(' OFF,', w.outp)

    def test_restore_preserves_original_output_and_phase(self):
        for initial_on in (True, False):
            c, w = self.client(Wire(bswv=SINE.replace('PHSE,0', 'PHSE,33'), outp=OUT if initial_on else OUT.replace(' ON,', ' OFF,')))
            original = c.snapshot()
            c.configure_dc(.5)
            result = c.restore(original)
            self.assertEqual(result['output_on'], initial_on)
            self.assertEqual(result['phase_deg'], 33)
            self.assertEqual(result['frequency_hz'], 50000)
            self.assertEqual(result['raw_bswv'].split(',PHSE,')[-1], '33')
        c, w = self.client()
        unsafe = c.snapshot()
        unsafe['raw_bswv'] = unsafe['raw_bswv'].replace('SINE', 'SQUARE')
        before = len(w.commands)
        with self.assertRaises(sdg.SDGError): c.restore(unsafe)
        self.assertEqual(len(w.commands), before)

    def test_framing_pending_oversize_timeout_ascii_and_injection(self):
        c, w = self.client()
        w.override['*IDN?'] = IDN + '\n' + SINE + '\n' + OUT
        # Every recv uses short fragments; pre-sent lines remain in the receive stream.
        w.override['C2:BSWV?'] = None
        w.override['C2:OUTP?'] = None
        self.assertEqual(c.snapshot()['vpp'], 1)
        for response in ('x' * 4097, None, 'x\x00'):
            c, w = self.client()
            w.override['*IDN?'] = response
            with self.assertRaises(sdg.SDGError): c.snapshot()
        for command in ('*IDN?\nC2:OUTP ON', '*RST', 'C1:BSWV?'):
            c, w = self.client()
            with self.assertRaises((ValueError, sdg.SDGError)): c.query(command)
            self.assertEqual(w.commands, [])

    def test_cli_is_readonly_against_local_tcp_peer(self):
        listener = socket.socket()
        listener.bind(('127.0.0.1', 0))
        listener.listen(1)
        listener.settimeout(5)
        commands, errors = [], []
        def serve():
            try:
                conn, _ = listener.accept()
                with conn:
                    stream = conn.makefile('rb')
                    for response in (IDN, SINE, OUT):
                        commands.append(stream.readline().decode().strip())
                        for chunk in (response[:5], response[5:] + '\n'):
                            conn.sendall(chunk.encode())
                    self.assertEqual(stream.readline(), b'')
            except Exception as exc: errors.append(exc)
        thread = threading.Thread(target=serve)
        thread.start()
        try:
            proc = subprocess.run([sys.executable, str(SCRIPT), '127.0.0.1', '--port', str(listener.getsockname()[1])], capture_output=True, text=True, timeout=10)
            thread.join(5)
            self.assertEqual(proc.returncode, 0, proc.stderr)
            self.assertEqual(json.loads(proc.stdout)['model'], 'SDG2122X')
            self.assertEqual(commands, ['*IDN?', 'C2:BSWV?', 'C2:OUTP?'])
            self.assertFalse(errors)
        finally:
            listener.close()

    def test_ignored_off_cannot_change_waveform(self):
        c, w = self.client()
        w.override['C2:OUTP OFF,LOAD,HZ,PLRT,NOR'] = None
        with self.assertRaises(sdg.SDGError): c.configure_dc(1)
        self.assertFalse(any(cmd.startswith('C2:BSWV ') for cmd in w.commands))

    def test_exact_frame_boundary_and_buffered_lines(self):
        class ByteWire(Wire):
            def recv(self, size):
                data = bytes(self.pending[:1])
                del self.pending[:1]
                return data
        c, w = self.client(ByteWire())
        w.override['*OPC?'] = '1' * 4096
        self.assertEqual(len(c.query('*OPC?')), 4096)
        # Test multiple lines received in one recv, as well as separate packets.
        class CombinedWire(Wire):
            def recv(self, size):
                data = bytes(self.pending[:size])
                del self.pending[:size]
                return data
        c, w = self.client(CombinedWire())
        w.override['*IDN?'] = IDN + '\n' + SINE + '\n' + OUT
        w.override['C2:BSWV?'] = w.override['C2:OUTP?'] = None
        self.assertEqual(c.snapshot()['frequency_hz'], 50000)

    def test_send_failure_poisons_queries_until_reconnect(self):
        class BrokenWire(Wire):
            def sendall(self, data): raise socket.timeout('send timeout')
        c, w = self.client(BrokenWire())
        with self.assertRaises(sdg.SDGError): c.query('*IDN?')
        self.assertTrue(c._desynchronized)

    def test_user_interrupt_during_configuration_attempts_off(self):
        class InterruptWire(Wire):
            def sendall(self, data):
                if data == b'*OPC?\n': raise KeyboardInterrupt()
                return super().sendall(data)
        c, w = self.client(InterruptWire())
        with self.assertRaises(KeyboardInterrupt): c.configure_dc(1)
        self.assertEqual(w.commands[-1], 'C2:OUTP OFF,LOAD,HZ,PLRT,NOR')
        self.assertIn(' OFF,', w.outp)

    def test_partial_receive_interrupt_discards_reply_until_fresh_connection(self):
        class InterruptedWire(Wire):
            def __init__(self, interruption):
                super().__init__()
                self.reads = 0
                self.interruption = interruption

            def recv(self, size):
                self.reads += 1
                if self.reads == 2:
                    raise self.interruption
                return super().recv(size)

        for interruption in (KeyboardInterrupt(), SystemExit(7)):
            with self.subTest(interruption=type(interruption).__name__):
                old = InterruptedWire(interruption)
                fresh = Wire(identity=IDN.replace('TESTSERIAL', 'NEWSERIAL'))
                peers = iter((old, fresh))
                client = sdg.SDGClient('offline.invalid', socket_factory=lambda address, timeout: next(peers))
                self.addCleanup(client.close)
                client.connect()
                with self.assertRaises(type(interruption)) as raised:
                    client.snapshot()
                self.assertIs(raised.exception, interruption)
                self.assertEqual(client._pending, bytearray())
                self.assertTrue(client._desynchronized)
                with self.assertRaises(sdg.SDGError): client.snapshot()
                self.assertEqual(old.commands, ['*IDN?'])
                client.close()
                self.assertTrue(old.closed)
                client.connect()
                self.assertIn('NEWSERIAL', client.snapshot()['raw_idn'])

    def test_dc_restore_and_context_connection(self):
        w = Wire(bswv='C2:BSWV WVTP,DC,OFST,-1V', outp=OUT.replace(' ON,', ' OFF,'))
        with sdg.SDGClient('offline.invalid', socket_factory=lambda address, timeout: w) as c:
            original = c.snapshot()
            c.configure_sine(50000, 1)
            state = c.restore(original)
            self.assertEqual(state['waveform'], 'DC')
            self.assertFalse(state['output_on'])
            self.assertEqual(state['offset_v'], -1)
        self.assertTrue(w.closed)

    def test_coupling_tracking_or_missing_flags_refused_without_writes(self):
        raw = 'COUP TRACE,OFF,FCOUP,OFF,PCOUP,OFF,ACOUP,OFF'
        for bad in (raw.replace('TRACE,OFF', 'TRACE,ON'), raw.replace('FCOUP,OFF', 'FCOUP,ON'),
                    raw.replace('PCOUP,OFF', 'PCOUP,ON'), raw.replace('ACOUP,OFF', 'ACOUP,ON'),
                    raw.replace(',ACOUP,OFF', ''), raw.replace('TRACE,OFF', 'TRACE,UNKNOWN')):
            c, w = self.client()
            w.override['COUP?'] = bad
            with self.assertRaises(sdg.SDGError): c.configure_dc(0)
            self.assertTrue(all(cmd.endswith('?') for cmd in w.commands))

    def test_actual_firmware_optional_trduch_must_be_off_when_present(self):
        base = 'COUP TRACE,OFF,FCOUP,OFF,PCOUP,OFF,ACOUP,OFF,TRDUCH,'
        c, w = self.client()
        w.override['COUP?'] = base + 'OFF'
        self.assertEqual(c.configure_dc(0)['coupling']['fields']['TRDUCH'], 'OFF')
        for value in ('ON', 'UNKNOWN'):
            with self.subTest(value=value):
                c, w = self.client()
                w.override['COUP?'] = base + value
                with self.assertRaises(sdg.SDGError): c.configure_dc(0)
                self.assertTrue(all(cmd.endswith('?') for cmd in w.commands))

    def test_actual_firmware_requires_full_output_syntax_to_preserve_hiz_amplitude(self):
        class ActualFirmwareWire(Wire):
            def sendall(self, data):
                if data == b'C2:OUTP LOAD,HZ\n':
                    # Observed by root on firmware 2.01.01.39R7T2: shorthand
                    # changes load to 50 and displayed amplitude to 0.5 Vpp.
                    self.commands.append('C2:OUTP LOAD,HZ')
                    self.outp = self.outp.replace('LOAD,HZ', 'LOAD,50')
                    self.bswv = self.bswv.replace('AMP,1V', 'AMP,0.5V')
                    return
                return super().sendall(data)
        c, w = self.client(ActualFirmwareWire())
        result = c.configure_sine(50000, 1, 0)
        self.assertEqual((result['load'], result['vpp'], result['output_on']), ('HZ', 1, True))
        writes = [cmd for cmd in w.commands if not cmd.endswith('?')]
        self.assertEqual(writes[0], 'C2:OUTP OFF,LOAD,HZ,PLRT,NOR')
        self.assertEqual(writes[-1], 'C2:OUTP ON,LOAD,HZ,PLRT,NOR')
        self.assertEqual(writes[1], 'C2:BSWV WVTP,SINE,FRQ,50000,AMP,1,OFST,0,PHSE,0')
        self.assertFalse(any(cmd.startswith(('C2:OUTP LOAD,', 'C2:OUTP PLRT,')) for cmd in writes))


if __name__ == '__main__': unittest.main(verbosity=2)
