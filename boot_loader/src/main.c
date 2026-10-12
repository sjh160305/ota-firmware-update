/******************************************************************************
 * @file     main.c
 * @brief    TD1601 Bootloader - A/B双分区启动控制
 * @version  V4.0
 ******************************************************************************/

#include <stdio.h>
#include <stdint.h>
#include <drv/uart.h>
#include <drv/gpio.h>
#include <drv/pin.h>
#include <drv/spiflash.h>
#include "board_config.h"
#include "board_init.h"
#include "csi_core.h"
#include "boot_ctrl.h"
#include "ota_update.h"

/*===========================================================================
 * 全局变量
 *===========================================================================*/
extern csi_spiflash_t g_spiflash;  /* 定义在system.c中 */
static boot_ctrl_t g_bcb;
static csi_gpio_t g_key_gpio;

/* 升级触发串口指令 */
#define UPG_CMD             "UPDATE\r\n"
#define UPG_CMD_LEN         8

/* PB20 按键长按时间(ms) */
#define KEY_LONG_PRESS_MS   2000
#define KEY_CHECK_INTERVAL  50

/*===========================================================================
 * 按键检测 (PB20)
 *===========================================================================*/
static void key_gpio_init(void)
{
    csi_pin_set_mux(PORTB, PIN20, PIN_FUNC_GPIO);
    csi_gpio_init(&g_key_gpio, PORTB);
    csi_gpio_dir(&g_key_gpio, PIN20, GPIO_DIRECTION_INPUT);
    csi_gpio_mode(&g_key_gpio, PIN20, GPIO_MODE_PULLUP);
}

static int key_is_pressed(void)
{
    uint32_t state = csi_gpio_read(&g_key_gpio, PIN20);
    return (state & PIN20) ? 0 : 1;  /* 低电平按下 */
}

static int key_long_press_detect(void)
{
    uint32_t press_time = 0;

    if (!key_is_pressed()) return 0;

    while (key_is_pressed()) {
        mdelay(KEY_CHECK_INTERVAL);
        press_time += KEY_CHECK_INTERVAL;
        if (press_time >= KEY_LONG_PRESS_MS) {
            return 1;
        }
    }
    return 0;
}

/*===========================================================================
 * 串口升级指令检测
 *===========================================================================*/
extern csi_uart_t console_uart;

static int check_uart_upgrade_cmd(void)
{
    uint8_t ch;
    int32_t ret;

    /* Boot启动后1秒窗口期，检测是否收到'U'字符 */
    printf("[BOOT] Press 'U' within 1s to enter upgrade mode...\n");
    ret = csi_uart_receive(&console_uart, &ch, 1, 5000);
    if (ret == 1 && ch == 'U') {
        return 1;
    }
    return 0;
}

/*===========================================================================
 * 动态跳转函数
 *===========================================================================*/
static void jump_to_partition(uint32_t part_addr)
{
    uint32_t entry = bcb_get_image_entry(part_addr);
    uint8_t  tmp[4];

    printf("[BOOT] Target partition: 0x%08X\n", part_addr);
    printf("[BOOT] Entry point: 0x%08X\n", entry);

    if (entry == 0xFFFFFFFF || entry == 0x00000000) {
        printf("[BOOT] ERR: Invalid entry!\n");
        return;
    }

    /*
     * 跳转前把 SPI Flash 踢出"连续读/XIP 残留模式"：
     *   温复位(按键复位, flash 不掉电)后, flash 的连续读模式位会残留,
     *   APP 侧 sys_spiflash_init 从残留态初始化会导致 XIP 取指挂死。
     *   主动发一条 indirect 普通读命令, 让每条启动路径都落到统一的已知状态。
     */
    csi_spiflash_read(&g_spiflash, 0x0U, tmp, sizeof(tmp));

    /* 停掉串口, 防跳转后残留 RX/中断状态污染 APP */
    csi_uart_uninit(&console_uart);

    /* 跳转前完整环境清理 */
    __disable_irq();
    csi_dcache_clean();
    csi_icache_invalid();       /* 作废旧指令缓存, 防残留指令 */
    csi_dcache_disable();
    csi_icache_disable();

    /* 内存屏障 */
    __asm volatile("fence.i");
    __asm volatile("fence");

    __asm volatile("jr %0\n" :: "r"(entry));
}

/*===========================================================================
 * 升级模式入口
 *===========================================================================*/
static void enter_upgrade_mode(void)
{
    printf("\n[BOOT] ========== UPGRADE MODE ==========\n");
    ota_do_upgrade();
    /* ota_do_upgrade成功后会复位，不会返回到这里 */
    printf("[BOOT] Upgrade failed, system halted\n");
    while (1) { mdelay(5000); }
}

/*===========================================================================
 * 主函数
 *===========================================================================*/
int main(void)
{
    uint8_t  target_part;
    uint32_t target_addr;
    int      img_valid;

    board_init();
    key_gpio_init();

    printf("\n");
    printf("====================================\n");
    printf("  TD1601 Bootloader V4.0 (A/B OTA) \n");
    printf("====================================\n");

    /* SPI Flash已在system.c中初始化 */
    printf("[BOOT] Flash initialized\n");

    /* 读取参数区 */
    if (bcb_read(&g_bcb) != 0) {
        printf("[BOOT] WARN: BCB corrupted, using default\n");
        /* 首次运行或参数区损坏，将默认值写入Flash */
        bcb_write(&g_bcb);
        printf("[BOOT] Default BCB written to flash\n");
    }

    printf("[BOOT] Active partition: %s\n",
           (g_bcb.active_part == PART_A) ? "A" : "B");
    printf("[BOOT] Upgrade state: 0x%02X\n", g_bcb.upg_state);

    /* 检测按键触发升级 (PB20长按2秒) */
    if (key_long_press_detect()) {
        printf("[BOOT] Key long pressed! Entering upgrade mode\n");
        enter_upgrade_mode();
    }

    /* 检测串口升级指令 */
    if (check_uart_upgrade_cmd()) {
        enter_upgrade_mode();
        /* 升级完成后会重启，不会执行到这里 */
    }

    /* 启动选区逻辑 */
    target_part = g_bcb.active_part;
    target_addr = bcb_get_part_addr(target_part);

    /* 校验激活分区镜像 */
    img_valid = bcb_verify_image(target_addr);
    if (img_valid == 0) {
        printf("[BOOT] Partition %s verified OK\n",
               (target_part == PART_A) ? "A" : "B");

        /* 待确认状态处理: 首次启动计数检查 */
        if (g_bcb.upg_state == UPG_STATE_PENDING) {
            printf("[BOOT] First boot after upgrade, waiting for APP confirm...\n");
            /* APP成功启动后会写运行确认标记 */
        }

        mdelay(10);
        jump_to_partition(target_addr);

        /* 跳转失败，不应该到这里 */
        printf("[BOOT] ERR: Jump failed!\n");
    } else {
        printf("[BOOT] ERR: Partition %s invalid (code %d)!\n",
               (target_part == PART_A) ? "A" : "B", img_valid);
    }

    /* 激活分区失败，尝试备用分区 */
    printf("[BOOT] Try backup partition...\n");
    target_part = bcb_get_backup_part(g_bcb.active_part);
    target_addr = bcb_get_part_addr(target_part);

    img_valid = bcb_verify_image(target_addr);
    if (img_valid == 0) {
        printf("[BOOT] Backup partition %s verified OK\n",
               (target_part == PART_A) ? "A" : "B");

        /* 自动回滚: 切换激活分区 */
        g_bcb.active_part = target_part;
        g_bcb.upg_state = UPG_STATE_IDLE;
        g_bcb.retry_count = 0;
        bcb_write(&g_bcb);
        printf("[BOOT] Auto rollback to partition %s\n",
               (target_part == PART_A) ? "A" : "B");

        mdelay(10);
        jump_to_partition(target_addr);
    }

    /* 两个分区都无效，进入升级模式 */
    printf("[BOOT] ERR: Both partitions invalid!\n");
    printf("[BOOT] Entering upgrade mode...\n");
    enter_upgrade_mode();

    while (1) {
        printf("[BOOT] System halted.\n");
        mdelay(5000);
    }

    return 0;
}