"""Complete autoset audit rejects mutated copies of actual run metadata."""
from pathlib import Path
import copy
import json
import sys
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
import audit_autoset_evidence as audit

directory=Path(sys.argv[1]);original=audit.load(directory/'session.json')
audit.audit(directory)
cases=[]
for key,value in [('target_time','7'),('target_mask','0'),('display_rate','50000000'),('level_mv','0'),('epoch','0'),('display_id','0')]:
    def change(s,key=key,value=value):s['autoset_cases'][0]['completed']['context'][key]=value
    cases.append((key,change))
cases.extend([
 ('vertical-scale',lambda s:s['autoset_cases'][0]['completed']['channels'][1].update(scale='6')),
 ('vertical-position',lambda s:s['autoset_cases'][0]['completed']['channels'][1].update(position='0')),
 ('cancel-epoch',lambda s:s['autoset_cancel']['after']['ui'].update(epoch=s['autoset_cancel']['before']['ui']['epoch'])),
 ('cancel-cache',lambda s:s['autoset_cancel']['after']['measurement']['channels'][1].update(mean_v='0')),
 ('no-signal-trigger',lambda s:s['autoset_empty']['after']['ui'].update(trig_channel='1')),
 ('false-track',lambda s:next(e for e in s['controls'] if e['test']=='autoset-does-not-track-after-manual-adjustment')['ui'].update(ch2_scale='1')),
 ('frame-sha',lambda s:next(e for e in s['autoset_records'] if e['name']=='auto-result-manual').update(frame_sha256='0'*64)),
 ('source-sha',lambda s:s['autoset_records'][0].update(sha256='0'*64)),
])
rejected=[]
for name,change in cases:
    candidate=copy.deepcopy(original);change(candidate)
    with patch.object(audit,'load',return_value=candidate):
        try:audit.audit(directory)
        except (AssertionError,KeyError,ValueError):rejected.append(name)
        else:raise AssertionError('False positive:'+name)
result=dict(state='offline-only-real-autoset-evidence-negative-tests',count=len(rejected),rejected=rejected,run=str(directory))
(directory/'autoset-negative-tests.json').write_text(json.dumps(result,indent=2)+'\n')
print('AUTOSET_NEGATIVE_CASES_PASS',len(rejected))
