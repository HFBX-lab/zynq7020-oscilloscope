"""CH2 output-only control; default CLI operation is a read-only snapshot.

Only explicit --on/--off or set_output() may write. Waveform settings are
never sent; SINE, bounded SQUARE and DC may retain their existing settings.
Failure after a write attempts complete CH2 OFF. This cannot guarantee OFF
when transport or physical connectivity fails.
"""
import argparse
import json
from pathlib import Path
import sys

from siglent_sdg import SDGClient, SDGError, _number, _safe_state, _sine_limits


def _safe_output_state(state):
    if state['waveform'] != 'SQUARE':
        _safe_state(state)
        return
    if state['load'] != 'HZ' or state['polarity'] != 'NOR':
        raise SDGError('Mutation requires CH2 High-Z and normal polarity')
    try:
        _sine_limits(state['frequency_hz'], state['vpp'], state['offset_v'])
        duty = _number(state['bswv'].get('DUTY'), 'SQUARE DUTY')
        if not 5 <= duty <= 95:
            raise ValueError('SQUARE DUTY must be 5..95 percent')
    except ValueError as exc:
        raise SDGError('Unsafe source state: ' + str(exc)) from exc


def _snapshot(client):
    state = client.snapshot()  # Reuses exact vendor/model identity validation.
    state['modes'] = client.continuous_state()
    state['coupling'] = client.coupling_state()
    return state


def _checked_snapshot(client):
    state = _snapshot(client)
    _safe_output_state(state)
    if any(value['state'] != 'OFF' for value in state['modes'].values()):
        raise SDGError('Mutation requires modulation, sweep and burst all OFF')
    if any(state['coupling']['fields'][key].upper() != 'OFF'
           for key in ('TRACE', 'FCOUP', 'PCOUP', 'ACOUP')):
        raise SDGError('Mutation requires tracking and all channel coupling OFF')
    # coupling_state also refuses non-OFF TRDUCH if the firmware reports it.
    return state


def set_output(client, enabled):
    """Change only CH2 ON/OFF after bounds/mode/identity validation.

    Both requested ON and requested OFF refuse unsafe initial states before
    writing. The exact raw BSWV and identity must match after the operation.
    Returns the complete verified post-operation snapshot.
    """
    if not isinstance(enabled, bool):
        raise ValueError('enabled must be a bool')
    initial = _checked_snapshot(client)
    try:
        # On firmware 2.01.01.39R7T2 standalone LOAD,HZ changed to 50 ohms;
        # only the complete form has been verified to preserve amplitude.
        client._send('C2:OUTP ' + ('ON' if enabled else 'OFF') + ',LOAD,HZ,PLRT,NOR')
        if client.query('*OPC?') != '1':
            raise SDGError('Operation complete response must be 1')
        actual = _checked_snapshot(client)
        if actual['raw_idn'] != initial['raw_idn']:
            raise SDGError('Instrument identity changed after output write')
        if actual['raw_bswv'] != initial['raw_bswv']:
            raise SDGError('Raw waveform settings changed after output write')
        if actual['output_on'] != enabled:
            raise SDGError('CH2 output readback mismatch')
        return actual
    except BaseException:
        try:
            client._send('C2:OUTP OFF,LOAD,HZ,PLRT,NOR')
        except BaseException:
            # Preserve the original error/interruption even if cleanup fails.
            pass
        raise


def main(argv=None):
    parser = argparse.ArgumentParser(description='SIGLENT SDG2122X CH2 snapshot or explicit output-only ON/OFF')
    parser.add_argument('host')
    parser.add_argument('--port', type=int, default=5025)
    parser.add_argument('--timeout', type=float, default=2)
    action = parser.add_mutually_exclusive_group()
    action.add_argument('--on', action='store_true', help='Explicitly enable existing CH2 waveform')
    action.add_argument('--off', action='store_true', help='Explicitly disable CH2 output')
    parser.add_argument('--output', type=Path, help='Also save returned JSON to this file')
    args = parser.parse_args(argv)
    try:
        with SDGClient(args.host, args.port, args.timeout) as client:
            state = set_output(client, args.on) if args.on or args.off else _snapshot(client)
        document = json.dumps(state, ensure_ascii=True, indent=2) + '\n'
        if args.output is not None:
            args.output.write_text(document, encoding='utf-8')
        print(document, end='')
        return 0
    except (SDGError, ValueError, OSError) as exc:
        print(str(exc), file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
