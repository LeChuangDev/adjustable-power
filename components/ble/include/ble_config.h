/*
 * @Author: error: error: git config user.name & please set dead value or install git && error: git config user.email & please set dead value or install git & please set dead value or install git
 * @Date: 2026-08-29 20:35:49
 * @LastEditors: error: error: git config user.name & please set dead value or install git && error: git config user.email & please set dead value or install git & please set dead value or install git
 * @LastEditTime: 2026-08-29 22:42:26
 * @FilePath: \adjustable-power\components\ble\include\ble_config.h
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
/* =========================================================
 * ble_config.h — BLE 配置
 *
 * ⭐ 所有 BLE 可配置项集中在此，方便修改。
 *    改蓝牙名称只需改 BLE_DEVICE_NAME 一处。
 * ========================================================= */

#ifndef BLE_CONFIG_H
#define BLE_CONFIG_H

/* ---- 设备名称（改这里！全局生效）---- */
#define BLE_DEVICE_NAME     "PSULC"      /* 可调电源-001号 */

/* ---- 服务/特征 UUID ---- */
#define BLE_SVC_UUID        0xFFE0           /* 服务 UUID */
#define BLE_TX_UUID         0xFFE1           /* TX 特征（ESP32→手机） */
#define BLE_RX_UUID         0xFFE2           /* RX 特征（手机→ESP32） */

/* ---- 广播参数 ---- */
#define BLE_ADV_INTERVAL_MS 100              /* 广播间隔 ms */

#endif /* BLE_CONFIG_H */