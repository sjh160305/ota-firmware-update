/******************************************************************************
 * @file     ymodem.c
 * @brief    Ymodem-1K协议接收端实现
 * @version  V2.0
 ******************************************************************************/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <drv/uart.h>
#include "ymodem.h"

extern csi_uart_t console_uart;

/*===========================================================================
 * UART 收发封装
 *===========================================================================*/
static int uart_receive_byte(uint8_t *ch, uint32_t timeout_ms)
{
    int32_t ret = csi_uart_receive(&console_uart, ch, 1, timeout_ms);
    return (ret == 1) ? 0 : -1;
}

static void uart_send_byte(uint8_t ch)
{
    csi_uart_putc(&console_uart, ch);
}

/*===========================================================================
 * CRC16-CCITT (多项式0x1021, 初始值0x0000)
 *===========================================================================*/
static uint16_t crc16_ccitt(const uint8_t *data, uint32_t len)
{
    uint16_t crc = 0;
    while (len--) {
        crc ^= ((uint16_t)*data++) << 8;
        for (int i = 0; i < 8; i++) {
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc <<= 1;
        }
    }
    return crc;
}

/*===========================================================================
 * 帧接收
 *===========================================================================*/
static uint8_t s_packet_data[YMODEM_STX_DATA_SIZE];

/**
 * \brief      Receive one Ymodem packet
 *             接收一帧 Ymodem 数据。关键点：把"帧类型"与"帧序号"彻底分开——
 *             帧类型用返回值表达，帧序号通过 seq_out 出参带出。
 *             这样序号值（可能等于 4、6…）永远不会和控制码 EOT(0x04) 撞车，
 *             这是旧实现"第4包被误判为传输结束"这一致命缺陷的根因。
 * \param[out] data       数据段缓冲区（128 或 1024 字节）
 * \param[out] packet_len 本帧数据段长度
 * \param[out] seq_out    本帧序号（仅数据帧有效，可传 NULL）
 * \return     1=收到数据帧 / 0=收到 EOT / 负数=错误码（YMODEM_ERR_*）
 */
static int receive_packet(uint8_t *data, uint32_t *packet_len, uint8_t *seq_out)
{
    uint8_t header;
    uint8_t seq[2];
    uint16_t data_size;
    uint16_t crc_recv, crc_calc;

    if (uart_receive_byte(&header, YMODEM_DATA_TIMEOUT_MS) != 0) {
        return YMODEM_ERR_TIMEOUT;
    }

    switch (header) {
    case YMODEM_SOH:
        data_size = YMODEM_SOH_DATA_SIZE;
        break;
    case YMODEM_STX:
        data_size = YMODEM_STX_DATA_SIZE;
        break;
    case YMODEM_EOT:
        *packet_len = 0;
        return 0;                       /* EOT 用返回值 0 表示,不再复用 YMODEM_EOT */
    case YMODEM_CAN:
        if (uart_receive_byte(&header, YMODEM_DATA_TIMEOUT_MS) == 0) {
            if (header == YMODEM_CAN) return YMODEM_ERR_CANCEL;
        }
        return YMODEM_ERR_CANCEL;
    default:
        return YMODEM_ERR_SEQ;
    }

    /* 批量接收序号(2字节) */
    if (csi_uart_receive(&console_uart, seq, 2, YMODEM_DATA_TIMEOUT_MS) != 2) {
        return YMODEM_ERR_TIMEOUT;
    }

    if (seq[0] != (uint8_t)(~seq[1])) {
        return YMODEM_ERR_SEQ;
    }

    /* 批量接收数据段(128或1024字节) */
    if (csi_uart_receive(&console_uart, data, data_size, YMODEM_DATA_TIMEOUT_MS) != (int32_t)data_size) {
        return YMODEM_ERR_TIMEOUT;
    }

    /* 批量接收CRC(2字节) */
    uint8_t crc_buf[2];
    if (csi_uart_receive(&console_uart, crc_buf, 2, YMODEM_DATA_TIMEOUT_MS) != 2) {
        return YMODEM_ERR_TIMEOUT;
    }
    crc_recv = ((uint16_t)crc_buf[0] << 8) | crc_buf[1];

    crc_calc = crc16_ccitt(data, data_size);
    if (crc_calc != crc_recv) {
        return YMODEM_ERR_CRC;
    }

    *packet_len = data_size;
    if (seq_out != NULL) {
        *seq_out = seq[0];              /* 序号单独带出,不再混入返回值 */
    }
    return 1;                           /* 1 = 收到有效数据帧 */
}

/*===========================================================================
 * 解析第0包 (文件名 + 文件大小)
 *===========================================================================*/
static int parse_file_header(const uint8_t *data, ymodem_info_t *info)
{
    if (data[0] == 0) {
        return -1;
    }

    strncpy(info->filename, (const char *)data, sizeof(info->filename) - 1);
    info->filename[sizeof(info->filename) - 1] = '\0';

    const char *size_str = (const char *)data + strlen(info->filename) + 1;
    info->filesize = (uint32_t)strtoul(size_str, NULL, 10);

    return 0;
}

/*===========================================================================
 * 阶段1: Ymodem握手 — 发C等待发送端, 接收第0包, 解析文件名和大小
 * 注意: 此函数返回后还未ACK第0包, 等上层擦除完成后再调用receive_data发ACK
 *===========================================================================*/
int32_t ymodem_handshake(ymodem_info_t *info)
{
    uint32_t packet_len = 0;
    int ret;
    int retry;

    memset(info, 0, sizeof(ymodem_info_t));

    for (retry = 0; retry < YMODEM_C_MAX_RETRY; retry++) {
        uart_send_byte(YMODEM_C);
        /* 第0包(文件名包)现在按数据帧处理,返回1才算收到 */
        ret = receive_packet(s_packet_data, &packet_len, NULL);
        if (ret == 1) break;
        if (ret == YMODEM_ERR_CANCEL) return YMODEM_ERR_CANCEL;
    }
    if (retry >= YMODEM_C_MAX_RETRY) {
        printf("[YMODEM] Timeout waiting for sender\n");
        return YMODEM_ERR_TIMEOUT;
    }

    if (parse_file_header(s_packet_data, info) != 0) {
        uart_send_byte(YMODEM_ACK);
        return YMODEM_ERR_TIMEOUT;
    }

    printf("[YMODEM] File: %s, Size: %u bytes\n", info->filename, info->filesize);

    /* 立刻ACK第0包,防止上位机超时 */
    uart_send_byte(YMODEM_ACK);

    return YMODEM_OK;
}

/*===========================================================================
 * 阶段2: 数据接收 — ACK第0包, 发C, 逐包接收数据并回调写入
 *===========================================================================*/
int32_t ymodem_receive_data(ymodem_write_cb write_cb, uint32_t filesize)
{
    uint32_t packet_len = 0;
    uint32_t file_offset = 0;
    uint32_t remaining = filesize;
    uint8_t  expected_seq = 1;
    uint8_t  seq = 0;
    int      ret;
    int      retry;

    /* 发C请求数据 (ACK已在handshake阶段发送) */
    uart_send_byte(YMODEM_C);

    while (1) {
        retry = 0;
retry_packet:
        ret = receive_packet(s_packet_data, &packet_len, &seq);

        /* EOT处理 (返回0代表收到EOT) */
        if (ret == 0) {
            uart_send_byte(YMODEM_NAK);
            uart_receive_byte((uint8_t *)&ret, YMODEM_DATA_TIMEOUT_MS);
            uart_send_byte(YMODEM_ACK);
            uart_send_byte(YMODEM_C);

            /* 收尾包(空文件名包):返回1为数据帧且首字节为0 */
            ret = receive_packet(s_packet_data, &packet_len, &seq);
            if (ret == 1 && s_packet_data[0] == 0) {
                uart_send_byte(YMODEM_ACK);
            }
            break;
        }

        if (ret == YMODEM_ERR_CANCEL) {
            printf("[YMODEM] Cancelled by sender\n");
            return YMODEM_ERR_CANCEL;
        }

        if (ret < 0) {
            retry++;
            if (retry > YMODEM_PACKET_RETRY) {
                uart_send_byte(YMODEM_CAN);
                uart_send_byte(YMODEM_CAN);
                printf("[YMODEM] Too many errors, abort\n");
                return ret;
            }
            uart_send_byte(YMODEM_NAK);
            goto retry_packet;
        }

        /* 用单独带出的序号比对期望值,不再拿返回值当序号 */
        if (seq != expected_seq) {
            retry++;
            if (retry > YMODEM_PACKET_RETRY) {
                uart_send_byte(YMODEM_CAN);
                uart_send_byte(YMODEM_CAN);
                return YMODEM_ERR_SEQ;
            }
            uart_send_byte(YMODEM_NAK);
            goto retry_packet;
        }

        uint32_t write_len = (remaining < packet_len) ? remaining : packet_len;
        if (write_cb && write_len > 0) {
            if (write_cb(file_offset, s_packet_data, write_len) != 0) {
                uart_send_byte(YMODEM_CAN);
                uart_send_byte(YMODEM_CAN);
                printf("[YMODEM] Write error at offset 0x%X\n", file_offset);
                return YMODEM_ERR_WRITE;
            }
        }

        file_offset += write_len;
        remaining -= write_len;
        expected_seq++;
        uart_send_byte(YMODEM_ACK);
    }

    /* 返回真实写入长度,而非第0包声明的filesize,让上层能识别截断 */
    printf("[YMODEM] Transfer complete: %u bytes\n", file_offset);
    return (int32_t)file_offset;
}
