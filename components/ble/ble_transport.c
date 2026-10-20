/* =========================================================
 * ble_transport.c — BLE 传输层（NimBLE 实现）
 *
 * 职责：
 *   1. 初始化 NimBLE 协议栈
 *   2. 注册 GATT 服务（FFE0/FFE1/FFE2）
 *   3. 启动/维护 BLE 广播
 *   4. 收发原始数据，交给上层 ble_service 处理
 *
 * 设计原则：
 *   - 本层只负责「传」，不解析业务数据
 *   - 协议解析在 ble_protocol.c
 *   - 命令分发在 ble_service.c
 * ========================================================= */

#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "ble_config.h"
#include "ble_service.h"
#include "ble_transport.h"

static const char *TAG = "ble_trans";

/* ---- 运行状态 ---- */
static uint16_t s_tx_handle;        /* TX 特征句柄（ESP32 → 手机） */
static uint16_t s_conn_handle;      /* 当前连接句柄 */
static bool s_connected = false;    /* 是否已连接 */
static uint8_t s_addr_type;         /* 本机广播地址类型 */
static ble_transport_rx_cb_t s_rx_cb = NULL;  /* 原始数据接收回调 */

/* ---- GATT UUID 定义 ---- */
static const ble_uuid16_t s_svc_uuid = BLE_UUID16_INIT(BLE_SVC_UUID);
static const ble_uuid16_t s_tx_uuid  = BLE_UUID16_INIT(BLE_TX_UUID);
static const ble_uuid16_t s_rx_uuid  = BLE_UUID16_INIT(BLE_RX_UUID);

/* ---- 前向声明 ---- */
static int gap_event_cb(struct ble_gap_event *event, void *arg);
static void start_advertising(void);

/* =========================================================
 * write_cb()
 *
 * 手机通过 RX 特征（FFE2）写数据时触发。
 * 将收到的数据通过回调交给上层处理。
 * ========================================================= */
static int write_cb(uint16_t conn_handle, uint16_t attr_handle,
                    struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    int len = OS_MBUF_PKTLEN(ctxt->om);
    static uint8_t buf[512];

    if (len > (int)sizeof(buf)) {
        len = sizeof(buf);
    }

    ble_hs_mbuf_to_flat(ctxt->om, buf, len, NULL);

    if (s_rx_cb != NULL) {
        s_rx_cb(buf, len);
    }

    return 0;
}

/* =========================================================
 * ble_service_send()
 *
 * 上层调用：通过 TX 特征（FFE1）notify 数据到手机。
 * 未连接时自动丢弃。
 * ========================================================= */
void ble_service_send(const uint8_t *data, int len)
{
    if (!s_connected) {
        return;
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (om == NULL) {
        ESP_LOGE(TAG, "发送失败：内存不足");
        return;
    }

    int rc = ble_gatts_notify_custom(s_conn_handle, s_tx_handle, om);
    if (rc != 0) {
        ESP_LOGE(TAG, "notify 失败: %d", rc);
    }
}

/* =========================================================
 * ble_transport_register_rx_cb()
 *
 * 挂接原始数据接收回调（由 ble_service 初始化时挂上解析器）。
 * 上层应用不应直接调用，请用 ble_service_register_cmd_cb()。
 * ========================================================= */
void ble_transport_register_rx_cb(ble_transport_rx_cb_t cb)
{
    s_rx_cb = cb;
}

/* =========================================================
 * GATT 服务表
 *
 * 服务 UUID: FFE0
 *   特征 FFE1: Notify（ESP32 → 手机）
 *   特征 FFE2: Write（手机 → ESP32）
 * ========================================================= */
static const struct ble_gatt_svc_def s_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = &s_tx_uuid.u,
                .access_cb = write_cb,
                .flags = BLE_GATT_CHR_F_NOTIFY,
            },
            {
                .uuid = &s_rx_uuid.u,
                .access_cb = write_cb,
                .flags = BLE_GATT_CHR_F_WRITE,
            },
            { 0 },  /* 特征表结束 */
        },
    },
    { 0 },  /* 服务表结束 */
};

/* =========================================================
 * start_advertising()
 *
 * 设置广播数据并启动广播。
 * 使用 BLE_HS_FOREVER 持续广播，确保手机随时可发现。
 * ========================================================= */
static void start_advertising(void)
{
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));

    /* 广播标志：普通可发现 + 不支持 BR/EDR（纯 BLE） */
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    /* 自动填充发射功率，提高兼容性 */
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    /* 设备名：从 gap 服务获取，确保与 ble_svc_gap_device_name_set 一致 */
    const char *name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    /* 广播服务 UUID，方便手机过滤发现 */
    fields.uuids16 = (ble_uuid16_t[]){
        BLE_UUID16_INIT(BLE_SVC_UUID)
    };
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "广播数据设置失败: %d", rc);
        return;
    }

    /* 广播参数：可连接、可发现 */
    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(s_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, gap_event_cb, NULL);
    if (rc == 0) {
        ESP_LOGI(TAG, "开始广播: %s", name);
    } else {
        ESP_LOGE(TAG, "广播启动失败: %d", rc);
    }
}

/* =========================================================
 * gap_event_cb()
 *
 * GAP 事件处理：连接、断开、连接参数更新等。
 * 断开后自动恢复广播。
 * ========================================================= */
static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_connected = true;
            s_conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "手机已连接");
        } else {
            s_connected = false;
            ESP_LOGI(TAG, "连接失败/断开，重新广播");
            start_advertising();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        s_connected = false;
        ESP_LOGI(TAG, "连接断开，原因=%d，重新广播", event->disconnect.reason);
        start_advertising();
        break;

    case BLE_GAP_EVENT_CONN_UPDATE:
        /* 连接参数已更新，可在此打印 interval/latency/timeout */
        ESP_LOGI(TAG, "连接参数更新");
        break;

    default:
        break;
    }

    return 0;
}

/* =========================================================
 * on_sync()
 *
 * NimBLE 主机与控制器同步完成后调用。
 * 在此获取地址类型并启动广播。
 * ========================================================= */
static void on_sync(void)
{
    /* 自动推断最佳地址类型（Public 或 Random） */
    int rc = ble_hs_id_infer_auto(0, &s_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "地址类型推断失败: %d", rc);
        return;
    }

    ESP_LOGI(TAG, "NimBLE 已同步，启动广播");
    start_advertising();
}

/* =========================================================
 * host_task()
 *
 * NimBLE 主机任务，必须在 FreeRTOS 中运行。
 * ========================================================= */
static void host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/* =========================================================
 * ble_service_init()
 *
 * 对外接口：初始化并启动 BLE。
 * ========================================================= */
void ble_service_init(void)
{
    esp_err_t nvs_ret = nvs_flash_init();
    /* 固件更新后 NVS 版本不符 / 无空闲页时需擦除重建 */
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS 需重建，擦除后重试");
        nvs_flash_erase();
        nvs_ret = nvs_flash_init();
    }
    if (nvs_ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS 初始化失败: %s", esp_err_to_name(nvs_ret));
        return;
    }

    nimble_port_init();

    /* 设置设备名 */
    ble_svc_gap_device_name_set(BLE_DEVICE_NAME);

    /* 把传输层收到的原始字节交给业务层解析 */
    ble_transport_register_rx_cb(ble_service_on_data);

    /* 注册 GATT 服务 */
    int rc = ble_gatts_count_cfg(s_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "GATT 服务计数失败: %d", rc);
        return;
    }
    rc = ble_gatts_add_svcs(s_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "GATT 服务注册失败: %d", rc);
        return;
    }

    /* 注册同步回调：协议栈就绪后自动开始广播 */
    ble_hs_cfg.sync_cb = on_sync;

    /* 启动 NimBLE 主机任务 */
    nimble_port_freertos_init(host_task);

    ESP_LOGI(TAG, "BLE 初始化完成: %s", BLE_DEVICE_NAME);
}