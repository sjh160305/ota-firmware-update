/******************************************************************************
 * @file     boot_ctrl.h
 * @brief    A/B双分区启动控制 - 参数区结构体与分区宏定义
 * @version  V1.0
 ******************************************************************************/

#ifndef __BOOT_CTRL_H__
#define __BOOT_CTRL_H__

#include <stdint.h>

/*===========================================================================
 * Flash分区地址定义 (2MB SPI NOR Flash, 4KB扇区对齐)
 *===========================================================================*/
#define FLASH_BASE_ADDR         0x18000000U
#define FLASH_TOTAL_SIZE        0x200000U       /* 2MB */
#define FLASH_SECTOR_SIZE       0x1000U         /* 4KB */
#define FLASH_PAGE_SIZE         256U

/* Bootloader区: 0x18000000 ~ 0x1800FFFF, 64KB */
#define BOOT_ADDR               0x18000000U
#define BOOT_SIZE               0x10000U

/* 参数区(主): 0x18010000 ~ 0x18010FFF, 4KB */
#define BCB_MAIN_ADDR           0x18010000U
#define BCB_MAIN_SIZE           FLASH_SECTOR_SIZE

/* 参数区(备): 0x18011000 ~ 0x18011FFF, 4KB */
#define BCB_BACKUP_ADDR         0x18011000U
#define BCB_BACKUP_SIZE         FLASH_SECTOR_SIZE

/* APP A区: 0x18012000 ~ 0x18108FFF, 988KB (0xF7000) */
#define APP_A_ADDR              0x18012000U
#define APP_A_SIZE              0xF7000U

/* APP B区: 0x18109000 ~ 0x181FFFFF, 988KB (0xF7000) */
#define APP_B_ADDR              0x18109000U
#define APP_B_SIZE              0xF7000U

/* 镜像头偏移定义 (复用startup.S中的JiuChen头部格式) */
#define IMG_MAGIC_OFFSET        0x200U          /* .text段起始, 紧跟向量表之后 */
#define IMG_MAGIC_LEN           7U              /* "JiuChen" */
#define IMG_ENTRY_OFFSET        (IMG_MAGIC_OFFSET + 0x40U) /* Reset_Handler地址偏移 */
#define IMG_SIZE_OFFSET         (IMG_MAGIC_OFFSET + 11U)   /* image size字段 */

/* 地址安全边界 */
#define FLASH_WRITABLE_START    BCB_MAIN_ADDR
#define FLASH_WRITABLE_END      (FLASH_BASE_ADDR + FLASH_TOTAL_SIZE)

/*===========================================================================
 * 参数区结构体定义
 *===========================================================================*/
#define BCB_MAGIC               0x4A43424CU     /* "JCBL" */
#define BCB_VERSION             0x0001U

/* 分区标识 */
#define PART_A                  0U
#define PART_B                  1U

/* 分区有效性标记 */
#define PART_INVALID            0x00U
#define PART_VALID              0x01U

/* 升级状态 */
#define UPG_STATE_IDLE          0x00U           /* 空闲，正常运行 */
#define UPG_STATE_UPDATING      0x01U           /* 升级中(正在写入) */
#define UPG_STATE_PENDING       0x02U           /* 待确认(已写入,等待首次启动确认) */

/* 运行确认标记 */
#define RUN_CONFIRM_NONE        0x00U           /* 未确认 */
#define RUN_CONFIRM_OK          0xA5U           /* 已确认运行正常 */

/* 回滚超时(秒) - Boot通过计数器实现 */
#define ROLLBACK_TIMEOUT_S      30U
#define ROLLBACK_MAX_RETRY      3U

typedef struct {
    uint32_t magic;             /* 魔数 BCB_MAGIC */
    uint16_t version;           /* 结构体版本号 */
    uint8_t  active_part;       /* 当前激活分区: PART_A / PART_B */
    uint8_t  upg_state;         /* 升级状态 */

    /* A区信息 */
    uint32_t part_a_fw_ver;     /* A区固件版本号 */
    uint32_t part_a_crc;        /* A区固件CRC32校验值 */
    uint8_t  part_a_valid;      /* A区有效性标记 */
    uint8_t  part_a_run_confirm;/* A区运行确认标记 */
    uint8_t  part_a_reserved[2];

    /* B区信息 */
    uint32_t part_b_fw_ver;     /* B区固件版本号 */
    uint32_t part_b_crc;        /* B区固件CRC32校验值 */
    uint8_t  part_b_valid;      /* B区有效性标记 */
    uint8_t  part_b_run_confirm;/* B区运行确认标记 */
    uint8_t  part_b_reserved[2];

    /* 回滚控制 */
    uint8_t  retry_count;       /* 启动重试计数 */
    uint8_t  reserved[3];

    uint32_t crc32;             /* 结构体自身CRC32 (不含此字段) */
} boot_ctrl_t;

/*===========================================================================
 * API声明
 *===========================================================================*/

/* 参数区操作 */
int      bcb_read(boot_ctrl_t *bcb);
int      bcb_write(const boot_ctrl_t *bcb);
void     bcb_init_default(boot_ctrl_t *bcb);

/* 分区信息查询 */
uint32_t bcb_get_part_addr(uint8_t part);
uint32_t bcb_get_part_size(uint8_t part);
uint8_t  bcb_get_backup_part(uint8_t active);

/* 镜像校验 */
int      bcb_verify_image(uint32_t part_addr);
uint32_t bcb_get_image_entry(uint32_t part_addr);

/* CRC32计算 */
uint32_t crc32_calc(const uint8_t *data, uint32_t len);
uint32_t crc32_flash(uint32_t addr, uint32_t len);

/* Flash安全操作 (带地址边界校验) */
int      safe_flash_erase(uint32_t addr, uint32_t size);
int      safe_flash_write(uint32_t addr, const uint8_t *data, uint32_t len);
int      safe_flash_read(uint32_t addr, uint8_t *buf, uint32_t len);

#endif /* __BOOT_CTRL_H__ */
