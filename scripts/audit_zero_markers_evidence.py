"""Read-only zero-reference badge geometry and frozen-record evidence audit."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re

from PIL import Image
from audit_hardware_trigger_evidence import load, checked_bytes
from audit_memory_evidence import _bind, _log_index, equal_fields, check_epoch
from audit_settings_evidence import _snapshot, _held, _same_saved, _acq, _parameter

COLORS=((64,224,192),(255,200,87))
TEXT=(12,17,27)
CASES=(('zero-overlap',0,0,'ADC2','1','zh-CN'),
       ('zero-near-ch2-above',0,250,'ADC2','1','zh-CN'),
       ('zero-near-ch1-above',250,0,'ADC2','1','zh-CN'),
       ('zero-separated',1500,-1500,'ADC2','1','zh-CN'),
       ('zero-limits',3500,-3500,'ADC2','1','zh-CN'),
       ('zero-single-ch1',0,0,'ADC1','0','zh-CN'),
       ('zero-single-ch2',0,0,'ADC2','0','zh-CN'),
       ('zero-overlap-en',0,0,'ADC2','1','en'))


def _tinted(pixel,rgb):
    # Thin diagonal LVGL lines are antialiased against the dark gutter.
    channel=max(range(3),key=lambda index:abs(rgb[index]-TEXT[index]))
    alpha=(pixel[channel]-TEXT[channel])/(rgb[channel]-TEXT[channel])
    return alpha>=.35 and all(abs(pixel[index]-(TEXT[index]+alpha*(rgb[index]-TEXT[index])))<=4
                             for index in range(3))


def check_pixels(image,ui):
    """Measure actual coloured badge bounds, numeral ink and reference leaders."""
    assert image.size==(1024,600) and image.mode=='RGB'
    positions=[int(ui['ch1_pos']),int(ui['ch2_pos'])]
    assert all(-3500<=value<=3500 for value in positions)
    zeros=[463-(value+4000)*326//8000 for value in positions]
    shown=[ui['dual']=='1' or ui['source']=='ADC1',
           ui['dual']=='1' or ui['source']=='ADC2']
    centers=zeros[:]
    if all(shown) and abs(zeros[0]-zeros[1])<20:
        first=0 if zeros[0]<=zeros[1] else 1
        centers[first]=(sum(zeros)-20)//2
        centers[1-first]=centers[first]+20
    reports=[]
    for ch,rgb in enumerate(COLORS):
        colored=[(x,y) for x in range(2,18) for y in range(138,463)
                 if image.getpixel((x,y))==rgb]
        if not shown[ch]:
            assert not colored,('hidden channel badge still visible',ch+1)
            assert not any(_tinted(image.getpixel((x,y)),rgb) for x in range(18,24)
                           for y in range(138,463)),('hidden leader visible',ch+1)
            reports.append(dict(channel=ch+1,visible=False));continue
        assert len(colored)>=180,('missing filled channel badge',ch+1,len(colored))
        xs,ys=zip(*colored)
        expected=(2,centers[ch]-9,17,centers[ch]+8)
        measured=(min(xs),min(ys),max(xs),max(ys))
        assert measured==expected,('badge bounds/alignment',ch+1,measured,expected)
        # Restrict to the interior; the screen background equals the numeral ink.
        ink=[(x,y) for x in range(5,15) for y in range(centers[ch]-6,centers[ch]+7)
             if sum(abs(a-b) for a,b in zip(image.getpixel((x,y)),TEXT))<=180]
        assert len(ink)>=5,('missing numeric ink',ch+1)
        ink_x,ink_y=zip(*ink)
        assert abs((min(ink_x)+max(ink_x))/2-9.5)<=2,('numeric ink is not horizontally centered',ch+1)
        assert abs((min(ink_y)+max(ink_y))/2-centers[ch])<=3,('numeric ink is not vertically centered',ch+1)
        assert any(_tinted(image.getpixel((x,y)),rgb) for x in range(18,20)
                   for y in range(centers[ch]-1,centers[ch]+2)),('leader missing at badge',ch+1)
        endpoint_colors=COLORS if all(shown) and zeros[0]==zeros[1] else (rgb,)
        assert image.getpixel((22,zeros[ch])) in endpoint_colors,('leader does not reach actual zero',ch+1,zeros[ch])
        if centers[ch]!=zeros[ch]:
            for y in range(min(centers[ch],zeros[ch])+1,max(centers[ch],zeros[ch])):
                x=18+3*(y-centers[ch])/(zeros[ch]-centers[ch])
                assert any(_tinted(image.getpixel((column,y)),rgb) for column in {math.floor(x),math.ceil(x)}),('broken diagonal leader',ch+1,y)
        else:
            assert all(_tinted(image.getpixel((x,zeros[ch])),rgb) for x in range(18,23)),('broken horizontal leader',ch+1)
        reports.append(dict(channel=ch+1,visible=True,bounds=measured,
                            actual_zero_y=zeros[ch],label_center_y=centers[ch],
                            colored_pixels=len(colored),numeric_ink_pixels=len(ink)))
    return reports


def _export_addresses(directory,name,snapshot,index,crc):
    body=(directory/('export-'+name+'.tcl')).read_text(encoding='utf-8-sig')
    for suffix,words in (('.bin',4096),('-frame.bin',460800)):
        matches=re.findall(r'mrd -bin -file \{[^{}\r\n]*[/\\]'+re.escape(name+suffix)
                           +r'\} 0x([0-9A-Fa-f]{8}) (\d+)',body)
        assert len(matches)==1 and int(matches[0][1])==words,('unexpected export range',name,suffix)
        address=matches[0][0].lower()
        if suffix=='.bin':
            assert address==snapshot['memory']['view']['record_address'].lower()
        else:
            assert address in ('10000000','10200000')
            assert any(dict(fields).get('addr','').lower()==address
                       and dict(fields).get('crc','').upper()==crc.upper()
                       and dict(fields).get('bytes')=='1843200'
                       and dict(fields).get('format')=='BGR888'
                       for fields in index.get('SNAPSHOT_READY',())), 'frame is absent from UART'


def audit(directory):
    directory=Path(directory)
    session=load(directory/'session.json')
    assert session['state'].startswith('verified-board-') and session['flash_written'] is False
    entries=session['zero_marker_records']
    assert [entry['name'] for entry in entries]==[case[0] for case in CASES]
    assert [entry['test'] for entry in session['zero_marker_checks']]==[case[0] for case in CASES]
    data=(directory/'uart.log').read_bytes()
    assert hashlib.sha256(data).hexdigest().upper()==session['uart_sha256'].upper()
    log=data.decode('utf-8-sig')
    assert not re.search(r'APP_FATAL|APP_LOG_ERROR|ADC_LIVE_FAIL|DDR_FAIL|VIDEO_PRESENT_FAIL|VIDEO_STOP_TIMEOUT',log)
    index=_log_index(log)
    base=_snapshot(session['zero_marker_baseline'],index)
    assert base['ui']['single_complete']=='1' and base['ui']['running']=='0'
    assert base['parameter']['status']['valid']==base['parameter']['status']['wave_valid']=='1'
    reports=[];raws=[]
    for entry,case,check in zip(entries,CASES,session['zero_marker_checks']):
        name,p1,p2,source,dual,language=case
        snapshot=_snapshot(entry['snapshot'],index)
        assert snapshot==check['snapshot'],'export/check snapshot mismatch'
        _held(base,snapshot,0);_same_saved(base,snapshot)
        ui=snapshot['ui']
        assert ui['source']==source and ui['dual']==dual and ui['lang']==language
        assert (int(ui['ch1_pos']),int(ui['ch2_pos']))==(p1,p2)
        assert ui['ch1_scale']==ui['ch2_scale']=='2'
        assert ui['single_complete']=='1' and ui['single_armed']=='0'
        _acq(entry,index)
        equal_fields(entry['ui'],ui,('time','source','dual','lang','ch1_scale','ch2_scale','ch1_pos','ch2_pos','epoch','display_id','seen_id','running','single_complete'))
        assert entry['id']==ui['display_id'] and str(entry['rate'])==snapshot['memory']['view']['rate']
        assert entry['crc32'].upper()==base['parameter']['status']['record_crc'].upper()
        raw=checked_bytes(directory/(name+'.bin'),entry['crc32'],entry['sha256'],16384)
        raws.append(raw)
        frame=checked_bytes(directory/(name+'-frame.bin'),entry['frame_crc'],entry['frame_sha256'],1843200)
        _export_addresses(directory,name,snapshot,index,entry['frame_crc'])
        image=Image.frombytes('RGB',(1024,600),frame,'raw','BGR')
        measured=check_pixels(image,ui)
        reports.append(dict(name=name,markers=measured,raw_sha256=entry['sha256'],frame_sha256=entry['frame_sha256']))
    assert all(raw==raws[0] for raw in raws),'marker layout changed frozen raw bytes'
    final=session['zero_marker_final'];_acq(final,index);_parameter(final['parameter'],index)
    ui=final['ui']
    assert ui['running']=='1' and ui['source']=='ADC2' and ui['lang']=='zh-CN' and ui['dual']=='1'
    assert ui['time']=='6' and ui['cursors']=='0' and ui['trig_normal']=='0'
    assert final['adc']['display_epoch']==ui['epoch'] and final['adc']['display_rate']=='6250000'
    assert int(ui['display_id'])>int(base['ui']['display_id'])
    check_epoch(base['ui'],ui,1)
    equal_fields(base['parameter']['status'],final['parameter']['status'],('valid','blob_crc','wave_valid','wave_crc'))
    return dict(state='verified-independent-zero-marker-evidence',frames=8,raws=8,
                persistent_storage='RAM-only-no-SD-or-Flash',records=reports)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run',type=Path)
    args=parser.parse_args();result=audit(args.run)
    (args.run/'zero-markers-evidence-audit.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print('ZERO_MARKERS_EVIDENCE_PASS',json.dumps(result))


if __name__=='__main__':
    main()
