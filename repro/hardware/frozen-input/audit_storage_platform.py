"""Offline proof that SD metadata keeps the accepted PL and other PS settings."""
import argparse,copy,difflib,hashlib,json,re,zipfile
from pathlib import Path
import xml.etree.ElementTree as ET

def require(ok,message):
    if not ok: raise ValueError(message)

def arrays(text):
    text=re.sub(r'/\*.*?\*/|//[^\n]*','',text,flags=re.S)
    return {name:[(op,tuple(int(v.strip().rstrip('uUlL'),0) for v in args.split(',') if v.strip())) for op,args in re.findall(r'(EMIT_\w+)\(([^)]*)\)',body)] for name,body in re.findall(r'unsigned long (ps7_\w+)\[\] = \{(.*?)\};',text,re.S)}

def compare_operations(before,after):
    require(set(before)==set(after),'PS initialization arrays changed')
    deltas=[]
    for name,ops in before.items():
        candidate=after[name]
        allowed={}
        if re.fullmatch(r'ps7_mio_init_data_[123]_0',name):
            allowed={0xf8000700+4*i:0x3fff for i in (40,41,42,43,44,45,47)}
            allowed[0xf8000830]=0x3f003f
        if re.fullmatch(r'ps7_clock_init_data_[123]_0',name):
            allowed={0xf8000150:0x3f31,0xf800012c:0x400}
        if re.fullmatch(r'ps7_peripherals_init_data_[123]_0',name): allowed={0xf8000218:0x11}
        def normalize(seq):
            result=[]
            for op,args in seq:
                if op=='EMIT_MASKWRITE' and len(args)==3 and args[0] in allowed:
                    addr,mask,value=args; changed_mask=allowed[addr]
                    if addr==0xf8000830:
                        require(mask==0x3f003f and value==0x2f0037,"Unexpected SD0 CD/WP selector")
                        continue
                    if addr==0xf8000150:
                        require(mask==0x3f33 and value==0x1001,"Unexpected SDIO clock: require SD0=100MHz and SD1 disabled")
                        continue
                    # Preserve all other bits, including SD1 clock enable and reset.
                    remaining=mask & ~changed_mask
                    if remaining: result.append((op,(addr,remaining,value & remaining)))
                else: result.append((op,args))
            return result
        require(normalize(ops)==normalize(candidate),'Unapproved PS initialization delta: '+name)
        if ops!=candidate:
            matcher=difflib.SequenceMatcher(a=ops,b=candidate,autojunk=False)
            for tag,i,j,k,l in matcher.get_opcodes():
                if tag!='equal':deltas.append(dict(array=name,before=ops[i:j],after=candidate[k:l]))
    return deltas

def compare_bd(before,after):
    before=copy.deepcopy(before);after=copy.deepcopy(after)
    for obj in (before,after):
        obj.pop('design_info',None)
        for comp in obj.get('components',{}).values():
            comp.pop('xci_path',None)
        if 'ps7' in obj.get('components',{}):obj['components']['ps7'].pop('parameters',None)
    require(before==after,'PL topology, addressing, interfaces, clocks, or non-PS parameters changed')
    return {'state':'verified','components':len(before.get('components',{})),'sections':sorted(before)}

def properties(text):
    result={}
    for line in text.splitlines():
        m=re.match(r'(\S+)\s+\S+\s+(?:true|false)\s+(.*)',line)
        if m:result[m[1]]=m[2]
    return result

def compare_properties(before,after):
    changed={k:(before.get(k),after.get(k)) for k in before.keys()|after.keys() if before.get(k)!=after.get(k)}
    allowed={'PCW_ACT_SDIO_PERIPHERAL_FREQMHZ','PCW_EN_SDIO0','PCW_SD0_GRP_CD_ENABLE','PCW_SD0_GRP_CD_IO','PCW_SD0_PERIPHERAL_ENABLE','PCW_SD0_SD0_IO','PCW_SDIO_PERIPHERAL_DIVISOR0','PCW_SDIO_PERIPHERAL_VALID','PCW_MIO_TREE_PERIPHERALS','PCW_MIO_TREE_SIGNALS'}
    allowed={'CONFIG.'+k for k in allowed}|{f'CONFIG.PCW_MIO_{i}_{field}' for i in (40,41,42,43,44,45,47) for field in ('DIRECTION','IOTYPE','PULLUP','SLEW')}
    require(set(changed)<=allowed,'Unexpected PS property delta: '+str(set(changed)-allowed))
    expected={'PCW_EN_SDIO0':'1','PCW_SD0_PERIPHERAL_ENABLE':'1','PCW_SD0_SD0_IO':'MIO 40 .. 45','PCW_SD0_GRP_CD_ENABLE':'1','PCW_SD0_GRP_CD_IO':'MIO 47','PCW_SD0_GRP_WP_ENABLE':'0','PCW_SD0_GRP_POW_ENABLE':'0','PCW_QSPI_PERIPHERAL_ENABLE':'1','PCW_QSPI_QSPI_IO':'MIO 1 .. 6','PCW_QSPI_GRP_SINGLE_SS_ENABLE':'1','PCW_ACT_SDIO_PERIPHERAL_FREQMHZ':'99.999001'}
    for k,v in expected.items():require(after.get('CONFIG.'+k)==v,'Wrong platform property '+k+': '+str(after.get('CONFIG.'+k)))
    for k in ('CONFIG.PCW_MIO_TREE_PERIPHERALS','CONFIG.PCW_MIO_TREE_SIGNALS'):
        a,b=before[k].split('#'),after[k].split('#')
        require(len(a)==len(b) and len(a)>=54,'MIO tree dimension mismatch')
        require(all(x==y for i,(x,y) in enumerate(zip(a,b)) if i not in (40,41,42,43,44,45,47)),'Non-SD MIO mapping changed')
    return [dict(property=k,before=v[0],after=v[1]) for k,v in sorted(changed.items())]

def hwh_signature(data):
    root=ET.fromstring(data)
    root.attrib.pop('TIMESTAMP',None)
    for module in root.findall('.//MODULE'):
        if module.get('INSTANCE')=='ps7':
            parameters=module.find('PARAMETERS')
            if parameters is not None: module.remove(parameters)
    return ET.tostring(root)

def compare_original_hwh(before,after):
    """Compare connected hardware; retain every regenerated metadata delta."""
    def flatten(el,path=''):
        key=path+'/'+el.tag+''.join('['+k+'='+el.get(k)+']' for k in ('INSTANCE','NAME','LOGICAL','PHYSICAL') if el.get(k))
        result={key:dict(el.attrib)}
        for child in el:result.update(flatten(child,key))
        return result
    aroot,broot=ET.fromstring(before),ET.fromstring(after)
    a,b=flatten(aroot),flatten(broot)
    differences=[dict(path=k,before=a.get(k),after=b.get(k)) for k in sorted(a.keys()|b.keys()) if a.get(k)!=b.get(k)]
    def normalized(root):
        root.attrib.pop('TIMESTAMP',None)
        for module in root.findall('.//MODULE'):
            if module.get('INSTANCE')=='ps7':
                params=module.find('PARAMETERS')
                if params is not None:module.remove(params)
            connected={p.get('NAME') for p in module.findall('PORTS/PORT') if p.find('CONNECTIONS') is not None}
            ports=module.find('PORTS')
            if ports is not None:
                for port in list(ports):
                    if port.get('NAME') not in connected:ports.remove(port)
            for maps in module.findall('BUSINTERFACES/BUSINTERFACE/PORTMAPS'):
                for mapping in list(maps):
                    if mapping.get('PHYSICAL') not in connected:maps.remove(mapping)
            # Vivado regenerates these informational AXI metadata values;
            # the actual GPIO component parameters and connected ports match.
            if module.get('INSTANCE')=='touch_gpio':
                for bus in module.findall('BUSINTERFACES/BUSINTERFACE'):
                    for parameter in list(bus):
                        if parameter.tag=='PARAMETER' and parameter.get('NAME') in ('NUM_READ_OUTSTANDING','NUM_WRITE_OUTSTANDING'):
                            require(parameter.get('VALUE') in ('1','2'),'Unexpected GPIO outstanding metadata')
                            bus.remove(parameter)
        return flatten(root)
    require(normalized(aroot)==normalized(broot),'Original PL connected interfaces, parameters, address, or clock differ')
    return differences

def main():
    p=argparse.ArgumentParser();p.add_argument('--root',type=Path,default=Path('D:/FPGA_Project/SP2_Storage_Build/hardware'));args=p.parse_args();out=args.root
    boot=Path('D:/FPGA_Project/SP2_Boot_Build/fsbl/hardware')
    accepted=Path(__file__).resolve().parents[1]/'build/highspeed-50m-soc/sp2_adc_live.xsa'
    accepted_bit=accepted.with_suffix('.bit')
    require(hashlib.sha256(accepted.read_bytes()).hexdigest()=='144607f33931603cbfb8e1b64b5707dcd00e5344b96e9aed1a09044b1e49bb92','Accepted application XSA SHA changed')
    require(hashlib.sha256((boot/'boot_qspi.xsa').read_bytes()).hexdigest()=='8e763bd68789dd1dd15377a380cef698aec1fb83f31627509807094b270d7ad2','Accepted QSPI metadata SHA changed')
    require(hashlib.sha256(accepted_bit.read_bytes()).hexdigest()=='be748a2a543047688010b3fd6179cda517eb5edba76a27962d62efbca90a3fbe','Accepted bitstream SHA changed')
    with zipfile.ZipFile(boot/'boot_qspi.xsa') as a,zipfile.ZipFile(out/'storage.xsa') as b,zipfile.ZipFile(accepted) as original:
        old=a.read('ps7_init.c').decode();new=b.read('ps7_init.c').decode()
        require(len(arrays(old))==len(arrays(new))==21,'Expected all 21 PS arrays')
        init=compare_operations(arrays(old),arrays(new))
        for data in (old,new):
            require(re.findall(r'ps7_config\s*\(\s*(ps7_\w+_init_data)\s*\)',data)==['ps7_mio_init_data','ps7_pll_init_data','ps7_clock_init_data','ps7_ddr_init_data','ps7_peripherals_init_data'],'PS init call order changed')
        require(not any(n.endswith('.bit') for n in b.namelist()),'Unexpected new bitstream')
        require(hwh_signature(a.read('screen_system.hwh'))==hwh_signature(b.read('screen_system.hwh')),'HWH PL interfaces/address/clock changed')
        # Require equivalent connected PL topology, addressing and clocks; retain regenerated HWH metadata differences.
        original_hwh_differences=compare_original_hwh(original.read('screen_system.hwh'),b.read('screen_system.hwh'))
        (out/'original-hwh-differences.json').write_text(json.dumps(original_hwh_differences,indent=2)+'\n')
        (out/'ps7_init.tcl').write_bytes(b.read('ps7_init.tcl'))
    src=json.loads((boot/'input/screen_system.bd').read_text())['design']
    newbd=json.loads((out/'project/sp2_storage_ps.srcs/sources_1/bd/screen_system/screen_system.bd').read_text())['design']
    report=dict(state='verified-offline-only',board_test='not-run',init_deltas=init,pl=compare_bd(src,newbd),original_hwh_differences=original_hwh_differences,properties=compare_properties(properties((out/'ps-before-properties.rpt').read_text()),properties((out/'ps-after-properties.rpt').read_text())),xsa_sha256=hashlib.sha256((out/'storage.xsa').read_bytes()).hexdigest(),compatible_accepted_bit_sha256='BE748A2A543047688010B3FD6179CDA517EB5EDBA76A27962D62EFBCA90A3FBE')
    (out/'platform-audit.json').write_text(json.dumps(report,indent=2)+'\n');print('STORAGE_PLATFORM_OFFLINE_AUDIT_PASS',len(init),len(report['properties']))
if __name__=='__main__':main()
