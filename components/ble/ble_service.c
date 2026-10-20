/* =========================================================
 * ble_service.c — BLE 业务层实现
 *
 * 职责：把传输层收到的原始字节解析成命令，分发给上层回调。
 *
 * 命令帧格式见 ble_protocol.h：
 *     [0xAA][cmd][payload...]
 *
 * ⚠️ 全工程只有这一处做命令字节的解析。上层拿到的是
 *    ble_cmd_evt_t 语义事件，不再接触裸字节，避免同一
 *    字节序列在不同文件里被赋予不同含义。
 * ========================================================= */

#include <stddef.h>
#include "ble_service.h"
#include "ble_protocol.h"
#include "esp_log.h"

static const char *TAG = "ble_svc";

/* 上层注册的命令回调 */
static ble_cmd_cb_t s_cmd_cb = NULL;

/* =========================================================
 * 命令码 → 命令事件
 * 返回值：0 识别成功，-1 未知命令
 * ========================================================= */
static int cmd_code_to_evt(uint8_t code, ble_cmd_evt_t *out)
{
    switch (code) {
    case BLE_CMD_PING:       *out = BLE_CMD_EVT_PING;       return 0;
    case BLE_CMD_OUTPUT_ON:  *out = BLE_CMD_EVT_OUTPUT_ON;  return 0;
    case BLE_CMD_OUTPUT_OFF: *out = BLE_CMD_EVT_OUTPUT_OFF; return 0;
    case BLE_CMD_GET_STATE:  *out = BLE_CMD_EVT_GET_STATE;  return 0;
    default:                 *out = BLE_CMD_EVT_UNKNOWN;    return -1;
    }
}

/* =========================================================
 * 传输层入口：解析一条命令帧并分发
 * ========================================================= */
void ble_service_on_data(const uint8_t *data, int len)
{
    /* 帧头 + 命令码，至少 2 字节 */
    if (data == NULL || len < 2 || data[0] != PROTO_HEADER) {
        return;   /* 非本协议的数据，静默丢弃 */
    }

    ble_cmd_evt_t evt;
    if (cmd_code_to_evt(data[1], &evt) != 0) {
        ESP_LOGW(TAG, "未知命令 0x%02X，已忽略", data[1]);
        uint8_t err[2] = { PROTO_HEADER, BLE_RESP_ERR };
        ble_service_send(err, sizeof(err));
        return;
    }

    /* 心跳属于链路层保活，本层直接应答，同时仍上报给上层记录 */
    if (evt == BLE_CMD_EVT_PING) {
        uint8_t pong[2] = { PROTO_HEADER, BLE_RESP_PONG };
        ble_service_send(pong, sizeof(pong));
    }

    if (s_cmd_cb != NULL) {
        /* 无参数命令传 NULL，避免回调拿到越过帧尾的地址 */
        int plen = len - 2;
        s_cmd_cb(evt, (plen > 0) ? data + 2 : NULL, plen);
    }
}

/* =========================================================
 * 注册命令回调
 * ========================================================= */
void ble_service_register_cmd_cb(ble_cmd_cb_t cb)
{
    s_cmd_cb = cb;
}
