#include "qspi_readonly.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static unsigned calls,reads,failed;
static uint8_t pattern(uint32_t at){return (uint8_t)((at*17u)^(at>>9)^0xa5u);}
static int transfer(void *context,const uint8_t *tx,uint8_t *rx,unsigned count){
    (void)context;++calls;assert(count>=2&&count<=1028);memset(rx,0,count);
    if(failed)return -1;
    if(tx[0]==0x9f){assert(count==4);rx[1]=0xef;rx[2]=0x40;rx[3]=0x18;return 0;}
    if(tx[0]==0x05||tx[0]==0x35||tx[0]==0x15){assert(count==2);rx[1]=tx[0]==0x35?2:0;return 0;}
    assert(tx[0]==0x03);assert(count>=5);++reads;
    uint32_t address=((uint32_t)tx[1]<<16)|((uint32_t)tx[2]<<8)|tx[3];
    assert(address+count-4<=BOOT_FLASH_CAPACITY);
    for(unsigned i=4;i<count;i++){assert(tx[i]==0);rx[i]=pattern(address+i-4);}
    return 0;
}
int main(void){
    BootFlashReadOnly flash={.transfer=transfer};uint8_t id[3]={0},status=99,data[2051];
    assert(!boot_flash_read_id(&flash,id)&&id[0]==0xef&&id[1]==0x40&&id[2]==0x18);
    assert(!boot_flash_read_status(&flash,1,&status)&&status==0);
    assert(!boot_flash_read_status(&flash,2,&status)&&status==2);
    assert(!boot_flash_read_status(&flash,3,&status)&&status==0);
    assert(!boot_flash_read(&flash,0xfffffd,data,3));for(unsigned i=0;i<3;i++)assert(data[i]==pattern(0xfffffd+i));
    assert(!boot_flash_read(&flash,0x12345,data,sizeof(data)));
    for(unsigned i=0;i<sizeof(data);i++)assert(data[i]==pattern(0x12345+i));
    unsigned before=calls;memset(data,0x5a,sizeof(data));
    assert(boot_flash_read(&flash,0xffffff,data,2)==-1);
    assert(boot_flash_read(&flash,UINT32_MAX,data,1)==-1);
    assert(boot_flash_read(&flash,0,NULL,1)==-1);
    assert(boot_flash_read_status(&flash,0,&status)==-1);
    assert(boot_flash_read_status(&flash,4,&status)==-1);
    assert(boot_flash_read(NULL,0,data,1)==-1);
    assert(!boot_flash_read(&flash,BOOT_FLASH_CAPACITY,NULL,0));
    assert(calls==before&&data[0]==0x5a&&status==0);
    failed=1;assert(boot_flash_read_id(&flash,id)==-1);assert(id[0]==0xef);
    assert(boot_flash_read_status(&flash,1,&status)==-1&&status==0);
    assert(boot_flash_read(&flash,0,data,1)==-1&&data[0]==0x5a);
    assert(boot_flash_crc32("123456789",9)==UINT32_C(0xcbf43926));
    printf("QSPI_READONLY_TEST_PASS: %u transfers/%u readchunks;read-only whitelist/ranges/failures/CRC\n",calls,reads);
    return 0;
}
