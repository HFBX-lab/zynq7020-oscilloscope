"""Verify a 16KiB internal-test record against UART CRC and digital sequence."""
from pathlib import Path
import argparse,hashlib,json,struct,zlib

p=argparse.ArgumentParser()
p.add_argument('binary',type=Path);p.add_argument('crc32');p.add_argument('--origin',choices=['host-fixture','board-ddr'],required=True)
a=p.parse_args();data=a.binary.read_bytes()
assert len(data)==16384,'Record size must be 4096 little-endian words'
crc=zlib.crc32(data)&0xffffffff
assert crc==int(a.crc32,16),f'Independent CRC mismatch: {crc:08x}'
words=struct.unpack('<4096I',data)
first=(words[0]&1023)|((((words[0]>>16)&1023)^(1023-(words[0]&1023)))<<10)
for i,word in enumerate(words):
    n=(first+i)&0xfffff;low=n&1023;high=(1023-low)^((n>>10)&1023)
    expected=low|(((n>>2)&1)<<10)|(high<<16)|(((n>>3)&1)<<26)
    assert word==expected,f'Nonconsecutive/invalid digital word {i}: {word:08x} != {expected:08x}'
result=dict(source=a.origin,mode='internal DIGITAL test, not real ADC',words=4096,first_sequence=first,
            crc32=f'{crc:08X}',sha256=hashlib.sha256(data).hexdigest().upper())
a.binary.with_suffix('.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
print('ADC_RECORD_HOST_PASS: '+json.dumps(result))
