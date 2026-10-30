"""Bounded, CH2-only SIGLENT SDG2122X engineering-calibration controller.

The command line is read-only: ``python scripts/siglent_sdg.py HOST``.
Mutation requires an explicit Python method call. No reset, CH1, coupling,
modulation, sweep, or burst writes are available. A failed mutation attempts
CH2 OFF; this cannot guarantee OFF after a physical/network failure.

Protocol references (manufacturer):
https://siglentna.com/wp-content/uploads/dlm_uploads/2017/11/SDG_ProgrammingGuide_PG_E03B.pdf
https://www.siglenteu.com/operating-tip/instrument-socket-and-telnet-port-information/
https://int.siglent.com/u_file/download/23_08_24/SDG_Programming%20Guide_PG02-E05B.pdf
AMP is the peak-to-peak BSWV amplitude in volts; AMPVRMS is a separate field.
The raw query values are retained; no independent voltage traceability is implied.
"""
import argparse
import json
import math
import re
import socket
import sys
import time


class SDGError(RuntimeError):
    """Identity, protocol, transport, or safe-state validation failed."""


def _number(value, name):
    if isinstance(value, bool):
        raise ValueError(name + ' must be a finite number')
    try:
        value = float(value)
    except (TypeError, ValueError, OverflowError) as exc:
        raise ValueError(name + ' must be a finite number') from exc
    if not math.isfinite(value):
        raise ValueError(name + ' must be finite')
    return value


def _sine_limits(frequency_hz, vpp, offset_v):
    frequency_hz = _number(frequency_hz, 'frequency_hz')
    vpp = _number(vpp, 'vpp')
    offset_v = _number(offset_v, 'offset_v')
    if not 1 <= frequency_hz <= 10_000_000:
        raise ValueError('frequency_hz must be 1..10000000 Hz')
    if not .2 <= vpp <= 4:
        raise ValueError('vpp must be 0.2..4 Vpp')
    if abs(offset_v) + vpp / 2 > 2.5:
        raise ValueError('abs(offset_v) + vpp/2 must be <= 2.5 V')
    return frequency_hz, vpp, offset_v


def _dc_limit(offset_v):
    offset_v = _number(offset_v, 'offset_v')
    if abs(offset_v) > 2:
        raise ValueError('DC offset_v must be -2..2 V')
    return offset_v


def _fields(raw, command):
    prefix = command + ' '
    if not isinstance(raw, str) or not raw.upper().startswith(prefix):
        raise SDGError('Expected ' + prefix + 'response')
    tokens = [x.strip() for x in raw[len(prefix):].split(',')]
    if len(tokens) % 2 or not tokens or any(not x for x in tokens):
        raise SDGError('Malformed key/value response for ' + command)
    result = {}
    for key, value in zip(tokens[::2], tokens[1::2]):
        key = key.upper()
        if key in result:
            raise SDGError('Duplicate response field: ' + key)
        result[key] = value
    return result


def _quantity(raw, units, field):
    match = re.fullmatch(r'([+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[Ee][+-]?\d+)?)\s*([A-Za-z]*)', raw)
    if not match or match[2].upper() not in units:
        raise SDGError('Invalid numeric value/unit for ' + field + ': ' + raw)
    value = float(match[1]) * units[match[2].upper()]
    if not math.isfinite(value):
        raise SDGError('Nonfinite value for ' + field)
    return value


def _parse_source(raw_bswv, raw_outp):
    bswv = _fields(raw_bswv, 'C2:BSWV')
    if 'WVTP' not in bswv:
        raise SDGError('Missing WVTP')
    waveform = bswv['WVTP'].upper()
    required = ('FRQ', 'AMP', 'OFST', 'PHSE') if waveform == 'SINE' else ('OFST',) if waveform == 'DC' else ()
    if any(key not in bswv for key in required):
        raise SDGError('Missing required ' + waveform + ' waveform fields')
    parsed = {}
    for key, target, units in (
        ('FRQ', 'frequency_hz', {'HZ': 1, 'KHZ': 1e3, 'MHZ': 1e6}),
        ('AMP', 'vpp', {'V': 1, 'VPP': 1, 'MV': 1e-3, 'UV': 1e-6}),
        ('OFST', 'offset_v', {'V': 1, 'MV': 1e-3, 'UV': 1e-6}),
        ('PHSE', 'phase_deg', {'': 1, 'DEG': 1}),
    ):
        parsed[target] = _quantity(bswv[key], units, key) if key in bswv else None
    prefix = 'C2:OUTP '
    if not isinstance(raw_outp, str) or not raw_outp.upper().startswith(prefix):
        raise SDGError('Expected C2:OUTP response')
    output, separator, remaining = raw_outp[len(prefix):].partition(',')
    if output.upper() not in ('ON', 'OFF') or not separator:
        raise SDGError('Invalid output state')
    outp = _fields('C2:OUTP ' + remaining, 'C2:OUTP')
    if 'LOAD' not in outp or 'PLRT' not in outp:
        raise SDGError('Missing LOAD/PLRT in output response')
    return dict(parsed, waveform=waveform, output_on=output.upper() == 'ON',
                load=outp['LOAD'].upper(), polarity=outp['PLRT'].upper(),
                bswv=bswv, outp=outp, raw_bswv=raw_bswv, raw_outp=raw_outp)


def _safe_state(state):
    if state['load'] != 'HZ' or state['polarity'] != 'NOR':
        raise SDGError('Mutation requires CH2 High-Z and normal polarity')
    try:
        if state['waveform'] == 'SINE':
            _sine_limits(state['frequency_hz'], state['vpp'], state['offset_v'])
            phase = _number(state['phase_deg'], 'phase_deg')
            if not 0 <= phase <= 360:
                raise ValueError('phase_deg must be 0..360')
        elif state['waveform'] == 'DC':
            _dc_limit(state['offset_v'])
        else:
            raise ValueError('Only SINE/DC states can be changed/restored')
    except ValueError as exc:
        raise SDGError('Unsafe source state: ' + str(exc)) from exc


class SDGClient:
    """One synchronous, single-owner raw TCP connection; not thread safe.

    ``snapshot()`` returns JSON-compatible raw fields plus SI numeric values.
    ``continuous_state()`` checks MDWV/SWWV/BTWV without changing them;
    ``coupling_state()`` reads global TRACE/FCOUP/PCOUP/ACOUP.
    configure_sine/DC enable CH2 after verification; restore preserves ON/OFF.
    Read/parse failures poison query framing until close/connect, so a delayed
    response cannot be mistaken for a later command. OFF writes remain possible.
    """
    QUERIES = frozenset(('*IDN?', '*OPC?', 'C2:BSWV?', 'C2:OUTP?',
                         'C2:MDWV?', 'C2:SWWV?', 'C2:BTWV?', 'COUP?'))
    MAX_LINE = 4096

    def __init__(self, host, port=5025, timeout=2, *, socket_factory=None):
        if not isinstance(host, str) or not host.strip() or any(x in host for x in '\r\n\x00'):
            raise ValueError('host must be a nonempty hostname/IP')
        if isinstance(port, bool) or not isinstance(port, int) or not 1 <= port <= 65535:
            raise ValueError('port must be 1..65535')
        timeout = _number(timeout, 'timeout')
        if not 0 < timeout <= 30:
            raise ValueError('timeout must be >0 and <=30 seconds')
        self.host, self.port, self.timeout = host, port, timeout
        self._socket_factory = socket_factory or socket.create_connection
        self._socket = None
        self._pending = bytearray()
        self._desynchronized = False

    def connect(self):
        if self._socket is not None:
            return self
        try:
            self._socket = self._socket_factory((self.host, self.port), timeout=self.timeout)
            self._socket.settimeout(self.timeout)
        except OSError as exc:
            self.close()
            raise SDGError('TCP connect failed: ' + str(exc)) from exc
        self._pending.clear()
        self._desynchronized = False
        return self

    def close(self):
        if self._socket is not None:
            self._socket.close()
            self._socket = None
        self._pending.clear()

    def __enter__(self):
        return self.connect()

    def __exit__(self, *unused):
        self.close()

    def _send(self, command):
        if not isinstance(command, str) or any(x in command for x in '\r\n;\x00'):
            raise ValueError('One ASCII command required; command injection rejected')
        try:
            payload = command.encode('ascii') + b'\n'
        except UnicodeEncodeError as exc:
            raise ValueError('ASCII command required') from exc
        if self._socket is None:
            raise SDGError('Client is not connected')
        try:
            self._socket.settimeout(self.timeout)
            self._socket.sendall(payload)
        except OSError as exc:
            raise SDGError('TCP command failed: ' + str(exc)) from exc

    def query(self, command):
        if command not in self.QUERIES:
            raise ValueError('Query is not in the read-only CH2 allowlist')
        if self._desynchronized:
            raise SDGError('Query stream lost synchronization; close/reconnect required')
        deadline = time.monotonic() + self.timeout
        try:
            self._send(command)
            while b'\n' not in self._pending:
                if len(self._pending) > self.MAX_LINE + 1 or (
                    len(self._pending) == self.MAX_LINE + 1 and not self._pending.endswith(b'\r')
                ):
                    raise SDGError('Response exceeds 4096 bytes')
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise SDGError('Response deadline exceeded')
                self._socket.settimeout(remaining)
                data = self._socket.recv(self.MAX_LINE + 2 - len(self._pending))
                if not data:
                    raise SDGError('Peer closed before response newline')
                self._pending.extend(data)
            line, _, tail = self._pending.partition(b'\n')
            self._pending = bytearray(tail)
            line = line.removesuffix(b'\r')
            if len(line) > self.MAX_LINE:
                raise SDGError('Response exceeds 4096 bytes')
            if not line or any(x < 32 or x > 126 for x in line):
                raise SDGError('Response must be one printable ASCII line')
            return line.decode('ascii')
        except BaseException as exc:
            self._pending.clear()
            self._desynchronized = True
            if isinstance(exc, OSError):
                raise SDGError('TCP response failed: ' + str(exc)) from exc
            # In particular, preserve Ctrl-C/SystemExit after poisoning the
            # stream: the unread tail belongs to this interrupted query.
            raise

    def snapshot(self):
        identity = self.query('*IDN?')
        fields = [x.strip() for x in identity.split(',')]
        if len(fields) != 4 or fields[0].upper() not in ('SIGLENT', 'SIGLENT TECHNOLOGIES', 'SIGLENT TECHNOLOGIES CO., LTD.') or fields[1].upper() != 'SDG2122X':
            raise SDGError('Expected SIGLENT SDG2122X identity')
        raw_bswv = self.query('C2:BSWV?')
        raw_outp = self.query('C2:OUTP?')
        return dict(_parse_source(raw_bswv, raw_outp), vendor=fields[0], model=fields[1], raw_idn=identity)

    def continuous_state(self):
        result = {}
        for mode in ('MDWV', 'SWWV', 'BTWV'):
            raw = self.query('C2:' + mode + '?')
            fields = _fields(raw, 'C2:' + mode)
            if fields.get('STATE', '').upper() not in ('ON', 'OFF'):
                raise SDGError('Missing/unknown ' + mode + ' STATE')
            result[mode] = {'state': fields['STATE'].upper(), 'raw': raw}
        return result

    def coupling_state(self):
        raw = self.query('COUP?')
        fields = _fields(raw, 'COUP')
        for key in ('TRACE', 'FCOUP', 'PCOUP', 'ACOUP'):
            if fields.get(key, '').upper() not in ('ON', 'OFF'):
                raise SDGError('Missing/unknown coupling flag: ' + key)
        # Firmware 2.01.01.39R7T2 also reports TRDUCH. Older documented
        # responses omit it, but never accept active/unknown tracking if present.
        if 'TRDUCH' in fields and fields['TRDUCH'].upper() != 'OFF':
            raise SDGError('Mutation requires optional TRDUCH tracking OFF')
        return {'raw': raw, 'fields': fields}

    def _checked_snapshot(self):
        state = self.snapshot()
        _safe_state(state)
        state['modes'] = self.continuous_state()
        if any(value['state'] != 'OFF' for value in state['modes'].values()):
            raise SDGError('Mutation requires modulation, sweep and burst all OFF')
        state['coupling'] = self.coupling_state()
        if any(state['coupling']['fields'][key].upper() != 'OFF' for key in ('TRACE', 'FCOUP', 'PCOUP', 'ACOUP')):
            raise SDGError('Mutation requires tracking and all channel coupling OFF')
        return state

    @staticmethod
    def _match(actual, desired, output_on):
        if actual['waveform'] != desired['waveform'] or actual['output_on'] != output_on:
            raise SDGError('Waveform/output readback mismatch')
        keys = ('frequency_hz', 'vpp', 'offset_v', 'phase_deg') if desired['waveform'] == 'SINE' else ('offset_v',)
        for key in keys:
            if not math.isclose(actual[key], desired[key], rel_tol=1e-6, abs_tol=2e-6):
                raise SDGError(key + ' readback mismatch')

    def _apply(self, desired, output_on):
        # Refuse unknown/unsafe initial states before any write, including OFF.
        initial = self._checked_snapshot()
        try:
            # Use the documented complete OUTP syntax. On the actual source,
            # standalone LOAD,HZ was interpreted as LOAD,50 and halved AMP.
            self._send('C2:OUTP OFF,LOAD,HZ,PLRT,NOR')
            output = _parse_source(initial['raw_bswv'], self.query('C2:OUTP?'))
            if output['output_on']:
                raise SDGError('CH2 did not switch OFF; waveform unchanged')
            _safe_state(output)
            if desired['waveform'] == 'SINE':
                command = 'C2:BSWV WVTP,SINE,FRQ,{:.12g},AMP,{:.12g},OFST,{:.12g},PHSE,{:.12g}'.format(
                    desired['frequency_hz'], desired['vpp'], desired['offset_v'], desired['phase_deg'])
            else:
                command = 'C2:BSWV WVTP,DC,OFST,{:.12g}'.format(desired['offset_v'])
            self._send(command)
            if self.query('*OPC?') != '1':
                raise SDGError('Operation complete response must be 1')
            state = self._checked_snapshot()
            self._match(state, desired, False)
            if output_on:
                self._send('C2:OUTP ON,LOAD,HZ,PLRT,NOR')
                state = self._checked_snapshot()
                self._match(state, desired, True)
            return state
        except BaseException:
            # Include Ctrl-C so interruption also attempts to leave CH2 OFF.
            try:
                self._send('C2:OUTP OFF,LOAD,HZ,PLRT,NOR')
            except Exception:
                pass
            raise

    def configure_sine(self, frequency_hz, vpp, offset_v=0):
        """Explicit CH2 write, verified then enabled; phase is set to zero."""
        frequency_hz, vpp, offset_v = _sine_limits(frequency_hz, vpp, offset_v)
        return self._apply(dict(waveform='SINE', frequency_hz=frequency_hz,
                                vpp=vpp, offset_v=offset_v, phase_deg=0), True)

    def configure_dc(self, offset_v):
        """Explicit CH2 DC write, verified then enabled."""
        return self._apply(dict(waveform='DC', offset_v=_dc_limit(offset_v)), True)

    def restore(self, snapshot):
        """Restore only original accepted SINE/DC/High-Z/NOR raw state.

        Raw unit-bearing values are reparsed, so a tampered parsed field cannot
        bypass bounds. Other waveform families/settings are never replayed.
        """
        try:
            state = _parse_source(snapshot['raw_bswv'], snapshot['raw_outp'])
            if snapshot['model'].upper() != 'SDG2122X':
                raise SDGError('Snapshot model mismatch')
        except (KeyError, TypeError, AttributeError) as exc:
            raise SDGError('Invalid snapshot') from exc
        _safe_state(state)
        return self._apply(state, state['output_on'])


def main(argv=None):
    parser = argparse.ArgumentParser(description='Read-only SIGLENT SDG2122X CH2 snapshot as JSON')
    parser.add_argument('host')
    parser.add_argument('--port', type=int, default=5025)
    parser.add_argument('--timeout', type=float, default=2)
    args = parser.parse_args(argv)
    try:
        with SDGClient(args.host, args.port, args.timeout) as client:
            print(json.dumps(client.snapshot(), ensure_ascii=True, indent=2))
        return 0
    except (SDGError, ValueError) as exc:
        print(str(exc), file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
