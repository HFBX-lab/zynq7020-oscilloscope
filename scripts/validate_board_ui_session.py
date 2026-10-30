"""Audit saved ARM UI board evidence; this command never connects to hardware."""
from pathlib import Path
import hashlib
import json
import re
import sys
import zlib
from PIL import Image

root = Path(__file__).resolve().parents[1]
mode = sys.argv[1] if len(sys.argv) > 1 else 'freertos'
assert mode in ('standalone', 'freertos')
base = root / 'build/board-lvgl' / mode
state = json.loads((base / 'board-session.json').read_text(encoding='utf-8-sig'))
assert state['state'] == 'verified-board-no-panel' and state['os'] == mode
assert state['flash_written'] is False
run = Path(state['run'])


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


for item in state['artifacts']:
    assert sha(Path(item['Path'])) == item['Hash']
assert sha(run / f'scope_lvgl_{mode}.elf') == sha(base / f'scope_lvgl_{mode}.elf')
assert sha(run / 'platform.bit') == sha(root / 'build/ps-screen/sp2_ps_screen.bit')
assert sha(run / 'ps7_init.tcl') == sha(root / 'build/ps-screen/ps7_init.tcl')
assert sha(run / 'board_init_helpers.tcl') == sha(root / 'firmware/board_init_helpers.tcl')
assert sha(run / 'uart.log') == state['uart_sha256']
uart = (run / 'uart.log').read_text(encoding='utf-8-sig')
# The port is opened before reset; discard any tail from the preceding firmware.
uart = uart[uart.index('SP2_LVGL_UART_READY:'):]
assert 'DDR_SAMPLE_PASS: 3x64KiB' in uart
assert f'LVGL_READY os={mode} version=9.2.2 lang=zh-CN input=UART_SIMULATED_TOUCH' in uart
assert not re.search(r'APP_FATAL|DDR_FAIL|VIDEO_[A-Z_]*FAIL|TAP_REJECTED', uart)


def fields(line):
    return dict(re.findall(r'(\w+)=([^\s]+)', line))


def diagnostics(text):
    rows = [fields(line) for line in text.splitlines()
            if line.startswith(f'APP_DIAG os={mode} ')]
    assert len(rows) >= 5
    for row in rows:
        for key in ('locked', 'pixel_locked'):
            assert row[key] == '1', row
        for key in ('underflow_sample', 'overflow_sample'):
            assert row[key] == '0', row
        assert int(row['dma_errors'], 16) == 0, row
        if mode == 'freertos':
            assert int(row['tasks']) == 5
            assert int(row['rtos_heap']) > 0 and int(row['ui_stack_words']) > 0
    assert int(rows[-1]['frames']) > int(rows[0]['frames'])
    return rows


rows = diagnostics(uart)
states = [fields(line) for line in uart.splitlines() if line.startswith('UI_STATUS ')]
for key, value in [('wave', '1'), ('wave', '2'), ('lang', 'en'), ('lang', 'zh-CN'),
                   ('running', '0'), ('running', '1'), ('grid', '0'), ('bars', '1')]:
    assert any(row.get(key) == value and row.get('touch_pending') == '0' for row in states), (key, value)
expected = dict(os=mode, lang='zh-CN', wave='2', time='3', scale='3', freq='2000',
                grid='1', bars='0', running='1', frozen='0', touch_pending='0')
for key, value in expected.items():
    assert state['final_ui'][key] == value and states[-1][key] == value

shots = []
for item in state['screenshots']:
    name = item['name']
    data = (run / f'{name}.bin').read_bytes()
    assert len(data) == 1843200
    crc = f'{zlib.crc32(data) & 0xffffffff:08x}'
    assert crc == item['crc32'].lower()
    assert sha(run / f'{name}.bin') == item['binary_sha256']
    assert f'addr={item["address"]} bytes=1843200 format=BGR888 crc={crc}' in uart
    expected_image = Image.frombytes('RGB', (1024, 600), data, 'raw', 'BGR')
    artifact = root / 'artifacts/board-lvgl' / mode / f'{name}.png'
    for path in (run / f'{name}.png', artifact):
        with Image.open(path) as actual:
            assert actual.size == (1024, 600) and actual.convert('RGB').tobytes() == expected_image.tobytes()
    shots.append(dict(name=name, crc32=crc, png_sha256=sha(artifact)))
assert {x['name'] for x in shots} == {'initial', 'configured'}

result = dict(state='verified-board-no-panel', os=mode, run=str(run), screenshots=shots,
              diagnostic_samples=len(rows), physical_lcd='not-run',
              physical_touch_driver='not-implemented', physical_touch_test='not-run',
              adc='not-implemented', flash_written=False)
if mode == 'freertos':
    tail = rows[-5:]
    ratio = (int(tail[-1]['tick']) - int(tail[0]['tick'])) / (int(tail[-1]['ms']) - int(tail[0]['ms']))
    assert 0.99 <= ratio <= 1.01 and abs(ratio - state['tick_per_global_ms']) < 1e-9
    assert int(rows[-1]['heartbeat']) > int(rows[0]['heartbeat'])
    result['tick_per_global_ms'] = ratio
    soak = run / 'soak.log'
    if soak.exists():
        observed = diagnostics(soak.read_text(encoding='utf-8-sig'))
        first, last = observed[0], observed[-1]
        duration = int(last['ms']) - int(first['ms'])
        assert duration >= 25000
        ratio = (int(last['tick']) - int(first['tick'])) / duration
        assert 0.99 <= ratio <= 1.01
        assert int(last['heartbeat']) > int(first['heartbeat'])
        assert len({row['rtos_heap'] for row in observed}) == 1
        assert len({row['ui_stack_words'] for row in observed}) == 1
        result['short_observation'] = dict(log_sha256=sha(soak), samples=len(observed),
            global_ms_start=int(first['ms']), global_ms_end=int(last['ms']),
            frames_start=int(first['frames']), frames_end=int(last['frames']),
            rendered_updates_per_second=(int(last['frames'])-int(first['frames']))*1000/duration,
            tick_per_global_ms=ratio, rtos_heap=int(last['rtos_heap']),
            ui_stack_free_words=int(last['ui_stack_words']))
(base / 'board-validation.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
print(f'PASS: {mode} saved board logs, artifact provenance, pointer states, independent DDR CRC and PNG pixels')
if 'short_observation' in result:
    print(json.dumps(result['short_observation']))
