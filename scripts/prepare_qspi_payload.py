"""Stage a QSPI boot payload, retaining original bytes in the last erase block."""
from pathlib import Path
import argparse,hashlib,json
CAPACITY=16*1024*1024
SECTOR=65536
def digest(data):return hashlib.sha256(data).hexdigest().upper()
def make_payload(original,boot):
    if not isinstance(original,bytes) or not isinstance(boot,bytes) or len(original)!=CAPACITY or not 0<len(boot)<=CAPACITY:
        raise ValueError('Exact16MiB backup and nonempty fitting boot image required')
    end=(len(boot)+SECTOR-1)//SECTOR*SECTOR
    return boot+original[len(boot):end]
def main():
    p=argparse.ArgumentParser();p.add_argument('--backup-run',type=Path,required=True);p.add_argument('--boot-directory',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    backup=json.loads((a.backup_run/'session.json').read_text(encoding='utf-8-sig'))
    if backup['state']!='verified-readonly-two-identical-full-16MiB-backups' or backup['flash_written'] or backup['flash_erased']:raise ValueError('Backup state not verified')
    if len(backup['reads'])!=2:raise ValueError('Two full reads required')
    old=(a.backup_run/backup['reads'][0]['file']).read_bytes();other=(a.backup_run/backup['reads'][1]['file']).read_bytes()
    if old!=other or any(r['sha256']!=digest(old) for r in backup['reads']):raise ValueError('Full backups changed')
    validation=json.loads((a.boot_directory/'validation.json').read_text())
    boot=(a.boot_directory/'BOOT.bin').read_bytes()
    if validation['state']!='verified-offline-boot-image-not-programmed' or validation['boot_sha256']!=digest(boot):raise ValueError('Boot image not verified')
    if validation['application_elf_sha256']!='54E9A67C6EB36A434C0FB525B945FF551267B720DBD140C04E7FEC878B6066F5':raise ValueError('Wrong application')
    payload=make_payload(old,boot)
    a.output.mkdir(parents=True,exist_ok=False)
    (a.output/'program-payload.bin').write_bytes(payload)
    plan=dict(state='prepared-not-programmed',flash_capacity=CAPACITY,flash_type='qspi-x4-single',jedec='EF4018',offset=0,
        erase_alignment=SECTOR,boot_bytes=len(boot),program_bytes=len(payload),retained_last_sector_bytes=len(payload)-len(boot),
        original_backup=str(a.backup_run),original_sha256=digest(old),boot_sha256=digest(boot),payload_sha256=digest(payload),
        expected_flash_sha256=digest(boot+old[len(boot):]),fsbl_elf_sha256=validation['fsbl_elf_sha256'],
        fsbl_file=str(a.boot_directory/'fsbl.elf'),boot_file=str(a.boot_directory/'BOOT.bin'),payload_file=str(a.output/'program-payload.bin'),
        target_name='jsn-JTAG-SP2-YOUR_CABLE_SERIAL-4ba00477-0',post_write_requires='Full16MiB read: newBOOT prefix exact, every byte afterBOOT unchanged; plus physical cold boot')
    (a.output/'program-plan.json').write_text(json.dumps(plan,indent=2)+'\n')
    print('QSPI_PAYLOAD_PREPARED',len(boot),len(payload),'retained',len(payload)-len(boot),plan['payload_sha256'])
if __name__=='__main__':main()
