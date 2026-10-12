/******************************************************************************
 * @file     ota_update.h
 * @brief    OTA升级流程封装
 * @version  V1.0
 ******************************************************************************/

#ifndef __OTA_UPDATE_H__
#define __OTA_UPDATE_H__

#include <stdint.h>

/**
 * @brief  执行OTA升级流程
 *         读BCB→确定备用分区→擦除→Ymodem接收→校验→切换BCB→复位
 * @return 正常不返回(升级成功会复位), 失败返回负数错误码
 */
int ota_do_upgrade(void);

#endif /* __OTA_UPDATE_H__ */
