#ifndef BOOT_QSPI_READONLY_H
#define BOOT_QSPI_READONLY_H
#include <stddef.h>
#include <stdint.h>
#define BOOT_FLASH_CAPACITY UINT32_C(0x01000000)
#define BOOT_FLASH_CHUNK 1024u
typedef int (*BootFlashTransfer)(void *,const uint8_t *,uint8_t *,unsigned);
typedef struct {
    BootFlashTransfer transfer;
    void *context;
    uint8_t tx[BOOT_FLASH_CHUNK+4],rx[BOOT_FLASH_CHUNK+4];
} BootFlashReadOnly;
/* No write/erase/register-set API exists. Each exchange has a read whitelist.
 * Output buffers must not alias this workspace. Read failures may retain prior
 * completed chunks; a caller must not publish completion until full success. */
int boot_flash_read_id(BootFlashReadOnly *,uint8_t id[3]);
int boot_flash_read_status(BootFlashReadOnly *,unsigned reg,uint8_t *status);
int boot_flash_read(BootFlashReadOnly *,uint32_t address,uint8_t *out,size_t bytes);
uint32_t boot_flash_crc32(const void *data,size_t bytes);
#endif
