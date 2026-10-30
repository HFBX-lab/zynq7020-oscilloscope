"""Read-only, independent M5 board-evidence audit. No device APIs or subprocesses.

A pass covers the recorded file transactions only. Physical UI acceptance and
an actual power-cycle remain separate, explicitly unverified requirements.
"""
import argparse
import ast
import hashlib
import json
from pathlib import Path
import re
import struct
import zlib

from record_blob import parse_record
from settings_blob import parse_settings

ELF_SHA = '6917E229BE48D990EB7255ADA4E5BDDD052CEBF4B6E97BABD105C69C4B9C5A4C'
# The accepted bitstream and SD-only PS init are pinned independently of manifest text.
BIT_SHA = 'BE748A2A543047688010B3FD6179CDA517EB5EDBA76A27962D62EFBCA90A3FBE'
PSINIT_SHA = 'F6D0B6A94B3EF33CE3C2542277D5B76179FBD2A00AD0C5A63CDCD2F0FF7BDEC4'
CAL_ID = '2b474854'
CAL_BITS = dict(slope1_bits='3c19e024',offset1_bits='c0976997',
                slope2_bits='3c158529',offset2_bits='c094a49b')
CID = '00000000000000000000000000000000'
PASS_STATE = 'verified-independent-board-M5-evidence-awaiting-physical-and-power-cycle'
HELPERS = ('acq_ui_session_helpers','long_timebase_session_helpers',
           'measurement_session_helpers','memory_session_helpers',
           'settings_session_helpers','spectrum_session_helpers',
           'calibration_session_helpers','storage_session_helpers')
PYTHON_FILES = ('record_blob.py','settings_blob.py','analyze_adc_live_record.py',
                'decode_board_frame.py','siglent_sdg.py','siglent_output.py')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def same(a, b):
    return str(a).lower() == str(b).lower()


def matches(actual, expected):
    return all(key in actual and same(actual[key], value) for key,value in expected.items())


def read_json(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def digest(data):
    return hashlib.sha256(data).hexdigest().upper()


def inside(run, name):
    path = Path(name)
    if not path.is_absolute():
        path = run / path
    path = path.resolve()
    require(path.is_relative_to(run.resolve()) and path != run.resolve(),
            f'Path escapes evidence run: {name}')
    require(path.is_file(), f'Missing evidence file: {path.name}')
    return path


class UART:
    """Parse raw UART fields; never accept duplicate keys or JSON-only claims."""
    def __init__(self, text):
        require(isinstance(text,str) and bool(text.strip()),'Missing UART log')
        require(not re.search(r'APP_FATAL|APP_LOG_ERROR|ADC_LIVE_FAIL|DDR_FAIL|VIDEO_PRESENT_FAIL|VIDEO_STOP_TIMEOUT',text),
                'UART contains application failure')
        self.text = text
        self.rows = []
        for index,line in enumerate(text.splitlines()):
            tag=line.split(' ',1)[0]
            pairs=re.findall(r'(\w+)=([^\s]+)',line)
            require(len(dict(pairs))==len(pairs),f'Duplicate UART field: {tag}')
            self.rows.append((index,tag,dict(pairs),line))

    def row(self, tag, expected):
        require(isinstance(expected,dict) and bool(expected),f'Empty asserted {tag}')
        found=[row for row in self.rows if row[1]==tag and matches(row[2],expected)]
        require(bool(found),f'Missing raw UART {tag} field binding')
        return found

    def group(self, expected):
        """Match an ordered complete transaction, excluding unrelated diagnostics."""
        tags={tag for tag,_ in expected}
        rows=[r for r in self.rows if r[1] in tags]
        found=[]
        for i in range(len(rows)-len(expected)+1):
            part=rows[i:i+len(expected)]
            if all(r[1]==tag and matches(r[2],values) for r,(tag,values) in zip(part,expected)):
                found.append(part)
        require(bool(found),'Missing atomic UART transaction: '+expected[0][0])
        return found


def audit_package(run, state):
    entries=state.get('package')
    require(isinstance(entries,list) and bool(entries),'Missing package manifest')
    paths={}
    for item in entries:
        path=inside(run,item['Path'])
        name=path.name.lower()
        require(name not in paths,'Duplicate package file')
        require(path.parent==run.resolve(),'Package source must be in run root')
        raw=path.read_bytes()
        require(re.fullmatch('[0-9a-fA-F]{64}',item['Hash']) is not None and
                digest(raw)==item['Hash'].upper(),f'Package hash changed: {name}')
        paths[name]=path
    needed={'application.elf','platform.bit','ps7_init.tcl','board_init_helpers.tcl',
            'validation.json','runner.ps1','with_owned_hw_server.ps1'}
    needed.update(name+'.ps1' for name in HELPERS)
    needed.update(PYTHON_FILES)
    require(needed <= paths.keys(),'Incomplete package: '+','.join(sorted(needed-paths.keys())))
    require(same(state.get('elf_sha256'),ELF_SHA),'Session ELF is not reviewed M5 candidate')
    for name,expected in [('application.elf',ELF_SHA),('platform.bit',BIT_SHA),('ps7_init.tcl',PSINIT_SHA)]:
        require(digest(paths[name].read_bytes())==expected,f'Pinned package identity: {name}')
    require(paths['application.elf'].read_bytes()[:4]==b'\x7fELF','Package ELF magic')
    for name in PYTHON_FILES:
        tree=ast.parse(paths[name].read_text(encoding='utf-8-sig'))
        require(len(list(ast.walk(tree)))>40,f'Placeholder package Python: {name}')
    for name in needed:
        if name.endswith(('.ps1','.tcl')):
            require(paths[name].stat().st_size>=100,f'Placeholder package script: {name}')
    proof=read_json(paths['validation.json'])
    require(proof.get('state')=='verified-offline' and proof.get('tf_storage') is True,
            'Missing offline storage validation')
    require(same(proof.get('elf_sha256'),ELF_SHA) and same(proof.get('bit_sha256'),BIT_SHA)
            and same(proof.get('ps7_init_sha256'),PSINIT_SHA),'Validation/package mismatch')
    require(same(proof.get('calibration_id'),CAL_ID) and proof.get('calibration_flags')==259,
            'Validation calibration identity')
    require(bool(proof.get('load_segments')),'Missing ELF range evidence')
    return proof


def audit_tf_actions(state, uart):
    actions=state.get('tf_actions',[])
    first=int(state.get('first_wave_id',0));param=int(state.get('parameter_id',0))
    require(first>0 and param>0,'Missing file identifiers')
    expected=['tfsave 0','tfsave 1']+['tfsave 0']*6+['tfsave 0',
              f'tfload 0 {first}',f'tfload 1 {param}','tfload 0 9999999','tfopen 0','tflist 0 1']
    require([a.get('command') for a in actions]==expected,'TF action coverage/order incomplete')
    expected_acks=[]
    for action in actions:
        command='tfopen' if action['command'].startswith('tfopen') else action['command']
        expected_acks.append(f"TF_COMMAND {command} QUEUED seq={action['sequence']}")
    actual_acks=[row[3] for row in uart.rows if row[1]=='TF_COMMAND']
    require(actual_acks==expected_acks,
            'Raw UART TF command/sequence coverage differs from manifest: extra, missing, duplicate or reordered ACK')
    previous=0;previous_uart=-1;wave_ids=[]
    for action in actions:
        command=action['command'];seq=int(action['sequence']);reply=action['status']
        status,media,blob=(reply[key] for key in ('status','media','blob'))
        require(seq>previous and str(seq)==str(status.get('seq')),'TF sequence mismatch/order')
        previous=seq
        require(matches(status,dict(busy='0',ready='1',kind=command.split()[1])), 'TF final flags/kind')
        result='7' if command=='tfload 0 9999999' else '0'
        require(same(status.get('result'),result),'TF unexpected result')
        require(matches(media,dict(cid=CID,fat='3',sector_count='61067264')),'TF media identity mismatch')
        size=int(blob['bytes'])
        require(re.fullmatch('[0-9a-fA-F]{8}',blob['address']) is not None and
                re.fullmatch('[0-9a-fA-F]{8}',blob['crc']) is not None,'TF blob malformed hex')
        if command.startswith(('tfsave','tfload')) and result=='0':
            require(size==(16512 if command.split()[1]=='0' else 96) and int(blob['address'],16)>0,
                    'TF successful blob absent/size')
        else:
            require(size==0 and int(blob['address'],16)==0 and int(blob['crc'],16)==0,
                    'TF failed/list action exposed nonempty blob')
        if command=='tfsave 0':
            wave_ids.append(int(status['id']))
            if len(wave_ids)<=7:
                require(matches(blob,actions[0]['status']['blob']),
                        'Frozen seven-file saves changed snapshot address/length/CRC')
        if command=='tfsave 1' or command.startswith('tfload'):
            require(int(status['id'])==int(command.split()[2] if command.startswith('tfload') else param),
                    'TF save/load file ID mismatch')
        ack_command='tfopen' if command.startswith('tfopen') else command
        acks=[r for r in uart.rows if r[3]==f'TF_COMMAND {ack_command} QUEUED seq={seq}']
        require(len(acks)==1,'TF command acknowledgement missing/duplicate/full-command mismatch')
        frames=uart.group([('TF_STATUS',status),('TF_MEDIA',media),('TF_BLOB',blob)])
        matching=[part for part in frames if part[0][0]>acks[0][0] and acks[0][0]>previous_uart]
        require(bool(matching),'TF transaction precedes command or crosses action ordering')
        frame=matching[0]
        following=[r for r in uart.rows if acks[0][0]<r[0]<=frame[-1][0] and r[1]=='TF_COMMAND']
        require(not following,'TF status borrowed across another command acknowledgement')
        previous_uart=frame[-1][0]
    require(wave_ids==list(range(first,first+8)),'TF wave IDs not monotonically new')
    return dict(action_count=len(actions),wave_save_count=len(wave_ids),first_wave_id=first,
                last_wave_id=wave_ids[-1],cid=CID)


def audit_export(run, entry, uart, segments):
    name=entry['name'];size=int(entry['bytes']);address=entry['address'];crc=entry['crc']
    require(re.fullmatch('[a-z0-9-]+',name) is not None,'Unsafe export name')
    require(re.fullmatch('[0-9a-fA-F]{8}',address) is not None and size>0 and size%4==0,
            'Invalid export range')
    require(re.fullmatch('[0-9a-fA-F]{8}',crc) is not None,'Invalid export CRC')
    raw=inside(run,name+'.bin').read_bytes()
    require(len(raw)==size,f'Export exact length differs: {name}')
    require(f'{zlib.crc32(raw):08x}'==crc.lower(),f'Export CRC differs: {name}')
    require(digest(raw)==entry['sha256'].upper(),f'Export SHA differs: {name}')
    start=int(address,16)
    if entry.get('frame'):
        require(name.endswith('-frame') and size==1843200 and address.lower() in ('10000000','10200000'),
                'Frame export range/dimensions')
        uart.row('SNAPSHOT_READY',dict(addr=address,bytes=str(size),crc=crc,format='BGR888'))
        marker='STORAGE_FRAME_EXPORTED'
        png=inside(run,name+'.png').read_bytes()
        require(png.startswith(b'\x89PNG\r\n\x1a\n') and png[12:16]==b'IHDR'
                and struct.unpack_from('>II',png,16)==(1024,600),'Missing valid 1024x600 frame PNG')
    else:
        if entry.get('ddr'):
            require(address.lower() in ('13000000','13004000') and size==16384,'Invalid live DDR export')
        else:
            require(any(start>=int(lo,16) and start+size<=int(hi,16) for lo,hi in segments),
                    'Export outside audited ELF load segments')
        bindings=[]
        for _,tag,fields,_ in uart.rows:
            choices={'TF_BLOB':('address','bytes','crc'),'MEMORY_VIEW':('record_address','record_bytes','record_crc'),
                     'ADC_LIVE_STATUS':('display_address',None,'display_crc')}
            if tag in choices:
                a,b,c=choices[tag]
                if matches(fields,{a:address,c:crc}) and (same(fields.get(b),size) if b else
                        size==16384 and str(fields.get('bytes'))=='16384'):
                    bindings.append(fields)
        require(bool(bindings),'Export lacks same-line UART address/length/CRC source')
        marker='STORAGE_BYTES_EXPORTED'
    tcl=inside(run,'export-'+name+'.tcl').read_text(encoding='utf-8-sig')
    reads=re.findall(r'(?m)^\s*mrd\s+-bin\s+-file\s+\{([^{}\r\n]+)\}\s+0x([0-9a-fA-F]+)\s+(\d+)\s*$',tcl)
    require(len(reads)==1 and int(reads[0][1],16)==start and int(reads[0][2])==size//4,
            'Export Tcl mrd address/word count differs')
    require(inside(run,reads[0][0])==inside(run,name+'.bin'),'Export Tcl path differs')
    require(len(re.findall(r'\bmrd\b',tcl))==1,'Export Tcl has extra reads')
    require(not re.search(r'(?i)\b(mwr|rst|dow|fpga|exec|source|loadhw|program_flash|sf)\b',tcl),
            'Export Tcl contains a write/load/execute operation')
    whitelist={'if {[catch {','connect -url tcp:127.0.0.1:3121',
        'targets -set -filter {name == "APU"}','configparams force-mem-accesses 1',
        'puts '+marker,'disconnect','} e opts]} {puts stderr "EXPORT_ERROR: $e";exit 1}','exit'}
    for source_line in tcl.splitlines():
        source_line=source_line.strip()
        require(not source_line or source_line in whitelist or re.fullmatch(
            r'mrd\s+-bin\s+-file\s+\{[^{}\r\n]+\}\s+0x[0-9a-fA-F]+\s+\d+',source_line),
            'Export Tcl contains an unapproved command')
    log=inside(run,'export-'+name+'.log').read_text(encoding='utf-8-sig')
    require(re.search(r'(?m)^'+marker+r'\s*$',log) is not None and
            'EXPORT_ERROR' not in log and not re.search(r'(?im)^\s*(?:error|failed)\b',log),
            'Export success marker missing or error present')
    return raw


def _scpi_fields(raw, prefix):
    require(isinstance(raw,str) and raw.startswith(prefix+' '),'Invalid raw SCPI prefix')
    parts=raw[len(prefix)+1:].split(',')
    require(len(parts)%2==0 and all(parts),'Malformed raw SCPI fields')
    out=dict(zip(parts[::2],parts[1::2]))
    require(len(out)*2==len(parts),'Duplicate SCPI keys')
    return out


def _number(raw):
    found=re.fullmatch(r'([+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)(?:HZ|V|S|%)?',str(raw),re.I)
    require(found is not None,'Invalid raw source number')
    return float(found[1])


def audit_source(run):
    values=[read_json(inside(run,'source-'+name+'.json')) for name in ('original','on','restored')]
    for value,enabled in zip(values,(False,True,False)):
        require(value.get('output_on') is enabled and value.get('model','').upper()=='SDG2122X',
                'Source output OFF/ON/OFF or model mismatch')
        identity=value['raw_idn'].split(',')
        require(len(identity)==4 and identity[1].upper()=='SDG2122X' and
                identity[0].upper() in ('SIGLENT','SIGLENT TECHNOLOGIES','SIGLENT TECHNOLOGIES CO., LTD.'),
                'Source raw identity mismatch')
        fields=_scpi_fields(value['raw_bswv'],'C2:BSWV')
        require(fields==value['bswv'],'Parsed/raw BSWV mismatch')
        require(fields.get('WVTP')=='SQUARE' and value['waveform']=='SQUARE','Expected current SQUARE waveform')
        for field,key,expected in [('FRQ','frequency_hz',50000),('AMP','vpp',1),('OFST','offset_v',0)]:
            require(_number(fields[field])==expected and float(value[key])==expected,
                    'Source waveform settings mismatch')
        require(5<=_number(fields['DUTY'])<=95,'Unsafe square duty')
        output=('ON' if enabled else 'OFF')
        prefix='C2:OUTP '+output+','
        require(value['raw_outp'].startswith(prefix),'Source raw output mismatch')
        outp=_scpi_fields('C2:OUTP '+value['raw_outp'][len(prefix):],'C2:OUTP')
        require(outp==value['outp'] and outp.get('LOAD')=='HZ' and outp.get('PLRT')=='NOR'
                and value['load']=='HZ' and value['polarity']=='NOR','Source HiZ/NOR mismatch')
        require(set(value['modes'])=={'MDWV','SWWV','BTWV'},'Incomplete source mode snapshot')
        for mode,item in value['modes'].items():
            require(item['state']=='OFF' and _scpi_fields(item['raw'],'C2:'+mode).get('STATE')=='OFF',
                    'Source mode not OFF or raw/parsed mismatch')
        coupling=value['coupling'];raw=_scpi_fields(coupling['raw'],'COUP')
        require(raw==coupling['fields'],'Raw coupling mismatch')
        require(all(raw.get(key)=='OFF' for key in ('TRACE','FCOUP','PCOUP','ACOUP'))
                and raw.get('TRDUCH','OFF')=='OFF','Source coupling not OFF')
    require(all(value['raw_idn']==values[0]['raw_idn'] and value['raw_bswv']==values[0]['raw_bswv']
                for value in values),'Source identity/raw waveform changed during test')
    return dict(waveform='SQUARE',frequency_hz=50000,vpp=1,offset_v=0,output_sequence=['OFF','ON','OFF'])


def audit_calibration(cal, uart):
    status=cal['status'];profiles=cal['profiles']
    require(matches(status,dict(instrument_id=CAL_ID,instrument_flags='259',record_id=CAL_ID,record_flags='259')),
            'Instrument/record calibration mismatch')
    expected=[('CALIBRATION_STATUS',status)]
    for name in ('instrument','record','spectrum'):
        profile=profiles[name]
        require(profile.get('profile')==name,'Calibration profile naming')
        active=name!='spectrum' or status['spectrum_valid']=='1'
        if active:
            require(matches(profile,CAL_BITS) and same(status[name+'_id'],CAL_ID)
                    and str(status[name+'_flags'])=='259','Calibration coefficient bits/identity changed')
        else:
            require(same(status['spectrum_id'],'00000000') and str(status['spectrum_flags'])=='0'
                    and all(int(profile[key],16)==0 for key in CAL_BITS),'Inactive spectrum calibration not empty')
        expected.append(('CALIBRATION_COEFFICIENTS',profile))
    expected.append(('CALIBRATION_END',dict(display_id=status['display_id'],epoch=status['epoch'])))
    uart.group(expected)


def audit_load(load):
    load=Path(load).resolve()
    load_state=read_json(inside(load,'session.json'))
    require(load_state.get('state')=='verified-new-application-readonly-list-no-card-writes'
            and same(load_state.get('elf_sha256'),ELF_SHA) and load_state.get('card_writes')==0
            and load_state.get('flash_written') is False,'Missing reviewed readonly load linkage')
    for name,expected in [('application.elf',ELF_SHA),('platform.bit',BIT_SHA),('ps7_init.tcl',PSINIT_SHA)]:
        require(digest(inside(load,name).read_bytes())==expected,'Readonly-load artifact changed: '+name)
    uart=UART(inside(load,'uart.log').read_text(encoding='utf-8-sig'))
    audit_calibration(load_state['calibration'],uart)
    uart.row('UI_STATUS',load_state['final']['ui']);uart.row('ADC_LIVE_STATUS',load_state['final']['adc'])
    actions=load_state['tf_actions']
    require(len(actions)==1 and actions[0]['command']=='tflist 0 0','Readonly load contained unexpected TF actions')
    commands=[row for row in uart.rows if row[1]=='TF_COMMAND']
    expected_ack=f"TF_COMMAND tflist 0 0 QUEUED seq={actions[0]['sequence']}"
    require(len(commands)==1 and commands[0][3]==expected_ack,
            'Readonly-load raw UART contains extra/missing/unexpected TF command')
    reply=actions[0]['status']
    require(matches(reply['status'],dict(busy='0',ready='1',result='0',kind='0',seq=actions[0]['sequence']))
            and matches(reply['media'],dict(cid=CID,fat='3',sector_count='61067264'))
            and matches(reply['blob'],dict(address='00000000',bytes='0',crc='00000000')),
            'Readonly-load media/result/empty-blob mismatch')
    completed=uart.group([('TF_STATUS',reply['status']),('TF_MEDIA',reply['media']),('TF_BLOB',reply['blob'])])
    require(any(frame[0][0]>commands[0][0] for frame in completed),
            'Readonly-load completion precedes its command')
    # Polls may report the pending request before its completed list result.
    # Every raw TF diagnostic must still belong to that sole readonly request.
    rows=[row for row in uart.rows if row[1] in ('TF_STATUS','TF_MEDIA','TF_BLOB')]
    require(len(rows)%3==0,'Readonly-load incomplete TF diagnostic transaction')
    for index in range(0,len(rows),3):
        frame=rows[index:index+3]
        require([row[1] for row in frame]==['TF_STATUS','TF_MEDIA','TF_BLOB']
                and frame[0][0]>commands[0][0], 'Readonly-load mixed/unordered TF diagnostic transaction')
        require(matches(frame[0][2],dict(seq=actions[0]['sequence'],id='0',kind='0',ready='1'))
                and str(frame[0][2].get('busy')) in ('0','1'),
                'Readonly-load raw TF diagnostic belongs to another request')
        require(matches(frame[2][2],dict(address='00000000',bytes='0',crc='00000000')),
                'Readonly-load raw diagnostics exposed a file blob')
    return dict(session_sha256=digest(inside(load,'session.json').read_bytes()),
                uart_sha256=digest(inside(load,'uart.log').read_bytes()),elf_sha256=ELF_SHA,cid=CID)


def audit_snapshot(snap, uart):
    ui,adc,memory,param,measurement,fft,cal=(snap[key] for key in
        ('ui','adc','memory','parameter','measurement','fft','calibration'))
    uart.row('UI_STATUS',ui);uart.row('ADC_LIVE_STATUS',adc)
    uart.group([('MEMORY_STATUS',memory['status']),('MEMORY_VIEW',memory['view']),
                ('MEMORY_END',dict(display_id=ui['display_id']))])
    uart.group([('PARAM_STATUS',param['status']),('PARAM_CONFIG',param['config']),
                ('PARAM_END',dict(epoch=ui['epoch']))])
    uart.group([('MEASURE_STATUS',measurement['context'])]+
               [('MEASURE_CHANNEL',channel) for channel in measurement['channels']]+
               [('MEASURE_END',dict(display_id=ui['display_id']))])
    uart.group([('FFT_STATUS',fft['status']),('FFT_VALUES',fft['values']),
                ('FFT_END',dict(seq=fft['status']['seq']))])
    audit_calibration(cal,uart)
    view=memory['view'];p=param['status'];context=measurement['context']
    require(len(measurement['channels'])==2 and [str(ch['ch']) for ch in measurement['channels']]==['1','2'],
            'Measurement missing/mixed channels')
    require(ui['running']=='0' and adc['pending']=='0' and view['running']=='0',
            'Checkpoint not frozen/cancelled')
    for values in (adc,view,p,context,fft['values'],cal['status']):
        require(same(values.get('display_id'),ui['display_id']),'Checkpoint mixed display ID')
    for values,key in [(view,'epoch'),(p,'epoch'),(context,'epoch'),(fft['values'],'ui_epoch'),(cal['status'],'epoch')]:
        require(same(values.get(key),ui['epoch']),'Checkpoint mixed epoch')
    require(matches(adc,dict(display_address=view['record_address'],display_crc=view['record_crc'],
                            display_rate=view['rate'],display_epoch=view['display_epoch'])),'Checkpoint mixed raw ADC')
    require(matches(p,dict(record_address=view['record_address'],record_bytes='16384',
                          record_crc=view['record_crc'],rate=view['rate'],actual_conversion='50000000',
                          wave_crc=memory['status']['blob_crc'],wave_valid=memory['status']['valid'])),
            'Checkpoint mixed parameter/raw/slots')
    require(view['record_bytes']=='16384' and view['actual_conversion']=='50000000'
            and context['count']=='4096' and context['rate']==view['rate'],'Checkpoint original rate/count')
    require(fft['values']['wave_crc']==p['wave_crc'] and fft['values']['param_crc']==p['blob_crc']
            and fft['values']['replay']==memory['status']['replay']
            and fft['values']['live_rate']==view['rate'],'Checkpoint mixed FFT slots/context')


def held(before, after, advance=0):
    require((int(after['ui']['epoch'])-int(before['ui']['epoch']))%2**32==advance,'Held epoch changed')
    groups=[('ui',('display_id','seen_id','running')),
            ('memory.view',('record_address','record_crc','rate','conversion','filter','display_epoch')),
            ('memory.status',('valid','blob_crc','saved_id','saved_rate','saved_epoch')),
            ('parameter.status',('valid','blob_crc','wave_valid','wave_crc'))]
    for group,keys in groups:
        a,b=before,after
        for key in group.split('.'):
            a,b=a[key],b[key]
        require(matches(b,{key:a[key] for key in keys}),'Held data/slot changed: '+group)
    for a,b in zip(before['measurement']['channels'],after['measurement']['channels']):
        require(matches(b,{key:a[key] for key in ('min_v','max_v','mean_v','vpp_v','rms_total_v','rms_ac_v',
                                                 'frequency_hz','period_us')}),'Held measurement changed')


def bind_raw(entry,snap):
    require(matches(entry,dict(address=snap['memory']['view']['record_address'],bytes=16384,
                               crc=snap['memory']['view']['record_crc'])),'Raw export/checkpoint mismatch')


def audit_records(state, checkpoints, exports, data, uart):
    before=checkpoints['frozen-before-files'];replay=checkpoints['tf-replay-original-rate']
    live=checkpoints['live-50m-before-replay'];returned=checkpoints['returned-live']
    require(data['wave-saved']==data['wave-loaded'],'TF SCW load differs from saved file bytes')
    record=parse_record(data['wave-saved']);meta=record['meta'];cal=meta['calibration']
    require(meta['version']==2 and cal['flags']==259 and cal['id']==int(CAL_ID,16),'SCW calibration/version')
    bits=struct.unpack_from('<4I',data['wave-saved'],108)
    require(bits==tuple(int(CAL_BITS[key],16) for key in ('slope1_bits','offset1_bits','slope2_bits','offset2_bits')),
            'SCW float coefficient bits')
    require(meta['rate_hz']==6250000 and meta['conversion_hz']==50000000 and meta['filter_kind']==3,
            'SCW original rate/conversion/filter')
    require(record['raw_data']==data['original-raw']==data['tf-replay-raw'],'SCW payload differs from original/replay raw')
    for name,snap in [('original-raw',before),('tf-replay-raw',replay)]:
        bind_raw(exports[name],snap)
        require(int(snap['ui']['display_id'])==meta['record_id']
                and int(snap['memory']['view']['display_epoch'])==meta['capture_epoch']
                and snap['memory']['view']['rate']=='6250000','SCW ID/epoch/rate not bound to display')
    require(before['memory']['status']['replay']=='0' and replay['memory']['status']['replay']=='1',
            'Replay transition missing')
    require(data['protected-live-raw']==data['protected-live-after'],'Replay overwrote protected live DDR')
    for name in ('protected-live-raw','protected-live-after'):
        bind_raw(exports[name],live)
    require(live['memory']['view']['rate']=='50000000' and returned['memory']['view']['rate']=='50000000'
            and returned['memory']['status']['replay']=='0','Live rate not restored')
    require(matches(returned['memory']['view'],{key:live['memory']['view'][key] for key in
        ('record_address','record_crc','display_id','display_epoch','rate')}),'Return live cache changed')
    require(data['parameters-saved']==data['parameters-loaded'],'TF SCP load differs from saved file bytes')
    settings=parse_settings(data['parameters-saved'])['settings']
    p=before['parameter']['config']
    pairs={'timebase_ns':'time_ns','origin_conversion_hz':'origin_conversion',
           'selected_channel':'selected_channel','trigger_channel':'trigger_channel','trigger_level_mv':'level_mv',
           'flags':'flags','language':'language'}
    for field,key in pairs.items():
        require(str(settings[field])==str(p[key]),'SCP stored settings mismatch: '+field)
    for field,keys in [('scale_index',('ch1_scale','ch2_scale')),('position_mdiv',('ch1_pos','ch2_pos')),
                       ('cursor',('cursor_a','cursor_b'))]:
        require(settings[field]==[int(p[key]) for key in keys],'SCP vector settings mismatch: '+field)
    preferences=('time','source','ch1_scale','ch2_scale','ch1_pos','ch2_pos','trig_channel','trig_normal',
                 'trig_rising','level_mv','dual','grid','cursors','cursor_a','cursor_b','lang')
    require(matches(checkpoints['after-TF-parameters']['ui'],{key:before['ui'][key] for key in preferences}),
            'Restored preferences differ from saved settings')
    held(before,checkpoints['wave-save-held']);held(before,checkpoints['parameter-save-held'])
    held(checkpoints['before-TF-parameters'],checkpoints['after-TF-parameters'],1)
    held(checkpoints['after-TF-parameters'],checkpoints['missing-file-held'])
    for snap in checkpoints.values():
        require(matches(snap['memory']['status'],{key:before['memory']['status'][key] for key in
            ('valid','blob_crc','saved_id','saved_rate','saved_epoch')}),'TF action changed original RAM wave slot')
        require(matches(snap['parameter']['status'],{key:before['parameter']['status'][key] for key in
            ('valid','blob_crc','wave_valid','wave_crc')}),'TF action changed original RAM parameter slot')
    fft=state['tf_fft']
    require(matches(fft['status'],dict(valid='1',rate='6250000',id=before['ui']['display_id'],
        source_epoch=before['memory']['view']['display_epoch'])) and fft['values']['replay']=='1',
        'TF FFT lost replay original rate/identity')
    uart.group([('FFT_STATUS',fft['status']),('FFT_VALUES',fft['values']),('FFT_END',dict(seq=fft['status']['seq']))])
    for name,index in [('wave-saved',0),('parameters-saved',1),('wave-loaded',9),('parameters-loaded',10)]:
        blob=state['tf_actions'][index]['status']['blob']
        require(matches(exports[name],dict(address=blob['address'],bytes=blob['bytes'],crc=blob['crc'])),
                'File export not bound to corresponding TF action: '+name)
    return dict(record_id=meta['record_id'],record_epoch=meta['capture_epoch'],record_hz=6250000,
                actual_conversion_hz=50000000,scw_version=2,calibration_id=CAL_ID,settings_bytes=96)


def audit_stability(state, uart):
    running=state['running_save']
    for value in (running['before'],running['after']):
        uart.row('UI_STATUS',value['ui']);uart.row('ADC_LIVE_STATUS',value['adc'])
        require(value['ui']['running']=='1','Running TF save stopped capture')
    require(running['before']['ui']['epoch']==running['after']['ui']['epoch'],'Running TF save changed epoch')
    require(running['tf']==state['tf_actions'][8]['status'],'Running TF save not tied to eighth wave file')
    stability=state['stability'];require(float(stability['seconds'])>=30,'Stability observation under 30 seconds')
    require(isinstance(stability['samples'],list) and bool(stability['samples']),
            'Stability has no intermediate samples')
    samples=[stability['before']]+stability['samples']+[stability['after']]
    ids=[];transactions=[];previous=-1
    for value in samples:
        ui=value['ui'];adc=value['adc']
        groups=uart.group([('UI_STATUS',ui),('ADC_LIVE_STATUS',adc)])
        candidates=[]
        for group in groups:
            first,last=group[0][0],group[-1][0]
            if first<=previous:
                continue
            boundaries=[row[0] for row in uart.rows if row[1]=='COMMANDS:']
            before=[index for index in boundaries if index<first]
            after=[index for index in boundaries if index>last]
            if before and after and max(before)>previous and not any(first<index<last for index in boundaries):
                candidates.append(group)
        require(bool(candidates),'Stability sample has no ordered, framed raw UART acquisition transaction')
        transaction=candidates[0];transactions.append(transaction);previous=transaction[-1][0]
        require(ui['running']=='1' and adc['available']=='1','Stability capture not running/available')
        ids.append(int(ui['display_id']))
    require(ids[-1]>ids[0] and all(a<=b for a,b in zip(ids,ids[1:])), 'Stability ID failed to advance/monotone')
    window_start=transactions[0][0][0];window_end=transactions[-1][-1][0]
    diag=[row for row in uart.rows if row[1]=='APP_DIAG' and window_start<row[0]<window_end]
    require(len(diag)>=2,'Missing repeated real video diagnostics')
    for _,_,fields,_ in diag:
        require(matches(fields,dict(locked='1',pixel_locked='1')) and int(fields['dma_errors'],16)==0,
                'Video lock or DMA error')
    times=[int(row[2]['ms']) for row in diag];frames=[int(row[2]['frames']) for row in diag]
    require(all(a<=b for a,b in zip(times,times[1:])) and all(a<=b for a,b in zip(frames,frames[1:])),
            'Stability-window video time/frame counters are not ordered')
    observed_ms=times[-1]-times[0]
    require(observed_ms>=30000 and frames[-1]>frames[0],
            'Stability-window UART diagnostics do not prove advancing 30s video')
    return dict(seconds=float(stability['seconds']),before_id=ids[0],after_id=ids[-1],
                uart_observed_seconds=observed_ms/1000,uart_first_line=window_start+1,uart_last_line=window_end+1,
                video_diagnostic_count=len(diag),video_frame_delta=frames[-1]-frames[0])


def audit_run(run):
    run=Path(run).resolve();session=inside(run,'session.json');state=read_json(session)
    require(state.get('state')=='verified-board-M5-files-awaiting-physical-and-power-cycle',
            'Session not a completed M5 board file test')
    require(state.get('flash_written') is False and state.get('resume_needed') is False,'Unsafe/incomplete session cleanup')
    require(state.get('source_restore')=='verified-original-output-state-waveform-unchanged','Source restore not verified')
    require(Path(state['run']).resolve()==run,'Session run path differs')
    proof=audit_package(run,state)
    load=audit_load(state['load_run'])
    uart=UART(inside(run,'uart.log').read_text(encoding='utf-8-sig'))
    tf=audit_tf_actions(state,uart);source=audit_source(run)
    checkpoints={}
    for item in state.get('checkpoints',[]):
        require(item['name'] not in checkpoints,'Duplicate checkpoint name')
        audit_snapshot(item['snapshot'],uart);checkpoints[item['name']]=item['snapshot']
    required={'frozen-before-files','wave-save-held','parameter-save-held','live-50m-before-replay',
              'tf-replay-original-rate','returned-live','before-TF-parameters','after-TF-parameters','missing-file-held'}
    require(required<=checkpoints.keys(),'Missing checkpoints')
    exports={};data={}
    for entry in state.get('exports',[]):
        require(entry['name'] not in exports,'Duplicate export name')
        exports[entry['name']]=entry
        data[entry['name']]=audit_export(run,entry,uart,proof['load_segments'])
    names={'original-raw','wave-saved','parameters-saved','protected-live-raw','wave-loaded',
           'tf-replay-raw','protected-live-after','parameters-loaded','original-frozen-frame',
           'tf-replay-frame','tf-files-page-1-frame','tf-files-page-2-frame'}
    require(names<=exports.keys(),'Missing required raw/blob/frame exports')
    record=audit_records(state,checkpoints,exports,data,uart)
    stability=audit_stability(state,uart)
    audit_calibration(state['final_calibration'],uart)
    uart.row('UI_STATUS',state['final']['ui']);uart.row('ADC_LIVE_STATUS',state['final']['adc'])
    final_memory,final_parameter=state['final_memory'],state['final_parameter']
    uart.group([('MEMORY_STATUS',final_memory['status']),('MEMORY_VIEW',final_memory['view']),
                ('MEMORY_END',dict(display_id=final_memory['view']['display_id']))])
    uart.group([('PARAM_STATUS',final_parameter['status']),('PARAM_CONFIG',final_parameter['config']),
                ('PARAM_END',dict(epoch=final_parameter['status']['epoch']))])
    before=checkpoints['frozen-before-files']
    require(matches(final_memory['status'],{key:before['memory']['status'][key] for key in
        ('valid','blob_crc','saved_id','saved_rate','saved_epoch')}),'Final stability changed RAM wave slot')
    require(matches(final_parameter['status'],{key:before['parameter']['status'][key] for key in
        ('valid','blob_crc','wave_valid','wave_crc')}),'Final stability changed RAM parameter slot')
    return dict(state=PASS_STATE,session_sha256=digest(session.read_bytes()),
                uart_sha256=digest(inside(run,'uart.log').read_bytes()),elf_sha256=ELF_SHA,
                package_files=len(state['package']),export_count=len(exports),tf=tf,source=source,readonly_load=load,
                record=record,stability=stability,physical_ui='not-verified-by-this-audit',
                power_cycle='not-run-by-this-audit',scope='offline-read-only-audit-of-recorded-board-evidence')


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run',type=Path)
    parser.add_argument('--output',type=Path)
    args=parser.parse_args(argv)
    try:
        result=audit_run(args.run)
        output=args.output or args.run/'storage-evidence-audit.json'
        output.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
        print(json.dumps(result,ensure_ascii=False,indent=2))
    except (OSError,ValueError,KeyError,TypeError,IndexError,struct.error) as exc:
        parser.exit(1,f'Storage evidence audit failed: {exc}\n')
    return 0


if __name__=='__main__':
    raise SystemExit(main())
