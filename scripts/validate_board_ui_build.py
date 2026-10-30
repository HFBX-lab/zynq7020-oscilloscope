"""Offline ARM LVGL build/provenance checks. Never connects to hardware."""
from pathlib import Path
import hashlib,json,struct,sys,subprocess

root=Path(__file__).resolve().parents[1]
mode=sys.argv[1] if len(sys.argv)>1 else 'standalone'
assert mode in ('standalone','freertos')
base=root/'build/board-lvgl'/mode
s=json.loads((base/'build-status.json').read_text(encoding='utf-8-sig'))
assert s['state']=='succeeded'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest().upper()
assert s['xsa_sha256']==sha(root/'build/ps-screen/sp2_ps_screen.xsa')
for a in s['artifacts']:assert sha(base/a['file'])==a['sha256']
run=Path(s['run'])
for item in s['source_hashes']:
    p=Path(item['Path']);assert sha(p)==item['Hash']
    relative=p.relative_to(run)
    kind=relative.parts[0];tail=Path(*relative.parts[1:])
    if kind=='board' and tail.name=='lscript.ld':continue
    if kind=='board':original=root/'firmware/lvgl'/tail if tail.name!='Xilinx.spec' else root/'firmware/Xilinx.spec'
    elif kind=='ui':original=root/'src'/tail
    elif kind=='legacy':original=root/'firmware/standalone'/tail
    else:original=root/'third_party/lvgl-9.2.2'/tail
    assert sha(original)==item['Hash'],f'Build source changed: {original}'
elf=base/f'scope_lvgl_{mode}.elf';data=elf.read_bytes()
assert data[:6]==b'\x7fELF\x01\x01' and struct.unpack_from('<H',data,18)[0]==40
entry,offset=struct.unpack_from('<II',data,24);size,count=struct.unpack_from('<HH',data,42)
assert entry==0x100000
segments=[]
for i in range(count):
    kind,off,virt,phys,fs,ms,flags,align=struct.unpack_from('<8I',data,offset+i*size)
    if kind==1:
        assert phys==virt and 0x100000<=virt and virt+ms<=0x1000000
        segments.append([hex(virt),hex(virt+ms)])
assert segments and b'LVGL_READY os=' in data
linker=(base/'lscript.ld').read_text()
assert '_HEAP_SIZE = 0x400000;' in linker and '_STACK_SIZE = 0x20000;' in linker
if mode=='freertos':
    config=(base/'FreeRTOSConfig.h').read_text()
    for macro,value in [('configTICK_RATE_HZ','1000'),('configTOTAL_HEAP_SIZE','1048576'),
                        ('configUSE_TASK_FPU_SUPPORT','2'),('configUSE_NEWLIB_REENTRANT','1')]:
        line=next(l for l in config.splitlines() if l.startswith('#define '+macro+' '))
        assert value in line,(macro,line)
    nm=Path('D:/Xilinx_2020.2/Vitis/2020.2/gnu/aarch32/nt/gcc-arm-none-eabi/bin/arm-none-eabi-nm.exe')
    symbols=subprocess.check_output([str(nm),str(elf)],text=True)
    for name in ['vTaskStartScheduler','xPortStartScheduler','__malloc_lock','__malloc_unlock','lv_thread_init']:
        assert any(l.endswith(' '+name) and ' U ' not in l for l in symbols.splitlines()),name
result=dict(state='verified-offline',execution='not-assessed',os=mode,elf_sha256=sha(elf),xsa_sha256=s['xsa_sha256'],
            load_segments=segments,draw_buffer=['0x12000000','0x12258000'],frame_slots=['0x10000000','0x10200000'])
(base/'offline-validation.json').write_text(json.dumps(result,indent=2)+'\n')
print(f'PASS: {mode} ARM ELF and current source/XSA hashes; LOAD {segments}; separate 4MiB newlib heap, frame/draw buffers')
