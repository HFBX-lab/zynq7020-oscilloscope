"""Storage boot safety regression tests; operate only on local binary files."""
import importlib.util
from pathlib import Path
import datetime
import hashlib
import json
import shutil
import struct
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('storage_boot_audit', ROOT/'scripts/audit_storage_boot.py')
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)
OLD = Path('D:/FPGA_Project/SP2_Boot_Build/boot-image')
BUILD_ROOT=Path('D:/FPGA_Project/SP2_Storage_Build/boot-update')
RUN=Path(json.loads((BUILD_ROOT/'latest.json').read_text())['run'])

def checksum_header(data,offset,count=16):
    values=list(struct.unpack_from('<'+str(count)+'I',data,offset))
    values[-1]=(~sum(values[:-1]))&0xffffffff
    struct.pack_into('<'+str(count)+'I',data,offset,*values)

class StorageBootTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        image=RUN/'boot-image'
        cls.boot=(image/'BOOT.bin').read_bytes()
        cls.fsbl=(image/'fsbl.elf').read_bytes()
        cls.bit=(image/'platform.bit').read_bytes()
        cls.app=(image/'application.elf').read_bytes()
        cls.pht=struct.unpack_from('<I',cls.boot,0x9c)[0]

    def check(self,boot=None,fsbl=None,bit=None,app=None,expected=None):
        return audit.check_boot(self.boot if boot is None else boot,self.fsbl if fsbl is None else fsbl,
            self.bit if bit is None else bit,self.app if app is None else app,
            expected_fsbl_sha=audit.FSBL_SHA if expected is None else expected)

    def test_prepared_image_is_valid(self):
        self.assertEqual(self.check()['boot_sha256'],'23F6B7E7EA62BB6A6CE6716B1F6D0E78F46D7DB213AA1DD258C17C0C2171753C')

    def test_previous_application_is_rejected(self):
        values = [(OLD/n).read_bytes() for n in ('BOOT.bin','fsbl.elf','platform.bit','application.elf')]
        with self.assertRaisesRegex(ValueError, 'application|identity'):
            audit.check_boot(*values)

    def test_requires_explicit_fsbl_identity(self):
        self.assertIn('expected_fsbl_sha', audit.check_boot.__code__.co_varnames)

    def test_has_provenance_gate(self):
        self.assertTrue(callable(getattr(audit, 'check_provenance', None)))

    def test_wrong_fsbl_identity_is_rejected(self):
        with self.assertRaisesRegex(ValueError,'FSBL identity'):self.check(expected='0'*64)

    def test_altered_fsbl_file_is_rejected(self):
        changed=bytearray(self.fsbl);changed[-1]^=1
        with self.assertRaisesRegex(ValueError,'FSBL identity'):self.check(fsbl=changed)

    def test_altered_bit_file_is_rejected(self):
        changed=bytearray(self.bit);changed[-1]^=1
        with self.assertRaisesRegex(ValueError,'application/bitstream'):self.check(bit=changed)

    def test_altered_application_is_rejected(self):
        changed=bytearray(self.app);changed[-1]^=1
        with self.assertRaisesRegex(ValueError,'application/bitstream'):self.check(app=changed)

    def test_boot_header_checksum_is_rejected(self):
        changed=bytearray(self.boot);changed[0x48]^=1
        with self.assertRaisesRegex(ValueError,'checksum'):self.check(boot=changed)

    def test_boot_register_initialization_table_is_rejected_if_changed(self):
        changed=bytearray(self.boot);changed[0xA0]^=1
        with self.assertRaisesRegex(ValueError,'register initialization table'):self.check(boot=changed)

    def test_partition_checksum_is_rejected(self):
        changed=bytearray(self.boot);changed[self.pht+60]^=1
        with self.assertRaisesRegex(ValueError,'Partition header checksum'):self.check(boot=changed)

    def test_partition_load_address_is_rejected_with_fixed_checksum(self):
        changed=bytearray(self.boot);offset=self.pht+128
        struct.pack_into('<I',changed,offset+12,0x200000)
        checksum_header(changed,offset)
        with self.assertRaisesRegex(ValueError,'address/image owner'):self.check(boot=changed)

    def test_signed_partition_is_rejected_with_fixed_checksum(self):
        changed=bytearray(self.boot);struct.pack_into('<I',changed,self.pht+32,1)
        checksum_header(changed,self.pht)
        with self.assertRaisesRegex(ValueError,'encrypted/signed'):self.check(boot=changed)

    def test_bit_payload_corruption_is_rejected(self):
        changed=bytearray(self.boot);offset=struct.unpack_from('<I',changed,self.pht+64+20)[0]*4
        changed[offset+128]^=1
        with self.assertRaisesRegex(ValueError,'payload'):self.check(boot=changed)

    def test_fsbl_payload_corruption_is_rejected(self):
        changed=bytearray(self.boot);offset=struct.unpack_from('<I',changed,self.pht+20)[0]*4
        changed[offset+128]^=1
        with self.assertRaisesRegex(ValueError,'payload'):self.check(boot=changed)

    def test_application_payload_corruption_is_rejected(self):
        changed=bytearray(self.boot);changed[-128]^=1
        with self.assertRaisesRegex(ValueError,'payload'):self.check(boot=changed)

    def test_trailing_bytes_are_rejected(self):
        with self.assertRaisesRegex(ValueError,'trailing'):self.check(boot=self.boot+b'\xff')

    def test_truncated_image_is_rejected(self):
        with self.assertRaisesRegex(ValueError,'payload'):self.check(boot=self.boot[:-1])

class ProvenanceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Isolated test-only copies leave the prepared BOOT/source snapshot unchanged.
        cls.sandbox=BUILD_ROOT/'tests'/datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f')
        shutil.copytree(RUN,cls.sandbox)
        cls.manifest=json.loads((cls.sandbox/'manifest.json').read_text())
        cc=cls.sandbox/'native/build/compile_commands.json'
        commands=json.loads(cc.read_text())
        for row in commands:
            for key,value in row.items():
                if isinstance(value,str):
                    row[key]=value.replace(str(RUN),str(cls.sandbox)).replace(RUN.as_posix(),cls.sandbox.as_posix())
        cc.write_text(json.dumps(commands,indent=2)+'\n')
        cls.refresh('native/build/compile_commands.json')
        cls.baseline={p:p.read_bytes() for p in cls.sandbox.rglob('*') if p.is_file()}
        cls.manifest_baseline=(cls.sandbox/'manifest.json').read_bytes()

    @classmethod
    def refresh(cls,relative):
        p=cls.sandbox/relative
        for row in cls.manifest['files']:
            if row['path']==relative:row.update(bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest().upper())
        (cls.sandbox/'manifest.json').write_text(json.dumps(cls.manifest,indent=2)+'\n')

    def tearDown(self):
        for path,data in self.baseline.items():
            if not path.exists() or path.read_bytes()!=data:path.write_bytes(data)
        self.manifest=json.loads(self.manifest_baseline)
        type(self).manifest=self.manifest

    def test_prepared_source_bsp_bindings_are_valid(self):
        self.assertEqual(audit.check_provenance(self.sandbox)['fsbl_sha256'],audit.FSBL_SHA)

    def test_manifest_rejects_bsp_archive_change(self):
        path=self.sandbox/'native/bsp/lib/libxil.a';path.write_bytes(path.read_bytes()+b'X')
        with self.assertRaisesRegex(ValueError,'Manifest file identity'):audit.check_provenance(self.sandbox)

    def test_old_xsa_even_with_updated_manifest_entry_is_rejected(self):
        path=self.sandbox/'hardware.xsa';path.write_bytes(path.read_bytes()+b'X');self.refresh('hardware.xsa')
        with self.assertRaisesRegex(ValueError,'Storage XSA mismatch'):audit.check_provenance(self.sandbox)

    def test_fsbl_identity_cannot_be_replaced_by_editing_manifest(self):
        manifest=json.loads((self.sandbox/'manifest.json').read_text());manifest['fsbl_sha256']='0'*64
        (self.sandbox/'manifest.json').write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError,'Expected FSBL identity'):audit.check_provenance(self.sandbox)

    def test_extra_ps_init_edit_is_rejected_after_rehash(self):
        path=self.sandbox/'native/source/ps7_init.c';path.write_bytes(path.read_bytes()+b'\n/* extra */\n')
        self.refresh('native/source/ps7_init.c')
        with self.assertRaisesRegex(ValueError,'Generated PSinit'):audit.check_provenance(self.sandbox)

    def test_changed_official_fsbl_source_is_rejected_after_rehash(self):
        path=self.sandbox/'native/source/main.c';path.write_bytes(path.read_bytes()+b'\n/* extra */\n')
        self.refresh('native/source/main.c')
        with self.assertRaisesRegex(ValueError,'Frozen source changed'):audit.check_provenance(self.sandbox)

    def test_changed_bsp_header_is_rejected_after_rehash(self):
        relative='native/bsp/include/xparameters.h';path=self.sandbox/relative
        path.write_bytes(path.read_bytes()+b'\n/* extra */\n');self.refresh(relative)
        with self.assertRaisesRegex(ValueError,'Frozen BSP differs'):audit.check_provenance(self.sandbox)

    def test_missing_compiler_debug_flag_is_rejected_after_rehash(self):
        relative='native/build/compile_commands.json';path=self.sandbox/relative
        path.write_text(path.read_text().replace('-DFSBL_DEBUG_INFO',''));self.refresh(relative)
        with self.assertRaisesRegex(ValueError,'compiler flag missing'):audit.check_provenance(self.sandbox)

    def test_untracked_source_is_rejected(self):
        manifest=json.loads((self.sandbox/'manifest.json').read_text())
        manifest['files']=[r for r in manifest['files'] if r['path']!='native/source/main.c']
        (self.sandbox/'manifest.json').write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError,'does not cover'):audit.check_provenance(self.sandbox)

if __name__ == '__main__': unittest.main(verbosity=2)
