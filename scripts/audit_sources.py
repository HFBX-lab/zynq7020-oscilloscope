"""Read-only source checks; record and later verify original-file hashes."""
from pathlib import Path
import hashlib
import json
import fitz

root = Path(__file__).resolve().parents[1]
source = Path(r'D:\FPGA_Project\Smart_ZYNQ_SP2_information')
paths = [Path(r'D:/external-reference/project-notes.md'),
         source/'SmartZynq_SP2_Schematic_V1d3B_20260413.pdf.pdf',
         next(source.rglob('ATK-MD0700R模块用户手册_V1.2.pdf')),
         next(source.rglob('ATK-MD0700R_V1.6.pdf')),
         next(p for p in source.rglob('7寸1024600裸屏规格书.pdf') if '新资料' in str(p)),
         Path(r'D:\external-reference\2026省赛G题_扩展V4.HMI')]
record = {str(p): {'sha256':hashlib.sha256(p.read_bytes()).hexdigest(), 'bytes':p.stat().st_size}
          for p in paths}
manifest = root/'docs'/'source-hashes.json'
if manifest.exists():
    if json.loads(manifest.read_text(encoding='utf-8')) != record:
        raise SystemExit('Source content changed since baseline; inspect before continuing.')
    print('PASS: all six original sources match baseline SHA256.')
else:
    manifest.write_text(json.dumps(record,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('Recorded six read-only original sources.')
out=root/'artifacts'/'reference-excerpts.txt'
with out.open('w',encoding='utf-8') as f:
    for p,indices in [(paths[1],[7,11]),(paths[2],[2,5,6,7]),(paths[3],[0]),(paths[4],[10,11])]:
        with fitz.open(p) as doc:
            for i in indices:
                f.write(f'\nSOURCE: {p}\nPDF PAGE: {i+1}\n{doc[i].get_text()}\n')
print('Reference excerpts written; no source modified.')
