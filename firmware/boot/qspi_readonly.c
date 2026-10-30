#include "qspi_readonly.h"
#include <string.h>
static int exchange(BootFlashReadOnly *flash,unsigned count){
    unsigned cmd=flash->tx[0];
    if(cmd!=0x03&&cmd!=0x05&&cmd!=0x35&&cmd!=0x15&&cmd!=0x9f)return -1;
    return flash->transfer(flash->context,flash->tx,flash->rx,count)?-1:0;
}
int boot_flash_read_id(BootFlashReadOnly *flash,uint8_t id[3]){
    if(!flash||!flash->transfer||!id)return -1;
    memset(flash->tx,0,4);flash->tx[0]=0x9f;
    if(exchange(flash,4))return -1;
    memcpy(id,flash->rx+1,3);return 0;
}
int boot_flash_read_status(BootFlashReadOnly *flash,unsigned reg,uint8_t *status){
    static const uint8_t commands[]={0x05,0x35,0x15};
    if(!flash||!flash->transfer||!status||reg<1||reg>3)return -1;
    flash->tx[0]=commands[reg-1];flash->tx[1]=0;
    if(exchange(flash,2))return -1;
    *status=flash->rx[1];return 0;
}
int boot_flash_read(BootFlashReadOnly *flash,uint32_t address,uint8_t *out,size_t bytes){
    if(!flash||!flash->transfer||address>BOOT_FLASH_CAPACITY||
       bytes>BOOT_FLASH_CAPACITY-address||(!out&&bytes))return -1;
    while(bytes){
        unsigned n=bytes>BOOT_FLASH_CHUNK?BOOT_FLASH_CHUNK:(unsigned)bytes;
        memset(flash->tx,0,n+4);flash->tx[0]=0x03;
        flash->tx[1]=(uint8_t)(address>>16);flash->tx[2]=(uint8_t)(address>>8);flash->tx[3]=(uint8_t)address;
        if(exchange(flash,n+4))return -1;
        memcpy(out,flash->rx+4,n);out+=n;address+=n;bytes-=n;
    }
    return 0;
}
uint32_t boot_flash_crc32(const void *data,size_t bytes){
    const uint8_t *p=data;uint32_t crc=UINT32_MAX;
    if(!data&&bytes)return 0;
    for(size_t i=0;i<bytes;i++){
        crc^=p[i];
        for(unsigned bit=0;bit<8;bit++)crc=(crc>>1)^((crc&1)?UINT32_C(0xedb88320):0);
    }
    return ~crc;
}
