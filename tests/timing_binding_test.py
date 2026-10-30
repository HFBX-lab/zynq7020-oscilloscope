"""Offline probes for atomic UART and Tcl address evidence binding."""
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from audit_timing_evidence import atomic_timing_frames, check_export_tcl

header = 'MEASURE_STATUS panel=1 timing=1 channel=2 display_id=7 rate=50000000 count=4096 epoch=4 running=0\n'
rows = 'MEASURE_CHANNEL ch=1\nMEASURE_TIMING ch=1 pulse_valid=0\nMEASURE_CHANNEL ch=2\nMEASURE_TIMING ch=2 pulse_valid=1\n'
body = header + rows + 'MEASURE_END display_id=7\n'
assert len(atomic_timing_frames(body)) == 1
checks = 1
for bad in (body.replace('END display_id=7', 'END display_id=8'),
            body.replace('MEASURE_TIMING ch=2 pulse_valid=1\n', ''),
            body.replace('MEASURE_TIMING ch=2', 'MEASURE_TIMING ch=1')):
    try:
        atomic_timing_frames(bad)
    except AssertionError:
        checks += 1
    else:
        raise AssertionError('Invalid atomic timing frame accepted')
folder = Path('D:/FPGA_Project/SP2_Timing_Build/binding-test')
folder.mkdir(parents=True, exist_ok=True)
name = 'timing-test'
script = ('mrd -bin -file {D:/example/timing-test.bin} 0x13000000 4096\n'
          'mrd -bin -file {D:/example/timing-test-frame.bin} 0x10200000 460800\n')
snapshot = {'memory': {'view': {'record_address': '13000000'}, 'status': {'replay': '0'}}}
path = folder / ('export-' + name + '.tcl')
path.write_text(script, encoding='ascii')
check_export_tcl(folder, name, snapshot)
checks += 1
for bad in (script.replace('0x13000000', '0x13004000'), script.replace('4096', '4095'),
            script.replace('0x10200000', '0x13000000'), script.replace('460800', '460799')):
    path.write_text(bad, encoding='ascii')
    try:
        check_export_tcl(folder, name, snapshot)
    except AssertionError:
        checks += 1
    else:
        raise AssertionError('Invalid export Tcl accepted')
print('TIMING_BINDING_TEST_PASS:', checks, 'checks; no hardware')
