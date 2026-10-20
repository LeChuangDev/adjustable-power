#ifndef OTA_CONFIG_H
#define OTA_CONFIG_H

/* =========================================================
 * ota_config.h — OTA 配置中心
 *
 * 所有可调参数集中在此，改配置不用动逻辑。
 * ========================================================= */

/* ---- WiFi 热点 ---- */
#define OTA_AP_SSID          "PSULC"        /* 热点名 */
#define OTA_AP_PASS          "12345678"       /* 热点密码（产品化后应改） */
#define OTA_AP_MAX_CONN      2                /* 最大连接数 */

/* ---- TCP 升级服务 ---- */
#define OTA_TCP_PORT         8888             /* 监听端口 */

/* ---- 升级安全令牌（上位机必须携带，纯 ASCII） ---- */
#define OTA_AUTH_TOKEN       "PSU2025"

/* ---- 传输超时（毫秒） ---- */
#define OTA_TIMEOUT_MS       60000            /* 60 秒无数据视为中断 */

/* ---- 队列深度（数据块数，8 × 4KB = 32KB RAM，安全） ---- */
#define OTA_QUEUE_LEN        16

#endif