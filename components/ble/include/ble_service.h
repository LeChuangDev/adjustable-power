/* =========================================================
 * ble_service.h — BLE 业务层对外接口
 *
 * 数据流：
 *   ble_transport（原始字节）
 *     → ble_service_on_data（解析命令帧）
 *       → 上层注册的 ble_cmd_cb_t（业务处理）
 *
 * 上层（main）只注册一个命令回调，**不要自己解析字节流**，
 * 否则同一条命令会出现两套互相冲突的含义。
 * 命令码统一定义在 ble_protocol.h。
 * ========================================================= */

#ifndef BLE_SERVICE_H
#define BLE_SERVICE_H

#include <stdint.h>

/* 解析后的命令事件 */
typedef enum {
    BLE_CMD_EVT_PING = 0,      /* 心跳（已由本层自动回 PONG） */
    BLE_CMD_EVT_OUTPUT_ON,     /* 开输出 */
    BLE_CMD_EVT_OUTPUT_OFF,    /* 关输出 */
    BLE_CMD_EVT_GET_STATE,     /* 查询当前状态 */
    BLE_CMD_EVT_UNKNOWN,       /* 无法识别的命令 */
} ble_cmd_evt_t;

/* 命令回调：payload 为命令参数（无参数时为 NULL），len 为其长度 */
typedef void (*ble_cmd_cb_t)(ble_cmd_evt_t evt, const uint8_t *payload, int len);

/* 初始化 BLE 并开始广播 */
void ble_service_init(void);

/* 发送数据到手机（TX 特征） */
void ble_service_send(const uint8_t *data, int len);

/* 注册命令回调（在 app_main 中调用一次） */
void ble_service_register_cmd_cb(ble_cmd_cb_t cb);

/* 传输层收到原始数据后调用，交给本层解析分发（内部使用） */
void ble_service_on_data(const uint8_t *data, int len);

#endif /* BLE_SERVICE_H */