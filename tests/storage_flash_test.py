from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from prepare_storage_flash import build_payload,CAPACITY,ERASE
from verify_storage_flash import check_bytes
def reject(*args):
    try:build_payload(*args)
    except ValueError:return
    raise AssertionError('unsafe flash inputs accepted')
before=bytes(range(256))*(CAPACITY//256)
for n in (4097,ERASE,ERASE+1,4936272,CAPACITY):
    boot=b'B'*n;payload,expected=build_payload(before,before,boot)
    aligned=(n+ERASE-1)//ERASE*ERASE
    assert len(payload)==aligned and payload[:n]==boot and payload[n:]==before[n:aligned]
    assert len(expected)==CAPACITY and expected[:n]==boot and expected[n:]==before[n:]
    check_bytes(before,boot,expected)
reject(before,before,b'');reject(before,before,b'B'*(CAPACITY+1));reject(before[:-1],before,b'B'*4097);reject(before,before[:-1]+b'X',b'B'*4097)
boot=b'B'*4936272;_,expected=build_payload(before,before,boot)
for modified in (expected[:-1],expected[:100]+b'X'+expected[101:],expected[:len(boot)]+b'X'+expected[len(boot)+1:]):
    try:check_bytes(before,boot,modified)
    except ValueError:pass
    else:raise AssertionError('corrupted full readback accepted')
print('STORAGE_FLASH_PAYLOAD_PASS boundaries/alignment/tail/afterBOOT/twoBackups/capacity')
