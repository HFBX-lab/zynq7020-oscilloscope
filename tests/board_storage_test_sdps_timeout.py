"""Run actual frozen vendor functions against stalled/ready/error SD responses."""
from pathlib import Path
import json,subprocess,sys
status=Path(sys.argv[1]) if len(sys.argv)>1 else next(
    p for p in sorted(Path('D:/FPGA_Project/SP2_Storage_Build/firmware/bsp-runs').glob('*/build-status.json'),reverse=True)
    if json.loads(p.read_text(encoding='utf-8-sig')).get('state')=='succeeded-offline-only')
build=json.loads(status.read_text(encoding='utf-8-sig'))
card=Path(build['bsp_include']).parent/'libsrc/sdps_v3_10/src/xsdps_card.c'
out=Path('D:/FPGA_Project/SP2_Storage_Build/adapter-tests/sdps-timeout')
out.mkdir(parents=True,exist_ok=True)
prefix=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <windows.h>
typedef uint32_t u32;typedef int32_t s32;typedef uint32_t TickType_t;
typedef struct{unsigned BusWidth,BaseAddress;} Config;
typedef struct{unsigned CardType,HC_Version,HCS,Switch1v8,RelCardAddr,CardID[4];Config Config;} XSdPs;
#define XST_SUCCESS 0
#define XST_FAILURE 1
#define XSDPS_CARD_SD 1
#define XSDPS_HC_SPEC_V3 3
#define XSDPS_WIDTH_8 8
#define XSDPS_ACMD41_HCS 0x40000000u
#define XSDPS_ACMD41_3V3 0x00ff8000u
#define XSDPS_CMD1_HIGH_VOL 0x00ff8000u
#define XSDPS_OCR_S18 0x1000000u
#define XSDPS_RESPOCR_READY 0x80000000u
#define XSDPS_RESP0_OFFSET 0
#define XSDPS_RESP1_OFFSET 4
#define XSDPS_RESP2_OFFSET 8
#define XSDPS_RESP3_OFFSET 12
#define CMD55 55
#define ACMD41 41
#define CMD1 1
#define CMD2 2
#define CMD3 3
#define pdMS_TO_TICKS(ms) (ms)
unsigned calls,current,ready,error_mode,delays;TickType_t tick;
TickType_t xTaskGetTickCount(void){return tick;}
void vTaskDelay(unsigned delay){tick+=delay;delays++;}
s32 XSdPs_CmdTransfer(XSdPs*s,unsigned cmd,u32 arg,unsigned flags){(void)s;(void)arg;(void)flags;current=cmd;assert(++calls<10000);return error_mode?XST_FAILURE:XST_SUCCESS;}
u32 XSdPs_ReadReg(unsigned base,unsigned reg){(void)base;(void)reg;if(current==41||current==1)return ready?XSDPS_RESPOCR_READY|XSDPS_ACMD41_HCS:0;if(current==3)return ready?0x12340000u:0;return 0xA1B2C3D4;}
u32 XSdPs_ReadReg16(unsigned base,unsigned reg){return XSdPs_ReadReg(base,reg)&65535;}
s32 XSdPs_Switch_Voltage(XSdPs*s){(void)s;return 0;}
'''
suffix=r'''
int main(void){SetErrorMode(SEM_NOGPFAULTERRORBOX);
    XSdPs sd={0};sd.CardType=XSDPS_CARD_SD;
    assert(XSdPs_CardOpCond(&sd)==XST_FAILURE);assert(tick==3000&&delays==3000&&calls<10000);
    tick=0;delays=0;calls=0;assert(XSdPs_GetCardId(&sd)==XST_FAILURE);assert(tick==3000&&delays==3000);
    tick=UINT32_MAX-10;delays=0;calls=0;assert(XSdPs_CardOpCond(&sd)==XST_FAILURE);assert(delays==3000);
    tick=0;delays=0;calls=0;ready=1;assert(XSdPs_CardOpCond(&sd)==XST_SUCCESS&&sd.HCS==1&&delays==1);
    tick=0;delays=0;calls=0;assert(XSdPs_GetCardId(&sd)==XST_SUCCESS&&sd.RelCardAddr==0x12340000&&delays==1);
    tick=0;delays=0;calls=0;ready=0;error_mode=1;assert(XSdPs_CardOpCond(&sd)==XST_FAILURE&&tick==0&&delays==0);
    assert(XSdPs_GetCardId(&sd)==XST_FAILURE&&delays==0);
    puts("sdps timeout PASS stalled ACMD41/CMD3, tick-wrap, ready, command-error");return 0;
}
'''
def extract(text,name):
    start=text.index('s32 '+name+'(')
    brace=text.index('{',start);depth=1;i=brace+1
    while depth:
        if text[i]=='{':depth+=1
        elif text[i]=='}':depth-=1
        i+=1
    return text[start:i]
def run(path,label):
    source=path.read_text()
    c=out/(label+'.c');exe=out/(label+'.exe')
    c.write_text(prefix+'\n'+extract(source,'XSdPs_CardOpCond')+'\n'+extract(source,'XSdPs_GetCardId')+suffix)
    subprocess.run(['D:/MinGW/bin/gcc.exe','-std=c11','-Wall','-Wextra','-Werror',str(c),'-o',str(exe)],check=True)
    return subprocess.run([str(exe)],capture_output=True,text=True)
red=run(card.with_name('xsdps_card.c.vendor-original'),'vendor-unbounded')
assert red.returncode!=0,'Unpatched driver unexpectedly passed timeout regression'
green=run(card,'bounded-run')
assert green.returncode==0,green.stderr
print(green.stdout,end='')
(out/'test-status.json').write_text(json.dumps({'state':'verified-offline-only','source':str(card),'red_returncode':red.returncode,'red_stderr':red.stderr,'green_returncode':green.returncode,'green_stdout':green.stdout},indent=2))
