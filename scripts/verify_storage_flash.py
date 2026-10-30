"""Independent full Flash readback validation; never accesses hardware."""
from pathlib import Path
import argparse,hashlib,json,re,zlib
CAPACITY=16777216
EXPECTED='0EF0D08CBDA1CC396C33A5DC357093F9F8742C27B16C047EFD0B848CE3667DD8'
def sha(b):return hashlib.sha256(b).hexdigest().upper()
def check_bytes(before,boot,after):
    if len(before)!=CAPACITY or len(after)!=CAPACITY or not 4096<len(boot)<=CAPACITY:raise ValueError('Full Flash length mismatch')
    if after[:len(boot)]!=boot or after[len(boot):]!=before[len(boot):]:raise ValueError('BOOT mismatch or original bytes after BOOT changed')
def load(p):return json.loads(Path(p).read_text(encoding='utf-8-sig'))
def main():
    p=argparse.ArgumentParser();p.add_argument('readback',type=Path);a=p.parse_args()
    base=Path('D:/FPGA_Project/SP2_Storage_Build/boot-update/programming/accepted-M5-final')
    plan=load(base/'program-plan.json');operation=load(base/'session.json');after=load(a.readback/'session.json')
    if operation['native_exit_code']!=0 or operation['flash_write_attempted'] is not True:raise ValueError('Programmer result incomplete')
    if after['state']!='verified-readonly-two-identical-full-16MiB-backups' or len(after['reads'])!=2:raise ValueError('Two complete readbacks required')
    before=(Path(plan['original_backup'])/'flash-1.bin').read_bytes();boot=Path(plan['boot_file']).read_bytes()
    if sha(before)!=plan['original_sha256'] or sha(boot)!=plan['boot_sha256']:raise ValueError('Original/BOOT identity differs')
    expected=boot+before[len(boot):]
    if sha(expected)!=EXPECTED or plan['expected_flash_sha256']!=EXPECTED:raise ValueError('Expected full Flash identity differs')
    log=(base/'program-flash.log').read_text(errors='replace')
    if not re.search(r'sf erase 0 4c0000',log,re.I):raise ValueError('Actual programmer erase range not bound')
    for n in (1,2):
        data=(a.readback/f'flash-{n}.bin').read_bytes();check_bytes(before,boot,data)
        row=after['reads'][n-1];text=(a.readback/f'probe-{n}-uart.log').read_text()
        if sha(data)!=EXPECTED or row['sha256']!=EXPECTED or row['crc32'].lower()!=f'{zlib.crc32(data):08x}':raise ValueError('Readback CRC/SHA differs')
        if not re.search(r'jedec=ef4018 sr1=00 sr2=02 sr3=60',text,re.I):raise ValueError('Flash identity/status changed')
        if not re.search(r'QSPI_READONLY_READY address=14000000 bytes=16777216 crc32='+row['crc32'],text,re.I):raise ValueError('Raw readback UART identity missing')
    report=dict(state='verified-new-M5-entire-Flash-awaiting-true-QSPI-cold-boot',flash_written=True,flash_capacity=CAPACITY,
        boot_bytes=len(boot),program_bytes=plan['program_bytes'],preserved_bytes_after_boot=CAPACITY-len(boot),preserved_last_sector_bytes=plan['retained_last_sector_bytes'],
        original_flash_sha256=sha(before),programmed_flash_sha256=EXPECTED,boot_sha256=sha(boot),readback=str(a.readback),
        application_elf_sha256=plan['application_elf_sha256'],calibration_id='2b474854',status_registers='unchanged:00/02/60',
        physical_QSPI_cold_boot='not-run',source_verifier_sha256=sha(Path(__file__).read_bytes()))
    (base/'full-readback-validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print('NEW_M5_FULL_FLASH_READBACK_PASS',EXPECTED,'every byte afterBOOT unchanged; true cold boot pending')
if __name__=='__main__':main()
