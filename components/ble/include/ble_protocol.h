/* =========================================================
 * ble_protocol.h — BLE 通信协议定义
 *
 * 设计目标：可扩展、可靠、支持版本演进
 * ========================================================= */

#ifndef BLE_PROTOCOL_H
#define BLE_PROTOCOL_H

#include <stdint.h>

/* ---- 协议常量 ---- */
#define PROTO_HEADER        0xAA        /* 帧头 */
#define PROTO_TAIL          0x55        /* 帧尾 */
#define PROTO_VERSION       0x01        /* 协议版本 */
#define PROTO_MAX_PAYLOAD   512         /* 负载最大长度 */

/* ---- 帧类型 ---- */
typedef enum {
    FRAME_REQUEST  = 0x01,   /* 请求（手机→设备）*/
    FRAME_RESPONSE = 0x02,   /* 响应（设备→手机）*/
    FRAME_NOTIFY   = 0x03,   /* 通知（设备主动上报）*/
    FRAME_ACK      = 0x04,   /* 确认 */
    FRAME_ERROR    = 0x05,   /* 错误 */
} proto_frame_type_t;

/* ---- 模块号 ---- */
#define MOD_SYSTEM      0xFF   /* 系统 */
#define MOD_POWER       0x01   /* 电源控制 */
#define MOD_STATUS      0x02   /* 状态查询 */
#define MOD_OTA         0x03   /* OTA 升级 */

/* ---- 系统命令 ---- */
#define SYS_PING        0x01   /* 心跳/连通测试 */
#define SYS_GET_INFO    0x02   /* 获取设备信息 */

/* ---- 响应状态码 ---- */
#define RESP_OK         0x00   /* 成功 */
#define RESP_ERR        0x01   /* 一般错误 */
#define RESP_BAD_CMD    0x02   /* 非法命令 */
#define RESP_BAD_PARAM  0x03   /* 参数错误 */

/* =========================================================
 * 轻量命令帧（手机 → 设备）
 *
 *   [PROTO_HEADER][cmd][payload...]
 *
 * 2 字节起步，窄到能塞进 BLE 默认 MTU；下面的 proto_frame_t
 * 是带 CRC 的重量级帧，两者格式不同，只是复用 0xAA 帧头。
 *
 * ⚠️ 命令码只在本文件定义一次，任何地方不要再另立一套。
 * ========================================================= */
#define BLE_CMD_PING        0x00   /* 心跳：回 PONG，不改变输出状态 */
#define BLE_CMD_OUTPUT_ON   0x01   /* 开输出 */
#define BLE_CMD_OUTPUT_OFF  0x02   /* 关输出 */
#define BLE_CMD_GET_STATE   0x03   /* 查询当前状态 */

/* ---- 设备 → 手机的应答码 ---- */
#define BLE_RESP_PONG       0x80   /* 对 PING 的应答，无参数 */
#define BLE_RESP_STATE      0x83   /* 对 GET_STATE 的应答，第 3 字节为状态枚举值 */
#define BLE_RESP_ERR        0x7F   /* 命令无法识别 */

/* ---- 帧长计算（长度字段为 payload 长度）---- */
#define PROTO_HDR_SIZE      5      /* header + version + type + len(2) */
#define PROTO_TAIL_SIZE     3      /* crc16(2) + tail(1) */
#define PROTO_FRAME_TOTAL(n)  (PROTO_HDR_SIZE + (n) + PROTO_TAIL_SIZE)
#define PROTO_CRC_COVER(n)    (PROTO_HDR_SIZE + (n))   /* CRC 覆盖 header..payload */

/* ---- 完整帧结构 ---- */
#pragma pack(push, 1)
typedef struct {
    uint8_t  header;     /* 0xAA */
    uint8_t  version;    /* 0x01 */
    uint8_t  type;       /* 帧类型 */
    uint16_t len;        /* 负载长度 */
    uint8_t  payload[PROTO_MAX_PAYLOAD];
    uint16_t crc16;      /* CRC 校验 */
    uint8_t  tail;       /* 0x55 */
} proto_frame_t;
#pragma pack(pop)

#endif /* BLE_PROTOCOL_H */