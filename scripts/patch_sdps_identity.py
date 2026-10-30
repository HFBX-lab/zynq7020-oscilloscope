"""Minimal, source-pinned sdps 3.10 identity diagnostic fix in frozen copies only.

CLI: python scripts/patch_sdps_identity.py <frozen-xsdps_card.c>
Idempotent. Emits JSON SHA provenance; never changes the installed vendor tree.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

VENDOR_SHA256='382c209e485c2dace8d0532fd102768fb3811c3f8d418972161a249bb942eb9c'
FROZEN_ROOT=Path('D:/FPGA_Project/SP2_Storage_Build')
MARKER=b'/* SP2: preserve four 32-bit CSD response words for diagnostics. */'

def digest(data):return hashlib.sha256(data).hexdigest()

def cid_replace(data,old,new):
    pattern=rb'(InstancePtr->CardID\[([0-3])\]\s*=\s*)'+old+rb'\('
    result,count=re.subn(pattern,lambda m:m[1]+new+b'(',data)
    if count!=4:raise ValueError('Expected exactly four CardID response reads')
    return result

def cache_block(newline):
    return newline+b'\t'+MARKER+newline+b''.join(b'\tInstancePtr->CardSpecData['+str(i).encode()+b'] = CSD['+str(i).encode()+b'];'+newline for i in range(4))

def patch_bytes(data):
    if digest(data)==VENDOR_SHA256:
        newline=b'\r\n' if b'\r\n' in data else b'\n'
        result=cid_replace(data,b'XSdPs_ReadReg16',b'XSdPs_ReadReg')
        pattern=rb'CSD\[3\] = XSdPs_ReadReg\(InstancePtr->Config.BaseAddress,\s*XSDPS_RESP3_OFFSET\);'
        matches=list(re.finditer(pattern,result))
        if len(matches)!=1:raise ValueError('Expected exactly one four-word CSD read')
        at=matches[0].end();return result[:at]+cache_block(newline)+result[at:]
    if data.count(MARKER)==1:
        newline=b'\r\n' if b'\r\n' in data else b'\n'
        block=cache_block(newline)
        if data.count(block)!=1:raise ValueError('Unknown identity patch shape')
        original=cid_replace(data.replace(block,b''),b'XSdPs_ReadReg',b'XSdPs_ReadReg16')
        if digest(original)==VENDOR_SHA256:return data
    raise ValueError('Unknown sdps source hash; expected pinned 2020.2 sdps_v3_10 or this exact patch')

def patch_file(target):
    target=Path(target).resolve();root=FROZEN_ROOT.resolve()
    if target.name!='xsdps_card.c' or not target.is_relative_to(root):
        raise ValueError('Only frozen xsdps_card.c copies under '+str(root)+' may be patched')
    original=target.read_bytes();patched=patch_bytes(original)
    changed=original!=patched
    if changed:
        backup=target.with_suffix('.c.vendor-original')
        if backup.exists() and digest(backup.read_bytes())!=VENDOR_SHA256:raise ValueError('Existing vendor-original backup hash mismatch')
        if not backup.exists():backup.write_bytes(original)
        target.write_bytes(patched)
    return dict(state='patched' if changed else 'already-patched',path=str(target),vendor_sha256=VENDOR_SHA256,before_sha256=digest(original),patched_sha256=digest(patched),cardid_bits=32,csd_cached_words=4)

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('source',type=Path);args=parser.parse_args()
    print(json.dumps(patch_file(args.source),indent=2))
if __name__=='__main__':main()
