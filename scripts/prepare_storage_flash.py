"""Prepare an aligned, tail-preserving QSPI payload. Offline; never programs."""
from pathlib import Path
import argparse,hashlib,json,zlib
import re
import audit_storage_boot
CAPACITY=16*1024*1024
ERASE=65536
OLD_FLASH='1964739FEA5336887009D2D463628AA712D4FE9DB1A6E468BC14AAE2F81DA171'
BOOT_SHA='23F6B7E7EA62BB6A6CE6716B1F6D0E78F46D7DB213AA1DD258C17C0C2171753C'
def sha(b):return hashlib.sha256(b).hexdigest().upper()
def build_payload(before,second,boot):
    if len(before)!=CAPACITY or before!=second:raise ValueError('Two identical full flash backups required')
    if not 4096<len(boot)<=CAPACITY:raise ValueError('BOOT size outside flash capacity')
    aligned=(len(boot)+ERASE-1)//ERASE*ERASE
    payload=boot+before[len(boot):aligned]
    expected=boot+before[len(boot):]
    if len(payload)!=aligned or len(expected)!=CAPACITY or expected[len(boot):]!=before[len(boot):]:raise ValueError('Flash tail preservation failed')
    return payload,expected
def load(p):return json.loads(Path(p).read_text(encoding='utf-8-sig'))
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--boot-run',required=True,type=Path);p.add_argument('--backup',required=True,type=Path)
    p.add_argument('--m5-completion',required=True,type=Path);p.add_argument('--output',required=True,type=Path)
    a=p.parse_args();receipt=load(a.m5_completion)
    if receipt['state']!='verified-M5-user-physical-and-real-cold-TF-byte-retention':raise ValueError('M5 persistence acceptance missing')
    report=audit_storage_boot.audit_run(a.boot_run)
    image=a.boot_run/'boot-image';boot=(image/'BOOT.bin').read_bytes()
    if sha(boot)!=BOOT_SHA or report['boot_sha256']!=BOOT_SHA:raise ValueError('Unreviewed BOOT image')
    old=load(a.backup/'session.json')
    if old['state']!='verified-readonly-two-identical-full-16MiB-backups' or old['flash_written'] or old['flash_erased'] or len(old['reads'])!=2:raise ValueError('Backup evidence incomplete')
    backups=[]
    for n in (1,2):
        data=(a.backup/f'flash-{n}.bin').read_bytes();row=old['reads'][n-1]
        text=(a.backup/f'probe-{n}-uart.log').read_text()
        if sha(data)!=OLD_FLASH or row['sha256']!=OLD_FLASH or f'{zlib.crc32(data):08x}'!=row['crc32'].lower():raise ValueError('Current M7 Flash backup identity mismatch')
        if not re.search(r'jedec=ef4018 sr1=00 sr2=02 sr3=60',text,re.I):raise ValueError('Flash identity/status changed')
        if not re.search(r'QSPI_READONLY_READY address=14000000 bytes=16777216 crc32='+row['crc32'],text,re.I):raise ValueError('Flash backup UART CRC not bound')
        backups.append(data)
    payload,expected=build_payload(*backups,boot)
    a.output.mkdir(parents=True,exist_ok=False)
    (a.output/'program-payload.bin').write_bytes(payload);(a.output/'expected-flash.bin').write_bytes(expected)
    (a.output/'boot-validation.json').write_text(json.dumps(report,indent=2)+'\n')
    plan=dict(state='prepared-not-programmed',flash_capacity=CAPACITY,flash_type='qspi-x4-single',jedec='EF4018',offset=0,erase_alignment=ERASE,
        boot_bytes=len(boot),program_bytes=len(payload),retained_last_sector_bytes=len(payload)-len(boot),original_backup=str(a.backup.resolve()),
        original_sha256=OLD_FLASH,boot_sha256=sha(boot),payload_sha256=sha(payload),expected_flash_sha256=sha(expected),
        fsbl_elf_sha256=report['fsbl_elf_sha256'],application_elf_sha256=report['application_elf_sha256'],bitstream_sha256=report['bitstream_sha256'],
        fsbl_file=str((image/'fsbl.elf').resolve()),boot_file=str((image/'BOOT.bin').resolve()),payload_file=str((a.output/'program-payload.bin').resolve()),
        expected_file=str((a.output/'expected-flash.bin').resolve()),target_name='jsn-JTAG-SP2-YOUR_CABLE_SERIAL-4ba00477-0',
        boot_manifest_sha256=report['manifest_sha256'],m5_completion_file=str(a.m5_completion.resolve()),m5_completion_sha256=sha(a.m5_completion.read_bytes()),
        post_write_requires='Two independent full16MiB reads, every byte afterBOOT unchanged; user actual QSPI cold boot then readonly checks')
    (a.output/'program-plan.json').write_text(json.dumps(plan,indent=2)+'\n')
    print('STORAGE_FLASH_PREPARE_ONLY_PASS',len(payload),len(payload)-len(boot),sha(payload),sha(expected))
if __name__=='__main__':main()
