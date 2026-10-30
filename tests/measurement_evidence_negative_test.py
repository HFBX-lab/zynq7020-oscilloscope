"""Exercise the complete auditor against modified copies of real session metadata."""
from pathlib import Path
import copy
import json
import sys
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
import audit_measurement_evidence as audit

directory=Path(sys.argv[1])
original=audit.load(directory/'session.json')
audit.audit(directory)
mutations=[]
for field,value in [('rms_ac_v','0.123456'),('mean_v','NaN'),('rms_total_v','Infinity'),('period_us','1'),('overrange','1')]:
    def change(s,field=field,value=value):
        # Mutate both UART snapshot and its paired export: binding alone must
        # not pass a physically wrong value with otherwise valid CRC/SHA.
        s['measurement_records'][0]['measurement']['channels'][1][field]=value
        s['measurement_checks'][0]['measurement']['channels'][1][field]=value
    mutations.append((field,change))
mutations.extend([
 ('frozen-rate',lambda s:s['measurement_checks'][6]['measurement']['context'].update(rate='6250000')),
 ('frozen-single-badge',lambda s:s['measurement_checks'][6]['ui'].update(single_complete='1')),
 ('export-ID',lambda s:s['measurement_records'][0].update(id='0')),
 ('frame-hash',lambda s:s['measurement_records'][0].update(frame_sha256='0'*64)),
 ('raw-hash',lambda s:s['measurement_records'][0].update(sha256='0'*64)),
 ('trigger-source',lambda s:s['measurement_checks'][2]['ui'].update(trig_channel='1')),
 ('missing-check',lambda s:s['measurement_checks'].pop()),
])
rejected=[]
for name,change in mutations:
    candidate=copy.deepcopy(original);change(candidate)
    with patch.object(audit,'load',return_value=candidate):
        try:audit.audit(directory)
        except (AssertionError,KeyError,ValueError):rejected.append(name)
        else:raise AssertionError('False positive: '+name)
result=dict(state='offline-only-real-evidence-negative-tests',rejected=rejected,count=len(rejected),original_run=str(directory))
(directory/'measurement-negative-tests.json').write_text(json.dumps(result,indent=2)+'\n')
print('MEASUREMENT_NEGATIVES_PASS',len(rejected))
