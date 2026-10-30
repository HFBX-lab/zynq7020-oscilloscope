"""Audit an existing board session's saved evidence. Never connects to hardware."""
from pathlib import Path
import hashlib
import json
import re
import sys

root = Path(__file__).resolve().parents[1]
session = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(
    'D:/FPGA_Project/SP2_PS_Screen_Build/board/20261020-first-bringup/attempt4-aux-reset')

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()

def read(name):
    return (session / name).read_text(encoding='utf-8-sig')

artifacts = {}
for folder in ['ps-screen', 'ps-firmware']:
    base = root / 'build' / folder
    state = json.loads((base / 'build-status.json').read_text(encoding='utf-8-sig'))
    assert state['state'] == 'succeeded'
    for artifact in state['artifacts']:
        name = artifact['file']
        if name.endswith(('.bit', '.xsa', '.elf')):
            assert sha(base / name) == sha(session / name) == artifact['sha256']
            artifacts[name] = artifact['sha256']
assert sha(root / 'firmware/board_init_helpers.tcl') == sha(session / 'board_init_helpers.tcl')
assert sha(root / 'build/ps-screen/ps7_init.tcl') == sha(session / 'ps7_init.tcl')
assert 'BOOT_MODE_REGISTER=0' in read('stage1_ocm.log')
assert 'PS_INIT_WITHOUT_DDR_DONE' in read('stage1_ocm.log')
assert 'STAGE2_DDR_INIT_PASS_READY_FOR_D' in read('stage2_ddr.log')
assert 'SCREEN_BRINGUP_RUNNING' in read('stage3_video.log')
uart = read('uart-COM18.log')
assert not re.search(r'DDR_FAIL|BRINGUP_STOP|FRAMEBUFFER_DDR_READBACK_FAIL', uart)
assert uart.index('WAIT_D:') < uart.index('DDR_TEST_BEGIN:')
assert uart.count('DDR_PATTERN_PASS:') == 2
assert 'OCM_PROBE_DONE:' in uart
assert 'FB0_COLORBARS_CRC32=DB424875 FB1_GRID_CRC32=6A57B038' in uart
assert 'pixel_clock_locked=1' in uart and 'VIDEO_STARTED:' in uart
samples = re.findall(r'VIDEO_DIAG: locked=(\d) underflow_sample=(\d) overflow_sample=(\d) pixel_clock_locked=(\d) dma_errors=([0-9a-fA-F]+)', uart)
assert len(samples) >= 3
assert all(sample == ('1', '0', '0', '1', '00000000') for sample in samples[-3:])
assert 'VIDEO_INTERNAL_LOCK_STABLE:' in uart
snapshot = read('final_snapshot.log')
assert 'FINAL_GPIO=9 ' in snapshot
assert 'FINAL_FPGA_STATE=FPGA is configured' in snapshot
assert 'PASS: final control access' in snapshot
logs = {name: sha(session / name) for name in ['stage1_ocm.log', 'stage2_ddr.log',
        'stage3_video.log', 'uart-COM18.log', 'final_snapshot.log']}
result = dict(state='verified-board-no-panel', session=str(session), artifacts=artifacts,
              log_sha256=logs, uart='COM18 115200 8N1', ddr='3 x 64KiB sampled regions; not full-capacity/stress',
              framebuffer_crc32=['DB424875', '6A57B038'],
              video='three one-second lock samples, zero sampled underflow/overflow and no VDMA errors; later GPIO=9',
              panel_test='not-run', touch='not-implemented', board_lvgl='not-implemented',
              freertos='not-implemented', adc='not-implemented', flash_written=False)
(root / 'build/board-validation.json').write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
print('PASS: current bit/XSA/ELF copies match the saved successful board session')
print('PASS: OCM UART, sampled DDR, both framebuffer CRCs, control access and internal video locks')
print('Panel and touch remain untested; wrote build/board-validation.json')
