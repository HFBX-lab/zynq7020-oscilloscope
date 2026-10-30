"""Negative checks for TF ARM provenance, no hardware access."""
from pathlib import Path
import sys,struct,tempfile,hashlib
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
import validate_storage_build as gate
def reject(f,*a):
    try:f(*a)
    except AssertionError:return
    raise AssertionError('invalid input accepted')
gate.check_tf_macro(['gcc','-DSCOPE_TF_STORAGE=1'])
for args in (['gcc'],['gcc','-DSCOPE_TF_STORAGE=0'],['gcc','-DSCOPE_TF_STORAGE=1','-USCOPE_TF_STORAGE'],['gcc','-D','SCOPE_TF_STORAGE=1','-DSCOPE_TF_STORAGE=1']):reject(gate.check_tf_macro,args)
data=bytearray(256);data[:6]=b'\x7fELF\x01\x01';struct.pack_into('<H',data,18,40);struct.pack_into('<II',data,24,0x100000,52);struct.pack_into('<HH',data,42,32,1);struct.pack_into('<8I',data,52,1,128,0x100000,0x100000,32,1024,5,4096)
assert gate.elf_ranges(data)==[['0x100000','0x100400']]
bad=bytearray(data);struct.pack_into('<I',bad,52+20,0x20000000);reject(gate.elf_ranges,bad)
bad=bytearray(data);struct.pack_into('<I',bad,52+12,0x100004);reject(gate.elf_ranges,bad)
bad=bytearray(data);struct.pack_into('<I',bad,52+4,255);reject(gate.elf_ranges,bad)
base=Path('D:/FPGA_Project/SP2_Storage_Build/build-gate-tests');base.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(dir=base) as tmp:
    root=Path(tmp);fw=root/'deliverable';run=root/'run';fw.mkdir();(run/'cmake').mkdir(parents=True)
    artifacts=[]
    for name in ('scope_lvgl_freertos.elf','scope_lvgl_freertos.map'):
        payload=name.encode();(fw/name).write_bytes(payload);(run/'cmake'/name).write_bytes(payload)
        artifacts.append({'file':name,'sha256':hashlib.sha256(payload).hexdigest().upper()})
    status={'artifacts':artifacts};gate.check_artifacts(status,fw,run)
    elf=fw/'scope_lvgl_freertos.elf';original=elf.read_bytes();elf.write_bytes(original+b'x');reject(gate.check_artifacts,status,fw,run);elf.write_bytes(original)
    mapfile=fw/'scope_lvgl_freertos.map';original=mapfile.read_bytes();mapfile.write_bytes(b'libstorage_sdps.a(xsdps_card.o)');reject(gate.check_artifacts,status,fw,run);mapfile.write_bytes(original)
    (run/'cmake'/mapfile.name).write_bytes(b'other');reject(gate.check_artifacts,status,fw,run);(run/'cmake'/mapfile.name).write_bytes(original)
    reject(gate.check_artifacts,{'artifacts':artifacts+artifacts[:1]},fw,run)
    for folder in ('board','touch','adc_live','ui','legacy','lvgl','storage'):(run/folder).mkdir()
    required=('board/app.c','board/board.c','board/board.h','board/portable.c','board/portable.h','ui/scope_ui.c','ui/scope_ui.h','ui/scope_storage.c','ui/scope_storage.h','ui/scope_storage_ui.c','ui/scope_storage_ui.h','storage/board_storage.c','storage/board_storage.h')
    records=[]
    for name in required:
        file=run/name;file.write_text('fixture');records.append({'Path':str(file),'Hash':gate.sha(file)})
    gate.check_source_inventory({'source_hashes':records},run)
    reject(gate.check_source_inventory,{'source_hashes':[]},run)
    reject(gate.check_source_inventory,{'source_hashes':records[:-1]},run)
    reject(gate.check_source_inventory,{'source_hashes':records+records[:1]},run)
print('STORAGE_BUILD_NEGATIVE_PASS TF-macro=unique-enabled ELF=ARM-entry/bounds/file/physical artifacts=ELF/map/deliverable/original/duplicate')
