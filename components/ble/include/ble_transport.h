/* =========================================================
 * ble_transport.h — BLE 传输层内部接口
 *
 * 传输层只负责「收发原始字节」，不解析业务数据。
 * 本头文件供 ble_service.c 挂接解析器使用，
 * 上层应用请使用 ble_service.h，不要直接调用这里的接口。
 * ========================================================= */

#ifndef BLE_TRANSPORT_H
#define BLE_TRANSPORT_H

#include <stdint.h>

/* 收到手机原始数据的回调类型 */
typedef void (*ble_transport_rx_cb_t)(const uint8_t *data, int len);

/* 注册原始数据回调（由 ble_service 初始化时挂上解析器） */
void ble_transport_register_rx_cb(ble_transport_rx_cb_t cb);

#endif /* BLE_TRANSPORT_H */
