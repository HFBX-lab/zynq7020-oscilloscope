"""Read-only audit of saved board logs; does not connect UART or JTAG."""
from pathlib import Path
import hashlib,json,re
root=Path(__file__).resolve().parents[1]
s=json.loads((root/'build/touch-soc/board-session.json').read_text(encoding='utf-8-sig'))
assert s['state']=='verified-board-video-awaiting-touch-observation',s['state']
run=Path(s['run'])
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest().upper()
for f in s['files']:assert sha(Path(f['Path']))==f['Hash'],f['Path']
assert sha(run/'loader.ps1')==s['loader_sha256']
assert sha(run/'uart.log')==s['uart_sha256']
boot=(run/'boot.log').read_text(encoding='utf-8-sig')
assert 'BOOT_MODE=0' in boot and 'TOUCH_APP_RAM_LOADED' in boot
uart=(run/'uart.log').read_text(encoding='utf-8-sig')
assert 'TOUCH_STATUS available=1 family=GTXX address=5d id=911 ' in uart
assert 'raw=1024x600 swap=0 invert_x=0 invert_y=0' in uart
ready=re.search(r'LVGL_READY os=freertos[^\r\n]*input=PANEL_I2C_AND_UART',uart)
assert ready
diags=re.findall(r'APP_DIAG os=freertos[^\r\n]+',uart[ready.end():])
assert len(diags)>=3
for d in diags:
 for field in ['locked=1','pixel_locked=1','dma_errors=00000000','underflow_sample=0','overflow_sample=0','tasks=5']:
  assert re.search(r'(?:^| )'+field+r'(?: |$)',d),(field,d)
assert not s['flash_written'] and s['real_adc']=='not-run'
assert s['server']=='owned-server-stopped'
result=dict(state='verified-saved-board-log',run=str(run),controller='GT911',address_7bit='0x5D',
            geometry=[1024,600],diagnostic_samples=len(diags),flash_written=False,
            physical_touch='awaiting-user-operation-feedback',real_adc='not-run')
physical_path=root/'build/touch-soc/physical-observations.json'
if physical_path.exists():
 physical=json.loads(physical_path.read_text(encoding='utf-8'))
 assert physical['state']=='verified-board-physical-touch-with-user-confirmation'
 text=''
 for item in physical['interaction_logs']:
  path=Path(item['path']);assert path.parent==run and sha(path)==item['sha256']
  text+='\n'+path.read_text(encoding='utf-8-sig')
 assert not re.search(r'TAP_QUEUED|APP_FATAL|VIDEO_PRESENT_FAIL|VIDEO_STOP_TIMEOUT',text)
 assert not physical['uart_taps_sent'] and physical['user_observation']
 releases=re.findall(r'TOUCH_POINT pressed=0[^\r\n]+',text)
 assert len(releases)==physical['releases']==physical['gestures'] and len(releases)>0
 assert all('errors=0' in l for l in re.findall(r'TOUCH_POINT[^\r\n]+',text))
 for field,value in [('running','0'),('running','1'),('wave','0'),('wave','1'),('wave','2'),('lang','en'),('lang','zh-CN')]:
  assert any(re.search(r'(?:^| )'+field+'='+value+r'(?: |$)',l) for l in re.findall(r'UI_STATUS[^\r\n]+',text))
 video=re.findall(r'APP_DIAG os=freertos[^\r\n]+',text)
 assert len(video)==physical['video_diagnostic_samples'] and len(video)>=3
 for line in video:
  for field in ['locked=1','pixel_locked=1','dma_errors=00000000','underflow_sample=0','overflow_sample=0']:
   assert re.search(r'(?:^| )'+field+r'(?: |$)',line)
 result['physical_touch']=physical['state'];result['gestures']=physical['gestures']
(root/'build/touch-soc/board-validation.json').write_text(json.dumps(result,indent=2)+'\n')
print('PASS: saved JTAG/UART hashes, GT911 ID, geometry and video; physical touch = '+result['physical_touch'])
