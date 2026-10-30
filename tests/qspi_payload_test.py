from pathlib import Path
import sys,unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from prepare_qspi_payload import make_payload
class Payload(unittest.TestCase):
    def test_preserve_partial_last_sector_and_rest(self):
        old=bytes(range(256))*65536;boot=b'NEWBOOT'*10001
        payload=make_payload(old,boot)
        self.assertEqual(len(payload)%65536,0)
        self.assertEqual(payload[:len(boot)],boot)
        self.assertEqual(payload[len(boot):],old[len(boot):len(payload)])
        self.assertEqual(payload+old[len(payload):],boot+old[len(boot):])
    def test_aligned_and_full_capacity(self):
        old=b'\x5a'*(16*1024*1024)
        self.assertEqual(len(make_payload(old,b'x'*65536)),65536)
        self.assertEqual(make_payload(old,b'y'*len(old)),b'y'*len(old))
    def test_reject_invalid_extents(self):
        old=b'\0'*(16*1024*1024)
        for backup,boot in ((old,b''),(old,b'x'*(len(old)+1)),(old[:-1],b'x')):
            with self.assertRaises(ValueError):make_payload(backup,boot)
if __name__=='__main__':unittest.main()
