"""Compare the NEW XDC with the original SP2 schematic, including RGB bit order."""
from pathlib import Path
import re
import fitz

root = Path(__file__).resolve().parents[1]
pdf = Path(r'D:\FPGA_Project\Smart_ZYNQ_SP2_information\SmartZynq_SP2_Schematic_V1d3B_20260413.pdf.pdf')
with fitz.open(pdf) as doc:
    text = doc[11].get_text()
reference_pattern = r'PACKAGE_PIN\s+(\w+)\s+IOSTANDARD\s+LVCMOS33\}\s+\[get_ports\s+\{?(RGB_LCD_\w+(?:\[\d\])?)'
reference = {name:pin for pin,name in re.findall(reference_pattern,text)}
xdc = (root/'hardware/constraints/sp2_v13b_j35.xdc').read_text(encoding='utf-8')
actual_pattern = r'PACKAGE_PIN\s+(\w+)\s+\[get_ports\s+\{?(lcd_\w+(?:\[\d\])?|tp_reset_n)'
actual = {name:pin for pin,name in re.findall(actual_pattern,xdc)}
names = {'lcd_clk':'RGB_LCD_CLK','lcd_hsync':'RGB_LCD_HSYNC','lcd_vsync':'RGB_LCD_VSYNC',
         'lcd_de':'RGB_LCD_DE','lcd_bl':'RGB_LCD_BL','lcd_reset_n':'RGB_LCD_RST','tp_reset_n':'RGB_LCD_TP_RES'}
for channel in 'rgb':
    names.update({f'lcd_{channel}[{bit}]':f'RGB_LCD_{channel.upper()}[{bit}]' for bit in range(8)})
if set(actual)!=set(names):
    raise SystemExit(f'Unexpected constrained ports: {set(actual)^set(names)}')
for name,pin in actual.items():
    if reference.get(names[name])!=pin:
        raise SystemExit(f'Pin mismatch: {name}={pin}, PDF={reference.get(names[name])}')
print('PASS: all 31 LCD/touch-reset pins match original SP2 schematic PDF page 12, including RGB bit order.')
