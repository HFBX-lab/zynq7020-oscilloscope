"""Copy the completed RAM parameter session and its audit dependencies."""
import ast
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import sys

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('run', type=Path)
parser.add_argument('--build-root', type=Path, default=Path('D:/FPGA_Project/SP2_Settings_Build'))
parser.add_argument('--firmware-directory', type=Path, default=Path('build/settings-ui-firmware/freertos'))
parser.add_argument('--ui-evidence', type=Path)
parser.add_argument('--artifact-parent', default='settings-ui-board')
parser.add_argument('--no-observation', action='store_true', help='Archive a superseded data run without claiming runtime observation.')
args = parser.parse_args()
run = args.run.resolve()
session = json.loads((run / 'session.json').read_text(encoding='utf-8-sig'))
assert session['state'].startswith('verified-board-') and (session.get('parameter_exports') or session.get('spectrum_exports'))
assert (run / 'settings-evidence-audit.json').is_file() or (run / 'spectrum-evidence-audit.json').is_file()
assert args.no_observation or (run / 'observation.json').is_file()
out = root / 'artifacts' / args.artifact_parent / run.name
out.mkdir(parents=True, exist_ok=True)
(out / 'source').mkdir(exist_ok=True)
for path in run.iterdir():
    if path.is_file() and path.suffix not in ('.bit', '.elf'):
        shutil.copy2(path, out / path.name)

pending = ['audit_highspeed_50m_evidence', 'audit_measurement_evidence',
           'audit_autoset_evidence', 'audit_memory_evidence', 'audit_settings_evidence',
           'record_blob', 'settings_blob', 'analyze_adc_live_record', 'decode_board_frame']
if (run / 'zero_marker_records.json').is_file() or session.get('zero_marker_records'):
    pending.append('audit_zero_markers_evidence')
if session.get('spectrum_exports'):
    pending.extend(('audit_spectrum_evidence', 'spectrum_reference'))
copied = set()
while pending:
    name = pending.pop()
    if name in copied:
        continue
    path = root / 'scripts' / (name + '.py')
    if not path.is_file():
        continue
    shutil.copy2(path, out / path.name)
    copied.add(name)
    for node in ast.walk(ast.parse(path.read_text(encoding='utf-8-sig'))):
        if isinstance(node, ast.ImportFrom) and node.module:
            pending.append(node.module)
        elif isinstance(node, ast.Import):
            pending.extend(alias.name for alias in node.names)

for name in ('settings_blob_test.py', 'settings_evidence_negative_test.py',
             'settings_session_helpers_test.ps1', 'settings_log_capacity_test.py'):
    shutil.copy2(root / 'tests' / name, out / name)
if session.get('zero_marker_records'):
    shutil.copy2(root / 'tests/zero_markers_evidence_test.py', out / 'zero_markers_evidence_test.py')
    (out / 'source').mkdir(exist_ok=True)
    shutil.copy2(root / 'tests/channel_ui_test.c', out / 'source/channel_ui_test.c')
if session.get('spectrum_exports'):
    for name in ('spectrum_reference_test.py','spectrum_evidence_negative_test.py',
                 'spectrum_session_helpers_test.ps1','spectrum_log_capacity_test.py'):
        shutil.copy2(root / 'tests' / name, out / name)
    for name in ('scope_spectrum.c','scope_spectrum.h'):
        shutil.copy2(root / 'src' / name, out / 'source' / name)
    for name in ('scope_spectrum_test.c','spectrum_ui_test.c','spectrum_fixture.c'):
        shutil.copy2(root / 'tests' / name, out / 'source' / name)
for name in ('scope_settings.c', 'scope_settings.h', 'scope_ui.c', 'scope_ui.h'):
    target = out / 'source' / name
    target.parent.mkdir(exist_ok=True)
    shutil.copy2(root / 'src' / name, target)
build = args.build_root
for folder in ('codec', 'python-oracle', 'pixel-oracle', 'ui', 'core', 'oracle', 'review', 'helper'):
    source = args.ui_evidence if folder == 'ui' and args.ui_evidence else build / folder
    for path in source.glob('*'):
        if path.is_file() and path.suffix in ('.json', '.log', '.txt', '.png', '.scp'):
            target = out / 'offline' / folder / path.name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, target)
for name in ('helper-red.txt', 'helper-green.txt', 'helper-green-windowsps5.txt',
             'helper-test.log', 'host-poller.log', 'log-test-red.txt', 'log-test-green.txt'):
    if (build / name).is_file():
        shutil.copy2(build / name, out / name)
for name in ('validation.json', 'build-status.json', 'compile.log', 'configure.log'):
    shutil.copy2(root / args.firmware_directory / name, out / name)

entries = []
for path in sorted(out.rglob('*')):
    if path.is_file() and path.name != 'files-sha256.json':
        data = path.read_bytes()
        entries.append(dict(file=path.relative_to(out).as_posix(), bytes=len(data),
                            sha256=hashlib.sha256(data).hexdigest().upper()))
(out / 'files-sha256.json').write_text(json.dumps(entries, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
for entry in entries:
    data = (out / entry['file']).read_bytes()
    assert len(data) == entry['bytes'] and hashlib.sha256(data).hexdigest().upper() == entry['sha256']
print(json.dumps(dict(directory=str(out), files=len(entries), bytes=sum(e['bytes'] for e in entries),
                      audit_dependencies=sorted(copied)), ensure_ascii=False))
