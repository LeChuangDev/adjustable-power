/*
 * @Author: error: error: git config user.name & please set dead value or install git && error: git config user.email & please set dead value or install git & please set dead value or install git
 * @Date: 2026-08-29 17:41:10
 * @LastEditors: error: error: git config user.name & please set dead value or install git && error: git config user.email & please set dead value or install git & please set dead value or install git
 * @LastEditTime: 2026-10-01 15:53:17
 * @FilePath: \adjustable-power\main\main.c
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
/* =========================================================
 * main.c — 可调电源主程序入口
 *
 * 启动顺序：
 *   1. 初始化底层模块（PWM/UART/BLE）
 *   2. 初始化状态机
 *   3. 创建应用任务
 * ========================================================= */

#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp_pd.h"
#include "bsp_pwm.h"
#include "bsp_uart.h"
#include "ble_service.h"
#include "ble_protocol.h"
#include "ota_manager.h"
#include "app_state.h"
#include "app_task.h"
#include "ota_flash.h"

static const char *TAG = "psu";


/* =========================================================
 * 启动协商任务：等充电器握手稳定后，自动协商最高电压
 * ========================================================= */
static void pd_boot_negotiate_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(2000));   /* 等 CH224Q 完成协议握手 */

    if (!bsp_pd_is_present()) {
        ESP_LOGW(TAG, "CH224Q 不在线（检查充电器）");
        vTaskDelete(NULL);
        return;
    }

    bsp_pd_state_t st;
    bsp_pd_get_state(&st);

    uint16_t got = bsp_pd_negotiate_max();

    ESP_LOGW(TAG, "========== 协商结果 ==========");
    ESP_LOGW(TAG, "协议     : %s", bsp_pd_protocol_name(&st));
    ESP_LOGW(TAG, "预期电压 : %u mV", got);
    ESP_LOGW(TAG, "档位电流 : %u mA（QC 模式无效）",
             bsp_pd_get_current_limit_ma());
    ESP_LOGW(TAG, "⚠️ 请用万用表实测 VBUS 确认");
    ESP_LOGW(TAG, "==============================");

    vTaskDelete(NULL);
}

/* =========================================================
 * PD 监控任务：每 5 秒打印一次协商状态
 * ========================================================= */
static void pd_monitor_task(void *arg)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));

        if (!bsp_pd_is_present()) {
            ESP_LOGW(TAG, "[PD] 不在线");
            continue;
        }

        bsp_pd_state_t st;
        if (bsp_pd_get_state(&st) != ESP_OK) {
            ESP_LOGW(TAG, "[PD] 读状态失败");
            continue;
        }

        ESP_LOGI(TAG, "[PD] %s | 请求=%umV 档位电流=%umA 协商=%lumW",
                 bsp_pd_protocol_name(&st),
                 bsp_pd_get_requested_mv(),
                 bsp_pd_get_current_limit_ma(),
                 (unsigned long)(bsp_pd_get_negotiated_power_mw() / 1000));
    }
}


/* BLE 命令回调：收到的是已解析的命令事件，不再解析裸字节
 * （命令码定义见 ble_protocol.h，解析在 ble_service.c） */
static void on_ble_cmd(ble_cmd_evt_t evt, const uint8_t *payload, int len)
{
    (void)payload;
    (void)len;

    switch (evt) {
    case BLE_CMD_EVT_OUTPUT_ON:
        ESP_LOGW(TAG, "BLE: 开输出");
        app_state_send_event(APP_EVT_ENABLE_OUTPUT);
        break;

    case BLE_CMD_EVT_OUTPUT_OFF:
        ESP_LOGW(TAG, "BLE: 关输出");
        app_state_send_event(APP_EVT_DISABLE_OUTPUT);
        break;

    case BLE_CMD_EVT_GET_STATE: {
        uint8_t resp[3] = {
            PROTO_HEADER,
            BLE_RESP_STATE,
            (uint8_t)app_state_get(),
        };
        ble_service_send(resp, sizeof(resp));
        break;
    }

    case BLE_CMD_EVT_PING:
        ESP_LOGD(TAG, "BLE: PING");
        break;

    default:
        break;
    }
}



/* 启动 5 秒后标记固件有效：
 * 如果新固件在 5 秒内崩溃，不会被标记，
 * bootloader 会自动回滚到上一版本。 */
static void mark_valid_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(5000));
    ota_flash_mark_valid();
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "可调电源启动");

    /* 1. 底层模块初始化 */
    bsp_pwm_init();
    bsp_uart_init(NULL);
    bsp_pd_init();    
    /* BLE 暂未启用（与 OTA 的 SoftAP 共用射频，确认后再打开）：
     * 启用时这两行一起解开即可，回调已改为命令级接口。 */
    //ble_service_init();
    //ble_service_register_cmd_cb(on_ble_cmd);
    ota_manager_start();
       /* PD 协商 + 监控 */
    xTaskCreate(pd_boot_negotiate_task, "pd_neg", 4096, NULL, 3, NULL);
    xTaskCreate(pd_monitor_task,        "pd_mon", 4096, NULL, 3, NULL);
     if (bsp_pd_is_present()) {
        bsp_pd_state_t st;
        if (bsp_pd_get_state(&st) == ESP_OK) {
            ESP_LOGI(TAG, "PD状态: EPR=%d PD=%d QC3=%d QC2=%d BC=%d",
                     st.epr_active, st.pd_active,
                     st.qc3_active, st.qc2_active, st.bc_active);
        }
        ESP_LOGI(TAG, "可用电流: %d mA", bsp_pd_get_current_limit_ma());
    }

    /* 2. 状态机初始化 */
    app_state_init();

    /* 3. 创建应用任务 */
    app_task_init();

    /* 4. 模拟启动完成，进入待机 */
    vTaskDelay(pdMS_TO_TICKS(500));
    app_state_send_event(APP_EVT_BOOT_DONE);

    ESP_LOGI(TAG, "系统就绪，当前状态: %s", app_state_name(app_state_get()));
    /* 新固件健康检查：5 秒后标记有效（失败则自动回滚） */
    xTaskCreate(mark_valid_task, "mark_valid", 2048, NULL, 2, NULL);
    /* 主循环：交给任务调度 */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
        }