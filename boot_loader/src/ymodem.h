/******************************************************************************
 * @file     ymodem.h
 * @brief    Ymodem-1K协议接收端实现
 * @version  V2.0
 ******************************************************************************/

#ifndef __YMODEM_H__
#define __YMODEM_H__

#include <stdint.h>

/* Ymodem协议控制字符 */
#define YMODEM_SOH          0x01    /* 128字节数据帧头 */
#define YMODEM_STX          0x02    /* 1024字节数据帧头 */
#define YMODEM_EOT          0x04    /* 传输结束 */
#define YMODEM_ACK          0x06    /* 确认 */
#define YMODEM_NAK          0x15    /* 否认 */
#define YMODEM_CAN          0x18    /* 取消传输 */
#define YMODEM_C            0x43    /* 请求CRC模式 */

/* 帧大小定义 */
#define YMODEM_SOH_DATA_SIZE    128
#define YMODEM_STX_DATA_SIZE    1024
#define YMODEM_PACKET_OVERHEAD  5       /* header(1)+seq(1)+seqC(1)+crc(2) */
#define YMODEM_MAX_PACKET_SIZE  (YMODEM_STX_DATA_SIZE + YMODEM_PACKET_OVERHEAD)

/* 超时与重试 */
#define YMODEM_C_TIMEOUT_MS     1000    /* 发送'C'后等待响应超时 */
#define YMODEM_C_MAX_RETRY      60      /* 最多发60次'C' (60秒) */
#define YMODEM_DATA_TIMEOUT_MS  2000    /* 数据帧内字节间超时 */
#define YMODEM_PACKET_RETRY     3       /* 单包最大重试次数 */

/* 错误码 */
#define YMODEM_OK               0
#define YMODEM_ERR_TIMEOUT      -1
#define YMODEM_ERR_CRC          -2
#define YMODEM_ERR_SEQ          -3
#define YMODEM_ERR_CANCEL       -4
#define YMODEM_ERR_WRITE        -5
#define YMODEM_ERR_OVERFLOW     -6

/* 接收回调: 每收到一包数据调用, offset为文件内偏移, 返回0成功/-1失败 */
typedef int (*ymodem_write_cb)(uint32_t offset, const uint8_t *data, uint32_t len);

/* Ymodem接收结果 */
typedef struct {
    char     filename[64];      /* 接收到的文件名 */
    uint32_t filesize;          /* 接收到的文件大小 */
} ymodem_info_t;

/**
 * @brief  Ymodem握手阶段: 发'C'等待发送端,接收并解析第0包(文件名+大小)
 * @param  info  输出文件名和文件大小
 * @return 0成功, 负数错误码
 */
int32_t ymodem_handshake(ymodem_info_t *info);

/**
 * @brief  Ymodem数据接收阶段: 在握手成功且擦除完成后调用
 *         发送ACK+C确认第0包,然后逐包接收数据
 * @param  write_cb  数据写入回调
 * @param  filesize  第0包中解析到的文件大小
 * @return 实际接收字节数, 失败返回负数错误码
 */
int32_t ymodem_receive_data(ymodem_write_cb write_cb, uint32_t filesize);

#endif /* __YMODEM_H__ */
