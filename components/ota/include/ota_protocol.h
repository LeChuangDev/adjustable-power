#ifndef OTA_PROTOCOL_H
#define OTA_PROTOCOL_H

#include <stdint.h>

/* =========================================================
 * ota_protocol.h — 升级协议定义（ESP32 端与上位机共用）
 *
 * 帧格式（小端）：
 *   magic(1) type(1) seq(2) len(2) crc16(2) payload(len) tail(1)
 *   共 8 + len + 1 字节
 * ========================================================= */

#define OTA_MAGIC       0xA5
#define OTA_TAIL        0x5A
#define OTA_HEADER_SIZE 8
#define OTA_MAX_PAYLOAD 4096        /* 数据块 4KB（上位机 CHUNK 必须一致） */

/* ---- 帧类型 ---- */
typedef enum {
    OTA_TYPE_AUTH      = 0x01,   /* 认证请求（上位机→设备） */
    OTA_TYPE_AUTH_ACK  = 0x02,   /* 认证应答 */
    OTA_TYPE_START     = 0x10,   /* 开始升级：携带总长+SHA256 */
    OTA_TYPE_START_ACK = 0x11,   /* 开始应答 */
    OTA_TYPE_DATA      = 0x20,   /* 数据块 */
    OTA_TYPE_ACK       = 0x21,   /* 数据确认 */
    OTA_TYPE_NAK       = 0x22,   /* 数据重传请求 */
    OTA_TYPE_END       = 0x30,   /* 结束：携带整包 SHA256 */
    OTA_TYPE_END_ACK   = 0x31,   /* 结束应答 */
    OTA_TYPE_ERR       = 0xFF,   /* 错误 */
} ota_frame_type_t;

/* ---- 帧头 ---- */
#pragma pack(push, 1)
typedef struct {
    uint8_t  magic;
    uint8_t  type;
    uint16_t seq;
    uint16_t len;
    uint16_t crc16;
} ota_frame_hdr_t;

/* ---- START 帧负载：总长 + 整包 SHA256 ---- */
typedef struct {
    uint32_t total_len;
    uint8_t  sha256[32];
} ota_start_payload_t;
#pragma pack(pop)

/* ---- 完整帧 ---- */
typedef struct {
    ota_frame_hdr_t hdr;
    uint8_t payload[OTA_MAX_PAYLOAD];
    uint8_t tail;
} ota_frame_t;

/* CRC16-CCITT（与上位机一致） */
uint16_t ota_crc16(const uint8_t *data, int len);

/* 组帧，返回完整帧长度；失败返回负数 */
int ota_build_frame(uint8_t type, uint16_t seq,
                    const uint8_t *payload, uint16_t len,
                    uint8_t *out, int out_cap);

/* 解析帧：返回帧长度；0=不完整；负数=错误 */
int ota_parse_frame(const uint8_t *buf, int buf_len, ota_frame_t *out);

#endif