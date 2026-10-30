"""Independent storage boot binary and frozen source/BSP provenance audit; offline."""
from pathlib import Path
import argparse,hashlib,json,re,struct,zipfile
APP_SHA='6917E229BE48D990EB7255ADA4E5BDDD052CEBF4B6E97BABD105C69C4B9C5A4C'
BIT_SHA='BE748A2A543047688010B3FD6179CDA517EB5EDBA76A27962D62EFBCA90A3FBE'
XSA_SHA='820B9017A5D13D679BE8CFF9E759E1433A2566DFB13BCF9E42EF011F34A6FE02'
FSBL_SHA='F24CD4A57673405B171A18F15EE302CA3EDD18E8BD2854F829FD5BC2DA14D77C'
def require(value,message):
    if not value:raise ValueError(message)
def sha(data):return hashlib.sha256(data).hexdigest().upper()
def elf(data,kind):
    require(data[:6]==b'\x7fELF\x01\x01' and struct.unpack_from('<H',data,18)[0]==40,'Not ELF32 ARM')
    entry,offset=struct.unpack_from('<II',data,24);size,count=struct.unpack_from('<HH',data,42)
    require(size>=32 and offset+size*count<=len(data),'Bad ELF program table')
    segments=[]
    for i in range(count):
        v=struct.unpack_from('<8I',data,offset+i*size)
        if v[0]!=1:continue
        require(v[2]==v[3] and v[4]<=v[5] and v[1]+v[4]<=len(data),'ELF physical/virtual/length mismatch')
        address,end=v[3],v[3]+v[5]
        if kind=='fsbl':require((0<=address<end<=0x30000) or (0xffff0000<=address<end<=0xfffffe00),'FSBL outside OCM')
        else:require(0x100000<=address<end<=0x1000000,'Application outside accepted DDR range')
        segments.append(v)
    active=[s for s in segments if s[4]];require(active,'No ELF data segments')
    low=min(s[3] for s in active);high=max(s[3]+s[4] for s in active)
    out=bytearray(high-low)
    for s in active:out[s[3]-low:s[3]-low+s[4]]=data[s[1]:s[1]+s[4]]
    require(entry==low==(0 if kind=='fsbl' else 0x100000),'ELF entry/load base changed')
    return entry,bytes(out),[dict(address=s[3],file_bytes=s[4],memory_bytes=s[5]) for s in segments]
def bit_payload(data):
    require(len(data)>=16,'Short bitstream')
    length=struct.unpack_from('>H',data)[0];pos=2+length
    require(pos+2<len(data) and data[pos:pos+2]==b'\x00\x01','Invalid .bit prefix')
    pos+=2;tags=[]
    while pos<len(data):
        tag=data[pos];pos+=1;tags.append(chr(tag))
        if tag==101:
            require(pos+4<=len(data),'Short bit payload length')
            length=struct.unpack_from('>I',data,pos)[0];pos+=4
            require(pos+length==len(data) and length%4==0 and tags==list('abcde'),'Bad bitstream layout')
            return data[pos:]
        require(tag in (97,98,99,100) and pos+2<=len(data),'Unknown .bit tag')
        length=struct.unpack_from('>H',data,pos)[0];pos+=2+length
    raise ValueError('Missing bitstream payload')
def check_boot(data,fsbl,bit,app,expected_fsbl_sha=None):
    require(0x1000<len(data)<=16*1024*1024,'Boot image capacity/length invalid')
    require(sha(bit)==BIT_SHA and sha(app)==APP_SHA,'Accepted application/bitstream changed')
    require(expected_fsbl_sha is not None and sha(fsbl)==expected_fsbl_sha,'Explicit FSBL identity mismatch')
    require(struct.unpack_from('<8I',data)==(0xeafffffe,)*8,'Unexpected boot vectors')
    h=struct.unpack_from('<11I',data,0x20)
    require(h[0]==0xaa995566 and h[1]==0x584c4e58 and h[2]==0 and h[3]==0x01010000,'Bad nonsecure Zynq boot header')
    require(sum(h)&0xffffffff==0xffffffff,'Boot header checksum mismatch')
    require(struct.unpack_from('<512I',data,0xA0)==(0xffffffff,0)*256,
            'Unexpected BootROM register initialization table')
    require(h[6]==h[7]==0 and h[5]==h[8] and 0<h[5]<=0x30000 and h[9]==1,'FSBL boot header length/address/config mismatch')
    iht_offset,pht_offset=struct.unpack_from('<II',data,0x98)
    require(iht_offset%64==pht_offset%64==0 and 0x100<=iht_offset<pht_offset<h[4],'Header tables overlap payload')
    iht=struct.unpack_from('<16I',data,iht_offset)
    require(iht[0]==0x01020000 and iht[1]==3 and iht[2]*4==pht_offset and iht[4]==0,'Unexpected image/header table')
    require(iht[5:]==(0xffffffff,)*11,'Unexpected reserved image-table fields')
    expected_names=('fsbl.elf','platform.bit','application.elf');ih=iht[3]*4;image_offsets=[]
    for i,name in enumerate(expected_names):
        require(0x100<=ih and ih+64<=pht_offset,'Invalid image-header pointer')
        image_offsets.append(ih);row=struct.unpack_from('<16I',data,ih)
        name_bytes=data[ih+16:ih+64]
        decoded=b''.join(name_bytes[n:n+4][::-1] for n in range(0,len(name_bytes),4)).split(b'\0',1)[0].decode('ascii')
        require(decoded==name and row[1]*4==pht_offset+i*64 and row[2:4]==(0,1),'Image names/order/section count changed')
        ih=row[0]*4
    require(ih==0,'More image headers than expected')
    fsbl_entry,fsbl_flat,fsbl_segments=elf(fsbl,'fsbl');app_entry,app_flat,app_segments=elf(app,'app')
    bits=bit_payload(bit);swapped=b''.join(bits[n:n+4][::-1] for n in range(0,len(bits),4))
    padded=swapped+struct.pack('<I',0x20000000)*((-len(swapped)//4)%4)
    expected_payloads=(fsbl_flat,padded,app_flat);parts=[];end=h[4]
    for i,(name,wanted) in enumerate(zip(expected_names,expected_payloads)):
        p=struct.unpack_from('<16I',data,pht_offset+64*i)
        require(sum(p)&0xffffffff==0xffffffff,'Partition header checksum mismatch')
        start,size=p[5]*4,p[1]*4
        require(p[0]==p[1]==p[2] and p[6]==(0x20 if i==1 else 0x10) and p[7]==1 and p[8]==p[10]==0,'Partition is encrypted/signed/checksummed/extra-section or wrong device')
        require(p[9]*4==image_offsets[i] and p[3]==p[4]==(0x100000 if i==2 else 0),'Partition header address/image owner mismatch')
        require(p[11:15]==(0,0,0,0),'Unexpected reserved partition fields')
        require(start>=end and start+size<=len(data) and data[start:start+size]==wanted,'Partition payload mismatch')
        require(all(v==0xff for v in data[end:start]),'Non-padding bytes between partitions')
        if i==0:require(start==h[4] and size==h[5],'BootROM FSBL source/length mismatch')
        parts.append(dict(name=name,offset=start,bytes=size,load_address=p[3],entry=p[4],sha256=sha(wanted)))
        end=start+size
    require(end==len(data),'Truncated/extra trailing boot bytes')
    require(b'Xilinx First Stage Boot Loader' in fsbl and b'QSPI' in fsbl,'FSBL debug/QSPI code missing')
    return dict(state='verified-offline-boot-image-not-programmed',bytes=len(data),boot_sha256=sha(data),partitions=parts,
        fsbl_elf_sha256=sha(fsbl),application_elf_sha256=sha(app),bitstream_sha256=sha(bit),
        fsbl_segments=fsbl_segments,application_segments=app_segments,secure_boot=False,hardware_accessed=False)
def check_provenance(base,expected_fsbl_sha=FSBL_SHA):
    base=Path(base).resolve()
    manifest=json.loads((base/'manifest.json').read_text(encoding='utf-8'))
    require(manifest['fsbl_sha256']==expected_fsbl_sha,'Expected FSBL identity differs from provenance manifest')
    require(manifest['xsa_sha256']==XSA_SHA and manifest['application_sha256']==APP_SHA and
            manifest['bitstream_sha256']==BIT_SHA,'Frozen manifest identity mismatch')
    require(manifest['hardware_accessed'] is False and manifest['flash_written'] is False and
            manifest['sd_written'] is False,'Manifest must remain offline-only')
    rows=manifest['files'];tracked=set()
    for row in rows:
        relative=row['path'];path=(base/relative).resolve()
        require(base in path.parents and relative not in tracked,'Unsafe/duplicate manifest path')
        require(path.is_file() and path.stat().st_size==row['bytes'] and sha(path.read_bytes())==row['sha256'],
                'Manifest file identity mismatch: '+relative)
        tracked.add(relative)
    native=base/'native';image=base/'boot-image'
    required={'hardware.xsa','bsp.log','build_boot_image.tcl','build_boot_image.ps1','prepare_boot_image.py',
              'audit_storage_boot.py','native/CMakeLists.txt','native/ps-init-cast.json',
              'native/build/fsbl.elf','native/build/fsbl.map','native/build/compile_commands.json',
              'native/compile-final.log','native/actual-commands.log','native/configure.log'}
    for tree in ('native/source','native/bsp','native/vendor-generated','boot-image'):
        required.update(p.relative_to(base).as_posix() for p in (base/tree).rglob('*') if p.is_file())
    require(required<=tracked,'Manifest does not cover full frozen source/BSP/image inputs')
    require(sha((base/'hardware.xsa').read_bytes())==XSA_SHA,'Storage XSA mismatch')
    exported=list((base/'workspace/sp2_storage_boot/export/sp2_storage_boot/hw').glob('*.xsa'))
    require(len(exported)==1 and sha(exported[0].read_bytes())==XSA_SHA and
            exported[0].relative_to(base).as_posix() in tracked,'Exported BSP XSA identity mismatch')
    with zipfile.ZipFile(base/'hardware.xsa') as archive:
        original=archive.read('ps7_init.c')
    before=b'while ((*addr < delay))';after=b'while ((*addr < (unsigned long)delay))'
    require((native/'vendor-generated/ps7_init.c').read_bytes()==original and original.count(before)==1 and
            (native/'source/ps7_init.c').read_bytes()==original.replace(before,after),
            'Generated PSinit differs beyond explicit equivalent cast')
    source=base/'workspace/sp2_storage_fsbl/src'
    vendor=Path('D:/Xilinx_2020.2/Vitis/2020.2/data/embeddedsw/lib/sw_apps/zynq_fsbl/src')
    require({p.name for p in source.iterdir() if p.is_file()}=={p.name for p in (native/'source').iterdir() if p.is_file()},
            'Frozen FSBL source set differs from generated app')
    for path in (native/'source').iterdir():
        if not path.is_file():continue
        expected=(source/path.name).read_bytes()
        if path.name=='ps7_init.c':expected=original.replace(before,after)
        require(path.read_bytes()==expected,'Frozen source changed: '+path.name)
        if (vendor/path.name).is_file():
            require(path.read_bytes()==(vendor/path.name).read_bytes(),'Non-official FSBL source: '+path.name)
    generated=base/'workspace/sp2_storage_boot/ps7_cortexa9_0/standalone_domain/bsp/ps7_cortexa9_0'
    frozen_files={p.relative_to(native/'bsp').as_posix():p for p in (native/'bsp').rglob('*') if p.is_file()}
    generated_files={p.relative_to(generated).as_posix():p for p in generated.rglob('*') if p.is_file()}
    require(frozen_files.keys()==generated_files.keys(),'Frozen BSP file set differs from generated BSP')
    require(all(p.read_bytes()==generated_files[n].read_bytes() for n,p in frozen_files.items()),'Frozen BSP differs from generated BSP')
    params=(native/'bsp/include/xparameters.h').read_text()
    for value in ('STDOUT_BASEADDRESS 0xE0001000','XPAR_XQSPIPS_0_BASEADDR 0xE000D000',
                  'XPAR_PS7_SD_0_BASEADDR 0xE0100000'):
        require(value in params,'BSP mapping changed: '+value)
    require(re.search(r'^#define FILE_SYSTEM_READ_ONLY\b',params,re.M),'FSBL filesystem is not read-only')
    require(not re.search(r'^#define FILE_SYSTEM_(?:USE_MKFS|FS_EXFAT)\b',params,re.M),'FSBL mkfs/exFAT must be disabled')
    mss=list((base/'workspace/sp2_storage_boot').rglob('*.mss'))
    require(mss and any('LIBRARY_NAME = xilffs' in p.read_text() and 'LIBRARY_VER = 4.4' in p.read_text() for p in mss),
            'Missing generated xilffs 4.4 MSS binding')
    for path in mss:require(path.relative_to(base).as_posix() in tracked,'Generated MSS not frozen')
    commands=json.loads((native/'build/compile_commands.json').read_text())
    expected={p.resolve() for p in (native/'source').iterdir() if p.suffix in ('.c','.S')}
    require(len(commands)==len(expected) and {Path(c['file']).resolve() for c in commands}==expected,'Compiler units differ from frozen source set')
    for row in commands:
        command=row['command'].replace('\\','/')
        for flag in ('-DFSBL_DEBUG_INFO','-O2','-Wall','-Wextra','-Werror','-mcpu=cortex-a9','-mfpu=vfpv3','-mfloat-abi=hard'):
            require(flag in command,'Required FSBL compiler flag missing: '+flag)
        require(str((native/'source').resolve()).replace('\\','/') in command and
                str((native/'bsp/include').resolve()).replace('\\','/') in command,'Actual compiler includes do not bind frozen source/BSP')
    log=(native/'compile-final.log').read_text(errors='replace')
    require(not re.search(r'warning:|error:',log,re.I) and 'fsbl.elf' in log,'FSBL compile warning/error or missing link')
    actual=(native/'actual-commands.log').read_text(errors='replace')
    require('fsbl.elf' in actual and '-lxilffs' in actual and '-lxil' in actual,'Actual FSBL link evidence missing')
    for name,path in manifest['tools'].items():
        require(sha(Path(path).read_bytes())==manifest['tool_sha256'][name],'Build tool identity changed: '+name)
    fsbl_sha=sha((native/'build/fsbl.elf').read_bytes())
    require(fsbl_sha==manifest['fsbl_sha256']==sha((image/'fsbl.elf').read_bytes()),'FSBL binary/source provenance mismatch')
    require(sha((image/'application.elf').read_bytes())==APP_SHA and sha((image/'platform.bit').read_bytes())==BIT_SHA,
            'Frozen application/bit identity mismatch')
    return dict(state='verified-offline-source-BSP-XSA-binding',files=len(rows),xsa_sha256=XSA_SHA,
                fsbl_sha256=fsbl_sha,fsbl_debug_info=True,read_only=True,use_mkfs=False,
                compiler_units=len(commands),ps_init_change='explicit-equivalent-unsigned-long-cast-only')

def audit_run(base,expected_fsbl_sha=FSBL_SHA):
    base=Path(base)
    provenance=check_provenance(base,expected_fsbl_sha)
    image=base/'boot-image'
    report=check_boot((image/'BOOT.bin').read_bytes(),*((image/n).read_bytes() for n in
        ('fsbl.elf','platform.bit','application.elf')),expected_fsbl_sha=provenance['fsbl_sha256'])
    report['provenance']=provenance
    report['manifest_sha256']=sha((base/'manifest.json').read_bytes())
    report['flash_written']=False
    report['sd_written']=False
    return report

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run',type=Path)
    parser.add_argument('--output',type=Path)
    parser.add_argument('--fsbl-sha',default=FSBL_SHA,help='Explicit reviewed FSBL ELF SHA; defaults to this prepared candidate')
    args=parser.parse_args()
    report=audit_run(args.run,args.fsbl_sha)
    if args.output:args.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('M5_BOOT_BINARY_PROVENANCE_AUDIT_PASS',report['bytes'],report['boot_sha256'])
if __name__=='__main__':main()
