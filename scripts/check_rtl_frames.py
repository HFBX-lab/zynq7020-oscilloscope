"""Check actual xsim pixel captures and export phone-friendly PNG previews."""
from pathlib import Path
import math
from PIL import Image

out=Path(__file__).resolve().parents[1]/'artifacts/rtl'
colors=[(255,255,255),(255,255,0),(0,255,255),(0,255,0),
        (255,0,255),(255,0,0),(0,0,255),(0,0,0)]
for name in ['colorbars','grid','waveform']:
    im=Image.open(out/f'{name}.ppm').convert('RGB')
    assert im.size==(1024,600),f'{name}: resolution'
    if name=='colorbars':
        expected=Image.new('RGB',(1024,600))
        for x in range(1024):
            for y in range(600):expected.putpixel((x,y),colors[x//128])
        assert im.tobytes()==expected.tobytes(),'Full-frame colorbar mismatch'
    else:
        assert [im.getpixel((92+i*120,26)) for i in range(8)]==colors,f'{name}: color key'
        gray=[im.getpixel((47+i*30,570)) for i in range(32)]
        assert gray==[(i*8,)*3 for i in range(32)],f'{name}: grayscale ramp must be RGB-equal'
        assert im.getpixel((32,100))==(135,152,175),f'{name}: left border'
        if name=='grid':
            assert im.getpixel((112,100))==(41,56,75),'Grid spacing'
            assert im.getpixel((512,100))==(82,105,129),'Center axis'
        else:
            # First frame advances the ROM phase by two. Check against independent math.
            for x in range(33,991):
                y=300-round(120*math.sin(2*math.pi*(x+2)/256))
                assert im.getpixel((x,y))==(64,224,192),f'Wave trace missing at x={x},y={y}'
            assert im.getpixel((100,500))==(16,25,37),'Off-trace background'
    im.save(out/f'{name}.png')
    print(f'PASS: {name} full raster capture -> {name}.png')
