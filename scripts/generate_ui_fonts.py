"""Generate portable LVGL glyph subsets; normal simulator builds use saved C files."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
from fontTools import subset, version as fonttools_version
from fontTools.ttLib import TTFont

root = Path(__file__).resolve().parents[1]
source = root / 'cache/fonts/NotoSansSC-Regular.otf'
existing = Path('D:/SOLIDWORKS Corp/SOLIDWORKS Visualize/Plugins/sp.x/resources/fonts/OpenType/NotoSansSC-Regular.otf')
tool = Path('D:/FPGA_Project/SP2_Scope_Tools/font-converter/node_modules/lv_font_conv/lv_font_conv.js')
node = Path('D:/nodejs/node.exe')
assert tool.is_file(), 'Install lv_font_conv@1.5.3 in the documented D-drive tool directory first'
source.parent.mkdir(parents=True, exist_ok=True)
if not source.exists():
    assert existing.is_file(), 'Supply the original OFL Noto Sans SC font at cache/fonts/NotoSansSC-Regular.otf'
    shutil.copyfile(existing, source)
assets = root / 'assets/fonts'
assets.mkdir(parents=True, exist_ok=True)
code = '\n'.join((root / name).read_text(encoding='utf-8') for name in ('src/scope_ui.c','src/scope_storage_ui.c'))
symbols = ''.join(sorted({c for c in code if ord(c) > 127}))
points = set(range(32, 127)) | {ord(c) for c in symbols}
font = TTFont(source, recalcTimestamp=False)
assert points <= set(font.getBestCmap()), 'Source font is missing requested glyphs'
copyright_text = font['name'].getDebugName(0)
assert 'SIL Open Font License' in font['name'].getDebugName(13)
options = subset.Options()
options.name_IDs = ['*']
subsetter = subset.Subsetter(options=options)
subsetter.populate(unicodes=points)
subsetter.subset(font)
names = {1:'SP2 UI Sans', 2:'Regular', 3:'SP2UISans-Regular-Subset',
         4:'SP2 UI Sans Regular', 6:'SP2UISans-Regular', 16:'SP2 UI Sans', 17:'Regular'}
for record in font['name'].names:
    if record.nameID in names:
        record.string = names[record.nameID].encode(record.getEncoding())
if 'CFF ' in font:
    cff = font['CFF '].cff
    cff.fontNames = ['SP2UISans-Regular']
    cff.topDictIndex[0].FamilyName = 'SP2 UI Sans'
    cff.topDictIndex[0].FullName = 'SP2 UI Sans Regular'
subset_path = assets / 'SP2UISans-Subset.otf'
font.save(subset_path)
license_source = Path('D:/steam/steamapps/common/Portal/platform/resource/linux_fonts/notosanscjk-regular_license.txt')
license_path = assets / 'OFL.txt'
if not license_path.exists():
    assert license_source.is_file(), 'Supply the SIL OFL 1.1 text at assets/fonts/OFL.txt'
    license_path.write_text(copyright_text+'\n\n'+license_source.read_text(encoding='utf-8'), encoding='utf-8')

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

outputs = {}
for size in (14,16,24):
    output = Path(f'src/fonts/scope_font_{size}.c')
    args = [str(node), str(tool), '--font', 'assets/fonts/SP2UISans-Subset.otf',
            '--range', '0x20-0x7e', '--symbols', symbols, '--size', str(size),
            '--bpp', '4', '--no-compress', '--no-kerning', '--format', 'lvgl',
            '--lv-include', 'lvgl.h', '--output', output.as_posix()]
    subprocess.run(args, cwd=root, check=True)
    path = root / output
    notice = '/* SP2 UI Sans: derived glyph subset of Noto Sans SC.\n * '+copyright_text+'\n * Font software: SIL OFL 1.1; see assets/fonts/OFL.txt.\n */\n'
    path.write_text(notice+path.read_text(encoding='utf-8'), encoding='utf-8')
    outputs[output.as_posix()] = dict(sha256=sha(path), bytes=path.stat().st_size)
manifest = dict(family='SP2 UI Sans', original_family='Noto Sans SC', license='SIL OFL 1.1',
                source_sha256=sha(source), subset_sha256=sha(subset_path),
                fonttools_version=fonttools_version,
                converter_version=subprocess.check_output([str(node), str(tool), '--version'], text=True).strip(),
                unicode_count=len(points), non_ascii_symbols=symbols, outputs=outputs)
(assets / 'font-provenance.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
print(f'Generated 14/16/24 px fonts covering {len(points)} glyphs; source, tools and caches remain on D:')
