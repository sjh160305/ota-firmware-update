/******************************************************************************
 * @file     ota_update.c
 * @brief    OTA升级流程封装 - 握手→按需擦除→接收写入
 * @version  V2.0
 ******************************************************************************/

#include <stdio.h>
#include <string.h>
#include <drv/spiflash.h>
#include "boot_ctrl.h"
#include "ymodem.h"
#include "ota_update.h"
#include "csi_core.h"
#include <drv/uart.h>
#include <drv/pin.h>

extern csi_spiflash_t g_spiflash;

/*===========================================================================
 * 升级写入上下文
 *===========================================================================*/
static uint32_t s_target_addr = 0;

/* XIP模式下写入函数放入RAM执行 */
#define ATTRIBUTE_DATA __attribute__((section(".ram.code")))

ATTRIBUTE_DATA static int flash_erase_ram(uint32_t offset, uint32_t size)
{
    csi_error_t ret = csi_spiflash_erase(&g_spiflash, offset, size);
    return (ret == CSI_OK) ? 0 : -1;
}

ATTRIBUTE_DATA static int flash_write_ram(uint32_t offset, const uint8_t *data, uint32_t len)
{
    int32_t ret = csi_spiflash_program(&g_spiflash, offset, data, len);
    return (ret == (int32_t)len) ? 0 : -1;
}

static int ota_write_callback(uint32_t offset, const uint8_t *data, uint32_t len)
{
    uint32_t flash_offset = (s_target_addr - FLASH_BASE_ADDR) + offset;
    int ret;

    csi_icache_disable();
    csi_dcache_clean();
    csi_dcache_disable();

    ret = flash_write_ram(flash_offset, data, len);

    csi_icache_enable();
    csi_dcache_enable();
    return ret;
}

/*===========================================================================
 * 软件复位
 *===========================================================================*/
static void software_reset(void)
{
    uint8_t tmp[4];

    printf("[OTA] Rebooting...\n\n");
    mdelay(100);

    /* 跳回 boot 前同样复位 Flash 连续读模式 */
    csi_spiflash_read(&g_spiflash, 0x0U, tmp, sizeof(tmp));

    /* 停掉串口 */
    extern csi_uart_t console_uart;
    csi_uart_uninit(&console_uart);

    __disable_irq();
    csi_dcache_clean();
    csi_icache_invalid();
    csi_dcache_disable();
    csi_icache_disable();
    __asm volatile("fence.i");
    __asm volatile("fence");

    uint32_t boot_entry = *(volatile uint32_t *)(BOOT_ADDR + 0x40);
    __asm volatile("jr %0\n" :: "r"(boot_entry));
}

/*===========================================================================
 * OTA升级主流程
 * 流程: 读BCB确定目标分区 → 握手拿文件大小 → 按需擦除 → 接收写入 → 校验 → 切换BCB → 复位
 *===========================================================================*/
int ota_do_upgrade(void)
{
    boot_ctrl_t bcb;
    uint8_t  target_part;
    uint32_t target_addr;
    uint32_t target_size;
    uint32_t target_offset;
    ymodem_info_t ym_info;
    int32_t recv_size;

    /* 读取BCB确定目标分区 */
    bcb_read(&bcb);
    target_part   = bcb_get_backup_part(bcb.active_part);
    target_addr   = bcb_get_part_addr(target_part);
    target_size   = bcb_get_part_size(target_part);
    target_offset = target_addr - FLASH_BASE_ADDR;

    printf("\n[OTA] ====== OTA UPGRADE START ======\n");
    printf("[OTA] Current active: %s\n", (bcb.active_part == PART_A) ? "A" : "B");
    printf("[OTA] Target partition: %s (0x%08X)\n",
           (target_part == PART_A) ? "A" : "B", target_addr);

    /* 阶段1: Ymodem握手, 获取文件名和文件大小 */
    printf("[OTA] Waiting for Ymodem transfer...\n");
    if (ymodem_handshake(&ym_info) != YMODEM_OK) {
        printf("[OTA] ERR: Handshake failed\n");
        return -1;
    }

    /* 校验文件大小合法性 */
    if (ym_info.filesize == 0 || ym_info.filesize > target_size) {
        printf("[OTA] ERR: Invalid file size %u (max %u)\n",
               ym_info.filesize, target_size);
        /* 通知发送端取消 */
        extern csi_uart_t console_uart;
        csi_uart_putc(&console_uart, YMODEM_CAN);
        csi_uart_putc(&console_uart, YMODEM_CAN);
        return -2;
    }
    
	

    /* 计算实际需要擦除的大小: 按扇区向上对齐 */
    uint32_t erase_size = (ym_info.filesize + FLASH_SECTOR_SIZE - 1)
                          & ~(FLASH_SECTOR_SIZE - 1);
    
    printf("[OTA] File: %s, %u bytes → erase %u KB\n",
           ym_info.filename, ym_info.filesize, erase_size / 1024);

    /* 阶段2: 按需擦除, 关Cache后在RAM中执行 */
    printf("[OTA] Erasing %u KB...\n", erase_size / 1024);


    csi_icache_disable();
    csi_dcache_clean();
    csi_dcache_disable();

    int erase_ret = flash_erase_ram(target_offset, erase_size);

    csi_icache_enable();
    csi_dcache_enable();

    if (erase_ret != 0) {
        printf("[OTA] ERR: Erase failed\n");
        return -3;
    }
    printf("[OTA] Erase complete\n");

    /* 阶段3: 接收数据并写入 */
    s_target_addr = target_addr;
    recv_size = ymodem_receive_data(ota_write_callback, ym_info.filesize);

    if (recv_size <= 0) {
        printf("[OTA] ERR: Receive failed (code %d)\n", recv_size);
        return -4;
    }

    /* 实际写入长度必须等于第0包声明的大小,否则说明传输被截断,拒绝升级 */
    if ((uint32_t)recv_size != ym_info.filesize) {
        printf("[OTA] ERR: Truncated! got %u, expect %u\n",
               (uint32_t)recv_size, ym_info.filesize);
        extern csi_uart_t console_uart;
        csi_uart_putc(&console_uart, YMODEM_CAN);
        csi_uart_putc(&console_uart, YMODEM_CAN);
        return -4;
    }
    printf("[OTA] Received %u bytes\n", (uint32_t)recv_size);

    /* 阶段4: 刷新DCache，保证后续读Flash是真实写入的数据 */
    csi_dcache_invalid_range((uint32_t *)target_addr, (int32_t)recv_size);

    /* 阶段5: 校验写入的镜像 */
    printf("[OTA] Verifying image...\n");
    if (bcb_verify_image(target_addr) != 0) {
        printf("[OTA] ERR: Image verification failed!\n");
        return -5;
    }
    printf("[OTA] Image verified OK\n");

    /* 阶段6: 更新BCB, 切换激活分区, 设置待确认状态 */
    bcb.active_part  = target_part;
    bcb.upg_state    = UPG_STATE_PENDING;
    bcb.retry_count  = 0;



	
    if (target_part == PART_A) {
        bcb.part_a_valid       = PART_VALID;
        bcb.part_a_crc         = crc32_flash(target_addr, (uint32_t)recv_size);
        bcb.part_a_run_confirm = RUN_CONFIRM_NONE;
    } else {
        bcb.part_b_valid       = PART_VALID;
        bcb.part_b_crc         = crc32_flash(target_addr, (uint32_t)recv_size);
        bcb.part_b_run_confirm = RUN_CONFIRM_NONE;
    }

    if (bcb_write(&bcb) != 0) {
        printf("[OTA] ERR: BCB write failed!\n");
        return -6;
    }

    printf("[OTA] Switched active to partition %s\n",
           (target_part == PART_A) ? "A" : "B");
    printf("[OTA] ====== UPGRADE SUCCESS ======\n");

    /* 阶段7: 复位从新分区启动 */
    software_reset();

    return 0;
}
