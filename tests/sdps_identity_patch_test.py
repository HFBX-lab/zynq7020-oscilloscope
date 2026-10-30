"""Exercise the actual two frozen vendor functions with 32-bit simulated MMIO."""
import importlib.util
import pathlib
import subprocess
import tempfile
import unittest

ROOT=pathlib.Path(__file__).resolve().parents[1]
PATCHER=ROOT/'scripts/patch_sdps_identity.py'
DRIVER=pathlib.Path('D:/Xilinx_2020.2/Vitis/2020.2/data/embeddedsw/XilinxProcessorIPLib/drivers/sdps_v3_10/src/xsdps_card.c')
GCC=pathlib.Path('D:/msys64/ucrt64/bin/gcc.exe')
TMP=pathlib.Path('D:/FPGA_Project/SP2_Storage_Build/identity-tests')

def function(source,name):
    start=source.index('s32 '+name+'(');end=source.index('\n}',start)+2
    return source[start:end]

HEADER=r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t u32;typedef int32_t s32;typedef uint8_t u8;
typedef struct { struct {u32 BaseAddress;} Config;u32 CardID[4],CardSpecData[4],RelCardAddr,SectorCount;u8 CardType,Card_Version;} XSdPs;
#define XST_SUCCESS 0
#define XST_FAILURE 1
#define XSDPS_CARD_SD 1U
#define CMD2 2U
#define CMD3 3U
#define CMD9 9U
#define XSDPS_RESP0_OFFSET 0U
#define XSDPS_RESP1_OFFSET 4U
#define XSDPS_RESP2_OFFSET 8U
#define XSDPS_RESP3_OFFSET 12U
#define CSD_SPEC_VER_MASK 0x3c0000U
#define READ_BLK_LEN_MASK 0xf00U
#define C_SIZE_MULT_MASK 0x380U
#define C_SIZE_LOWER_MASK 0xffc00000U
#define C_SIZE_UPPER_MASK 3U
#define CSD_STRUCT_MASK 0xc00000U
#define CSD_V2_C_SIZE_MASK 0x3fffff00U
#define XSDPS_BLK_SIZE_512_MASK 512U
static u32 command;
static int transfer_failure;
static u32 cid[4]={0xa1b2c3d4U,0x11223344U,0x55667788U,0x99aabbccU};
static u32 csd[4]={0x13572468U,0x12345678U,0xabcdef01U,0x00412345U};
static s32 XSdPs_CmdTransfer(XSdPs *p,u32 cmd,u32 arg,u32 x){(void)p;(void)arg;(void)x;command=cmd;return transfer_failure?XST_FAILURE:XST_SUCCESS;}
static u32 XSdPs_ReadReg(u32 base,u32 offset){(void)base;if(command==CMD3)return 0x34560000U;return command==CMD2?cid[offset/4U]:csd[offset/4U];}
#define XSdPs_ReadReg16(base,offset) ((uint16_t)XSdPs_ReadReg((base),(offset)))
'''
MAIN=r'''
int main(void){
    XSdPs instance={0};int failures=0;unsigned i;
    instance.CardType=XSDPS_CARD_SD;
    if(XSdPs_GetCardId(&instance)!=XST_SUCCESS)return 20;
    for(i=0;i<4;i++)if(instance.CardID[i]!=cid[i]){printf("CID[%u] got=%08x expected=%08x\n",i,instance.CardID[i],cid[i]);failures++;}
    if(instance.RelCardAddr!=0x34560000U)return 21;
    if(XSdPs_GetCsd(&instance)!=XST_SUCCESS)return 22;
    for(i=0;i<4;i++)if(instance.CardSpecData[i]!=csd[i]){printf("CSD[%u] got=%08x expected=%08x\n",i,instance.CardSpecData[i],csd[i]);failures++;}
    if(instance.SectorCount!=(((csd[1]&CSD_V2_C_SIZE_MASK)>>8U)+1U)*1024U)return 23;
    transfer_failure=1;
    for(i=0;i<4;i++){instance.CardID[i]=0x12345678U;instance.CardSpecData[i]=0x87654321U;}
    if(XSdPs_GetCardId(&instance)!=XST_FAILURE||XSdPs_GetCsd(&instance)!=XST_FAILURE)return 24;
    for(i=0;i<4;i++)if(instance.CardID[i]!=0x12345678U||instance.CardSpecData[i]!=0x87654321U)return 25;
    return failures?1:0;
}
'''

def exercise(source):
    TMP.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(dir=TMP) as name:
        path=pathlib.Path(name);c=path/'identity.c';exe=path/'identity.exe'
        c.write_text(HEADER+'\n'+function(source,'XSdPs_GetCardId')+'\n'+function(source,'XSdPs_GetCsd')+'\n'+MAIN)
        result=subprocess.run([str(GCC),'-std=c11','-Wall','-Wextra','-Werror',str(c),'-o',str(exe)],capture_output=True,text=True)
        if result.returncode:raise AssertionError(result.stdout+result.stderr)
        return subprocess.run([str(exe)],capture_output=True,text=True)

class IdentityPatchTest(unittest.TestCase):
    def setUp(self):
        self.assertTrue(PATCHER.is_file(),'identity patcher is required')
        spec=importlib.util.spec_from_file_location('identity_patch',PATCHER);self.patch=importlib.util.module_from_spec(spec);spec.loader.exec_module(self.patch)
        self.vendor=DRIVER.read_bytes()
    def test_full_word_cardid_and_csd_in_actual_functions(self):
        result=exercise(self.patch.patch_bytes(self.vendor).decode())
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
    def test_unpatched_vendor_fails_simulated_mmio(self):
        result=exercise(self.vendor.decode())
        self.assertEqual(result.returncode,1,result.stdout+result.stderr)
        self.assertIn('CID[0]',result.stdout);self.assertIn('CSD[3]',result.stdout)
    def test_patch_idempotent(self):
        patched=self.patch.patch_bytes(self.vendor)
        self.assertEqual(self.patch.patch_bytes(patched),patched)
    def test_unknown_vendor_content_rejected(self):
        with self.assertRaises(ValueError):self.patch.patch_bytes(self.vendor+b'\nunknown delta\n')
    def test_installed_source_cannot_be_patched(self):
        with self.assertRaises(ValueError):self.patch.patch_file(DRIVER)

if __name__=='__main__':unittest.main()
