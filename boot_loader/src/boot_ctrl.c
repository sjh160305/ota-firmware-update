/******************************************************************************
 * @file     boot_ctrl.c
 * @brief    A/B双分区启动控制 - 参数区双备份读写与分区操作实现
 * @version  V1.0
 ******************************************************************************/

#include <string.h>
#include <stdio.h>
#include <drv/spiflash.h>
#include <drv/common.h>
#include "boot_ctrl.h"

/*===========================================================================
 * CRC32 (标准MPEG-2多项式 0x04C11DB7, 初始值0xFFFFFFFF)
 *===========================================================================*/
static uint32_t crc32_table[256];
static uint8_t  crc32_table_ready = 0;

static void crc32_init_table(void)
{
    uint32_t c;
    for (uint32_t i = 0; i < 256; i++) {
        c = i;
        for (int j = 0; j < 8; j++) {
            c = (c & 1) ? (0xEDB88320U ^ (c >> 1)) : (c >> 1);
        }
        crc32_table[i] = c;
    }
    crc32_table_ready = 1;
}

uint32_t crc32_calc(const uint8_t *data, uint32_t len)
{
    if (!crc32_table_ready) {
        crc32_init_table();
    }
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < len; i++) {
        crc = (crc >> 8) ^ crc32_table[(crc ^ data[i]) & 0xFF];
    }
    return crc ^ 0xFFFFFFFF;
}

uint32_t crc32_flash(uint32_t addr, uint32_t len)
{
    /* XIP模式下Flash地址可直接访问 */
    return crc32_calc((const uint8_t *)addr, len);
}

/*===========================================================================
 * Flash安全操作 (带地址边界校验, 禁止越界到Boot区)
 * 注意: CSI SPI Flash API使用相对于Flash基地址的offset, 非绝对地址
 * XIP模式下擦写函数必须放入RAM执行
 *===========================================================================*/

#define ATTRIBUTE_DATA __attribute__((section(".ram.code")))

extern csi_spiflash_t g_spiflash;

static int addr_check(uint32_t addr, uint32_t size)
{
    if (addr < FLASH_WRITABLE_START) return -1;
    if ((addr + size) > FLASH_WRITABLE_END) return -1;
    if (addr < BOOT_ADDR + BOOT_SIZE) return -1;
    return 0;
}

ATTRIBUTE_DATA int safe_flash_erase(uint32_t addr, uint32_t size)
{
    if (addr_check(addr, size) != 0) {
        return -1;
    }
    uint32_t offset = addr - FLASH_BASE_ADDR;
    csi_error_t ret = csi_spiflash_erase(&g_spiflash, offset, size);
    return (ret == CSI_OK) ? 0 : -1;
}

ATTRIBUTE_DATA int safe_flash_write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    if (addr_check(addr, len) != 0) {
        return -1;
    }
    uint32_t offset = addr - FLASH_BASE_ADDR;
    int32_t ret = csi_spiflash_program(&g_spiflash, offset, data, len);
    return (ret == (int32_t)len) ? 0 : -1;
}

int safe_flash_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    if (addr < FLASH_BASE_ADDR || (addr + len) > FLASH_WRITABLE_END) {
        return -1;
    }
    memcpy(buf, (const void *)addr, len);
    return 0;
}

/*===========================================================================
 * 分区信息查询
 *===========================================================================*/

uint32_t bcb_get_part_addr(uint8_t part)
{
    return (part == PART_A) ? APP_A_ADDR : APP_B_ADDR;
}

uint32_t bcb_get_part_size(uint8_t part)
{
    return (part == PART_A) ? APP_A_SIZE : APP_B_SIZE;
}

uint8_t bcb_get_backup_part(uint8_t active)
{
    return (active == PART_A) ? PART_B : PART_A;
}

/*===========================================================================
 * 参数区 (BCB) 读写操作 - 双备份机制
 *===========================================================================*/

static uint32_t bcb_calc_crc(const boot_ctrl_t *bcb)
{
    /* CRC计算不包含结构体末尾的crc32字段 */
    return crc32_calc((const uint8_t *)bcb,
                      sizeof(boot_ctrl_t) - sizeof(uint32_t));
}

static int bcb_validate(const boot_ctrl_t *bcb)
{
    if (bcb->magic != BCB_MAGIC) return -1;
    if (bcb->active_part > PART_B) return -1;
    if (bcb_calc_crc(bcb) != bcb->crc32) return -1;
    return 0;
}

void bcb_init_default(boot_ctrl_t *bcb)
{
    memset(bcb, 0, sizeof(boot_ctrl_t));
    bcb->magic = BCB_MAGIC;
    bcb->version = BCB_VERSION;
    bcb->active_part = PART_A;
    bcb->upg_state = UPG_STATE_IDLE;
    bcb->part_a_valid = PART_INVALID;
    bcb->part_b_valid = PART_INVALID;
    bcb->part_a_run_confirm = RUN_CONFIRM_NONE;
    bcb->part_b_run_confirm = RUN_CONFIRM_NONE;
    bcb->retry_count = 0;
    bcb->crc32 = bcb_calc_crc(bcb);
}

int bcb_read(boot_ctrl_t *bcb)
{
    boot_ctrl_t tmp;

    /* 先读主参数区 */
    memcpy(&tmp, (const void *)BCB_MAIN_ADDR, sizeof(boot_ctrl_t));
    if (bcb_validate(&tmp) == 0) {
        memcpy(bcb, &tmp, sizeof(boot_ctrl_t));
        return 0;
    }
    printf("[BCB] Main BCB invalid, try backup...\n");

    /* 主区损坏，读备份参数区 */
    memcpy(&tmp, (const void *)BCB_BACKUP_ADDR, sizeof(boot_ctrl_t));
    if (bcb_validate(&tmp) == 0) {
        memcpy(bcb, &tmp, sizeof(boot_ctrl_t));
        /* 恢复主参数区 */
        safe_flash_erase(BCB_MAIN_ADDR, FLASH_SECTOR_SIZE);
        safe_flash_write(BCB_MAIN_ADDR, (const uint8_t *)bcb,
                         sizeof(boot_ctrl_t));
        printf("[BCB] Restored main BCB from backup\n");
        return 0;
    }

    /* 两份都损坏，初始化默认值 */
    printf("[BCB] Both BCB corrupted! Init default\n");
    bcb_init_default(bcb);
    return -1;
}

int bcb_write(const boot_ctrl_t *bcb)
{
    boot_ctrl_t tmp;
    memcpy(&tmp, bcb, sizeof(boot_ctrl_t));
    tmp.crc32 = bcb_calc_crc(&tmp);

    /* 先写备份区，再写主区，保证至少一份有效 */
    if (safe_flash_erase(BCB_BACKUP_ADDR, FLASH_SECTOR_SIZE) != 0) return -1;
    if (safe_flash_write(BCB_BACKUP_ADDR, (const uint8_t *)&tmp,
                         sizeof(boot_ctrl_t)) != 0) return -1;

    if (safe_flash_erase(BCB_MAIN_ADDR, FLASH_SECTOR_SIZE) != 0) return -1;
    if (safe_flash_write(BCB_MAIN_ADDR, (const uint8_t *)&tmp,
                         sizeof(boot_ctrl_t)) != 0) return -1;

    return 0;
}

/*===========================================================================
 * 镜像校验
 *===========================================================================*/

int bcb_verify_image(uint32_t part_addr)
{
    volatile uint8_t *magic = (volatile uint8_t *)(part_addr + IMG_MAGIC_OFFSET);

    /* 校验JiuChen魔术字 */
    if (magic[0] != 'J' || magic[1] != 'i' || magic[2] != 'u' ||
        magic[3] != 'C' || magic[4] != 'h' || magic[5] != 'e' ||
        magic[6] != 'n') {
        return -1;
    }

    /* 读取镜像大小,检查合理性 */
    /* 头部布局: JiuChen(7) + chipID(2) + reserved(4) + imgOffset(4) = 偏移0x11处为imgSize */
    uint32_t img_size = *(volatile uint32_t *)(part_addr + 0x11);
    if (img_size == 0 || img_size == 0xFFFFFFFF || img_size > APP_A_SIZE) {
        return -2;
    }

    return 0;
}

uint32_t bcb_get_image_entry(uint32_t part_addr)
{
    return *(volatile uint32_t *)(part_addr + IMG_ENTRY_OFFSET);
}
