"""Independent zero-marker geometry contracts and actual evidence mutations."""
import copy
import importlib
import hashlib
from pathlib import Path
import re
import sys
import unittest
from unittest.mock import patch
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
try:
    auditor = importlib.import_module('audit_zero_markers_evidence')
except ModuleNotFoundError:
    auditor = None
from PIL import Image, ImageDraw


def picture(p1=0, p2=0, source='ADC2', dual='1'):
    image = Image.new('RGB', (1024, 600), (12, 17, 27))
    draw = ImageDraw.Draw(image)
    ui = dict(ch1_pos=str(p1), ch2_pos=str(p2), source=source, dual=dual)
    zeros = [463 - (p + 4000) * 326 // 8000 for p in (p1, p2)]
    centers = zeros[:]
    shown = [dual == '1' or source == 'ADC1', dual == '1' or source == 'ADC2']
    if all(shown) and abs(zeros[0] - zeros[1]) < 20:
        first = 0 if zeros[0] <= zeros[1] else 1
        centers[first] = (sum(zeros) - 20) // 2
        centers[1-first] = centers[first] + 20
    for ch, color in enumerate(((64, 224, 192), (255, 200, 87))):
        if not shown[ch]:
            continue
        y = centers[ch]
        draw.rounded_rectangle((2, y-9, 17, y+8), radius=3, fill=color)
        glyph=('010','110','010','010','010','010','111') if ch==0 else ('111','001','001','111','100','100','111')
        for row,bits in enumerate(glyph):
            for column,bit in enumerate(bits):
                if bit=='1':draw.point((8+column,y-4+row),fill=(12,17,27))
        draw.line(((18, y), (21, zeros[ch]), (23, zeros[ch])), fill=color, width=1)
    return image, ui


class GeometryContracts(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(auditor, 'zero marker auditor is not implemented')

    def test_all_layouts_and_single_visibility(self):
        for p1, p2, source, dual in ((0,0,'ADC2','1'),(0,250,'ADC2','1'),
                (250,0,'ADC2','1'),(1500,-1500,'ADC2','1'),
                (3500,-3500,'ADC2','1'),(0,0,'ADC1','0'),(0,0,'ADC2','0')):
            image, ui = picture(p1,p2,source,dual)
            auditor.check_pixels(image, ui)

    def test_misaligned_second_label_rejected(self):
        image, ui = picture()
        patch_image = image.crop((2,301,24,320))
        ImageDraw.Draw(image).rectangle((2,301,23,319), fill=(12,17,27))
        image.paste(patch_image, (3,301))
        with self.assertRaises(AssertionError):
            auditor.check_pixels(image, ui)

    def test_missing_numeric_text_rejected(self):
        image, ui = picture()
        ImageDraw.Draw(image).rectangle((5,284,14,296), fill=(64,224,192))
        with self.assertRaises(AssertionError):
            auditor.check_pixels(image, ui)

    def test_wrong_zero_leader_rejected(self):
        image, ui = picture(1500,-1500)
        ImageDraw.Draw(image).rectangle((21,362,23,362), fill=(12,17,27))
        with self.assertRaises(AssertionError):
            auditor.check_pixels(image, ui)

    def test_broken_diagonal_rejected(self):
        image,ui=picture()
        ImageDraw.Draw(image).rectangle((18,293,21,297),fill=(12,17,27))
        with self.assertRaises(AssertionError):
            auditor.check_pixels(image,ui)

    def test_hidden_channel_mark_rejected(self):
        image, ui = picture(source='ADC1',dual='0')
        ImageDraw.Draw(image).point((10,350),fill=(255,200,87))
        with self.assertRaises(AssertionError):
            auditor.check_pixels(image,ui)

    def test_old_actual_frame_is_rejected(self):
        path=Path('D:/FPGA_Project/SP2_ZeroMarkers_Build/user-backup/20261025-155536-6364/before-frame.bin')
        if not path.exists():
            self.skipTest('historical frame unavailable')
        image=Image.frombytes('RGB',(1024,600),path.read_bytes(),'raw','BGR')
        with self.assertRaises(AssertionError):
            auditor.check_pixels(image,dict(ch1_pos='500',ch2_pos='-1000',source='ADC2',dual='1'))


def actual_negatives(directory):
    original=auditor.load(directory/'session.json')
    baseline=auditor.audit(directory)
    count=0
    def reject(mutator):
        nonlocal count
        changed=copy.deepcopy(original);mutator(changed)
        with patch.object(auditor,'load',return_value=changed):
            try:
                auditor.audit(directory)
            except (AssertionError,ValueError,KeyError,IndexError):
                count+=1;return
        raise AssertionError('mutated actual evidence accepted')
    reject(lambda s:s['zero_marker_records'].pop())
    reject(lambda s:s['zero_marker_records'][0].update(frame_crc='00000000'))
    reject(lambda s:s['zero_marker_records'][0].update(frame_sha256='0'*64))
    reject(lambda s:s['zero_marker_records'][0].update(crc32='00000000'))
    reject(lambda s:s['zero_marker_records'][0].update(sha256='0'*64))
    reject(lambda s:s['zero_marker_records'][0]['snapshot']['ui'].update(epoch='999999'))
    reject(lambda s:s['zero_marker_records'][0]['snapshot']['parameter']['status'].update(blob_crc='00000000'))
    reject(lambda s:s['zero_marker_records'][0]['snapshot']['parameter']['status'].update(wave_crc='00000000'))
    reject(lambda s:s['zero_marker_records'][0]['snapshot']['memory']['view'].update(rate='50000000'))
    reject(lambda s:s['zero_marker_records'][0]['ui'].update(ch1_pos='1000'))
    reject(lambda s:s['zero_marker_baseline']['ui'].update(single_complete='0'))
    reject(lambda s:s['zero_marker_final']['ui'].update(running='0'))
    read=Path.read_bytes
    for suffix in ('.bin','-frame.bin'):
        target=directory/('zero-overlap'+suffix)
        def changed_read(path,_target=target):
            data=read(path)
            return bytes((data[0]^1,))+data[1:] if path==_target else data
        with patch.object(Path,'read_bytes',changed_read):
            try:auditor.audit(directory)
            except AssertionError:count+=1
            else:raise AssertionError('changed actual binary accepted')
    # A self-consistent frame CRC/SHA and UART re-signing must still fail geometry.
    changed=copy.deepcopy(original)
    frame_path=directory/'zero-overlap-frame.bin'
    altered_image=Image.frombytes('RGB',(1024,600),read(frame_path),'raw','BGR')
    ImageDraw.Draw(altered_image).rectangle((2,281,17,298),fill=(12,17,27))
    altered_frame=altered_image.tobytes('raw','BGR')
    frame_entry=changed['zero_marker_records'][0]
    old_crc=frame_entry['frame_crc'];new_crc=f'{zlib.crc32(altered_frame):08X}'
    frame_entry.update(frame_crc=new_crc,frame_sha256=hashlib.sha256(altered_frame).hexdigest().upper())
    uart_path=directory/'uart.log'
    old_log=read(uart_path)
    modified_log=re.sub(rb'(?m)^(SNAPSHOT_READY [^\r\n]*\bcrc=)'+old_crc.encode()+rb'\b',
                        lambda match:match[1]+new_crc.lower().encode(),old_log,flags=re.I)
    assert modified_log!=old_log
    changed['uart_sha256']=hashlib.sha256(modified_log).hexdigest().upper()
    def resigned_read(path):
        if path==frame_path:return altered_frame
        if path==uart_path:return modified_log
        return read(path)
    with patch.object(auditor,'load',return_value=changed),patch.object(Path,'read_bytes',resigned_read):
        try:auditor.audit(directory)
        except AssertionError as error:
            assert 'missing filled channel badge' in str(error),('wrong detection for resigned frame',error)
            count+=1
        else:raise AssertionError('self-consistent wrong marker pixels accepted')
    read_text=Path.read_text
    tcl_path=directory/'export-zero-overlap.tcl'
    def altered_tcl(path,*args,**kwargs):
        text=read_text(path,*args,**kwargs)
        return text.replace(' 460800',' 460799') if path==tcl_path else text
    with patch.object(Path,'read_text',altered_tcl):
        try:auditor.audit(directory)
        except AssertionError:count+=1
        else:raise AssertionError('changed actual export range accepted')
    print('ZERO_MARKER_NEGATIVES_PASS',count,baseline['frames'])


if __name__=='__main__':
    if len(sys.argv)>1 and Path(sys.argv[1]).is_dir():
        actual_negatives(Path(sys.argv[1]));sys.exit(0)
    unittest.main()
