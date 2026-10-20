#include <string.h>
#include "ota_protocol.h"

/* =========================================================
 * CRC16-CCITT（初始 0xFFFF，与上位机一致）
 * ========================================================= */
uint16_t ota_crc16(const uint8_t *data, int len)
{
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

/* =========================================================
 * 组帧：填头、CRC、尾
 * ========================================================= */
int ota_build_frame(uint8_t type, uint16_t seq,
                    const uint8_t *payload, uint16_t len,
                    uint8_t *out, int out_cap)
{
    if (len > OTA_MAX_PAYLOAD) return -1;

    int total = OTA_HEADER_SIZE + len + 1;
    if (out_cap < total) return -1;

    ota_frame_hdr_t *hdr = (ota_frame_hdr_t *)out;
    hdr->magic = OTA_MAGIC;
    hdr->type  = type;
    hdr->seq   = seq;
    hdr->len   = len;
    hdr->crc16 = 0;

    if (len > 0) {
        memcpy(out + OTA_HEADER_SIZE, payload, len);
    }
    out[OTA_HEADER_SIZE + len] = OTA_TAIL;

    /* CRC 覆盖 header + payload */
    hdr->crc16 = ota_crc16(out, OTA_HEADER_SIZE + len);
    return total;
}

/* =========================================================
 * 解析帧：校验 magic、tail、crc
 * ========================================================= */
int ota_parse_frame(const uint8_t *buf, int buf_len, ota_frame_t *out)
{
    if (buf_len < OTA_HEADER_SIZE + 1) return 0;

    ota_frame_hdr_t *hdr = (ota_frame_hdr_t *)buf;
    if (hdr->magic != OTA_MAGIC) return -1;

    int total = OTA_HEADER_SIZE + hdr->len + 1;
    if (hdr->len > OTA_MAX_PAYLOAD) return -1;
    if (buf_len < total) return 0;
    if (buf[total - 1] != OTA_TAIL) return -1;

    /* CRC 校验 */
    uint16_t crc = ota_crc16(buf, OTA_HEADER_SIZE + hdr->len);
    if (crc != hdr->crc16) return -1;

    memcpy(out, buf, total);
    return total;
}