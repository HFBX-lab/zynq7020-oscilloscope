"""Verify both channel trace colors exist in a decoded frozen LVGL frame."""
from pathlib import Path
import argparse,json
from PIL import Image
p=argparse.ArgumentParser();p.add_argument('image',type=Path);a=p.parse_args()
im=Image.open(a.image).convert('RGB');assert im.size==(1024,600)
chart=im.crop((26,138,766,462))
hist=chart.getcolors(chart.width*chart.height);counts={rgb:n for n,rgb in hist}
result={name:counts.get(rgb,0) for name,rgb in [('ADC1',(64,224,192)),('ADC2',(255,200,87))]}
assert all(n>=200 for n in result.values()),result
a.image.with_name(a.image.stem+'-traces.json').write_text(json.dumps(result,indent=2)+'\n')
print('DUAL_FRAME_TWO_TRACES_PASS: '+json.dumps(result))
