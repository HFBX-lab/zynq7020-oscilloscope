"""Decode a frozen DDR framebuffer dump after independently verifying its CRC."""
from pathlib import Path
import argparse,hashlib,json,zlib
from PIL import Image
p=argparse.ArgumentParser()
p.add_argument('binary',type=Path);p.add_argument('crc');p.add_argument('png',type=Path)
a=p.parse_args();data=a.binary.read_bytes()
assert len(data)==1024*600*3, f'Unexpected frame size: {len(data)}'
crc=zlib.crc32(data)&0xffffffff
assert crc==int(a.crc,16),f'Host CRC {crc:08X} differs from board {a.crc}'
a.png.parent.mkdir(parents=True,exist_ok=True)
Image.frombytes('RGB',(1024,600),data,'raw','BGR').save(a.png)
a.png.with_suffix('.json').write_text(json.dumps(dict(source='Zynq DDR dump, not physical LCD photo',
    format='BGR888',width=1024,height=600,crc32=f'{crc:08X}',sha256=hashlib.sha256(data).hexdigest()),indent=2)+'\n')
print(f'PASS: actual DDR framebuffer CRC={crc:08X}; saved {a.png}')
