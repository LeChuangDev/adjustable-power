/* =========================================================
 * ble_protocol.c — 协议编解码实现
 *
 * 功能：
 *   - CRC16 计算
 *   - 组帧（编）
 *   - 解析帧（解）
 * ========================================================= */

#include <string.h>
#include "ble_protocol.h"

/* ---- CRC16 计算（标准 CRC16-CCITT）---- */
static uint16_t crc16_update(uint16_t crc, uint8_t b)
{
    crc ^= (uint16_t)b << 8;
    for (int i = 0; i < 8; i++) {
        if (crc & 0x8000) crc = (crc << 1) ^ 0x1021;
        else crc <<= 1;
    }
    return crc;
}

uint16_t proto_crc16(const uint8_t *data, int len)
{
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < len; i++) crc = crc16_update(crc, data[i]);
    return crc;
}

/* ---- 组帧：填好帧头/尾/CRC ---- */
int proto_build_frame(uint8_t type, const uint8_t *payload, int len,
                      uint8_t *out, int out_cap)
{
    if (out == NULL) return -1;
    if (len < 0 || len > PROTO_MAX_PAYLOAD) return -1;
    if (len > 0 && payload == NULL) return -1;

    int total = PROTO_FRAME_TOTAL(len);
    if (out_cap < total) return -1;

    proto_frame_t *f = (proto_frame_t *)out;
    memset(f, 0, total);          /* 只清有效字节，不必清整个 512B 负载区 */
    f->header  = PROTO_HEADER;
    f->version = PROTO_VERSION;
    f->type    = type;
    f->len     = (uint16_t)len;
    if (len > 0) memcpy(f->payload, payload, len);

    /* CRC 覆盖：header ~ payload 最后一个字节 */
    f->crc16 = proto_crc16(out, PROTO_CRC_COVER(len));
    f->tail  = PROTO_TAIL;

    return total;
}

/* ---- 校验并解析收到的帧 ---- */
int proto_parse_frame(const uint8_t *data, int data_len, proto_frame_t *out)
{
    if (data == NULL || out == NULL) return -1;

    /* 必须先够读满长度字段（偏移 3/4），否则下面 data[4] 会越界 */
    if (data_len < PROTO_HDR_SIZE) return -1;
    if (data[0] != PROTO_HEADER) return -1;

    uint16_t len = (uint16_t)(data[3] | (data[4] << 8));
    if (len > PROTO_MAX_PAYLOAD) return -1;

    int total = PROTO_FRAME_TOTAL(len);
    if (data_len < total) return -1;

    memcpy(out, data, total);
    if (out->tail != PROTO_TAIL) return -3;
    if (proto_crc16(data, PROTO_CRC_COVER(len)) != out->crc16) return -2;

    return 0;
}