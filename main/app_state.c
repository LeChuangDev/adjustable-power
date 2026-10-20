/* =========================================================
 * app_state.c — 系统状态机实现
 *
 * 职责：
 *   1. 维护当前系统状态
 *   2. 接收事件，按状态转移表切换状态
 *   3. 状态切换时执行对应动作（如关输出）
 * ========================================================= */

#include <string.h>
#include "app_state.h"
#include "app_queue.h"
#include "bsp_pwm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

static const char *TAG = "app_state";

/* 事件队列句柄 */
QueueHandle_t g_app_event_queue = NULL;

/* 当前状态 */
static app_state_t s_state = APP_STATE_BOOT;

/* 状态名字表（与枚举一一对应） */
static const char *s_state_names[] = {
    [APP_STATE_BOOT]    = "BOOT",
    [APP_STATE_STANDBY] = "STANDBY",
    [APP_STATE_RUN]     = "RUN",
    [APP_STATE_PROTECT] = "PROTECT",
    [APP_STATE_FAULT]   = "FAULT",
};

/* =========================================================
 * 状态转移处理
 *
 * 返回新状态。每个状态只接受合法事件。
 * ========================================================= */
static app_state_t state_transition(app_state_t cur, app_event_t evt)
{
    switch (cur) {
    case APP_STATE_BOOT:
        if (evt == APP_EVT_BOOT_DONE) {
            return APP_STATE_STANDBY;   /* 初始化完成 → 待机 */
        }
        break;

    case APP_STATE_STANDBY:
        if (evt == APP_EVT_ENABLE_OUTPUT) {
            return APP_STATE_RUN;       /* 开输出 → 运行 */
        }
        break;

    case APP_STATE_RUN:
        if (evt == APP_EVT_DISABLE_OUTPUT) {
            return APP_STATE_STANDBY;   /* 关输出 → 待机 */
        }
        if (evt == APP_EVT_PROTECT_TRIG) {
            return APP_STATE_PROTECT;   /* 异常 → 保护 */
        }
        if (evt == APP_EVT_FAULT) {
            return APP_STATE_FAULT;     /* 严重错误 → 故障 */
        }
        break;

    case APP_STATE_PROTECT:
        if (evt == APP_EVT_PROTECT_CLEAR) {
            return APP_STATE_STANDBY;   /* 保护解除 → 待机 */
        }
        break;

    case APP_STATE_FAULT:
        if (evt == APP_EVT_FAULT_CLEAR) {
            return APP_STATE_STANDBY;   /* 故障清除 → 待机 */
        }
        break;
    }
    return cur;   /* 其它事件：保持当前状态 */
}

/* =========================================================
 * 状态切换时的动作（进入某状态时执行）
 * ========================================================= */
static void on_state_enter(app_state_t state)
{
    switch (state) {
    case APP_STATE_STANDBY:
        /* 待机：确保输出关闭 */
         bsp_pwm_enable(false);  /* 真正关闭 PWM 输出 */
        ESP_LOGI(TAG, "进入待机，输出关闭");
        /* 这里可调用 bsp_pwm_enable(false) */
        break;

    case APP_STATE_RUN:
        bsp_pwm_enable(true);   /* 真正打开 PWM 输出 */
        ESP_LOGI(TAG, "进入运行，输出使能");
        /* 这里可调用 bsp_pwm_enable(true) */
        break;

    case APP_STATE_PROTECT:
        ESP_LOGI(TAG, "保护触发，输出关闭");
        /* 必须关闭输出！ */
        break;

    case APP_STATE_FAULT:
        ESP_LOGI(TAG, "严重故障，输出关闭");
        /* 关闭输出，记录错误 */
        break;

    default:
        break;
    }
}

/* =========================================================
 * 状态机任务：等待事件 → 转移状态 → 执行动作
 * ========================================================= */
static void state_machine_task(void *arg)
{
    app_event_t evt;

    ESP_LOGI(TAG, "状态机启动，当前状态: %s", app_state_name(s_state));

    while (1) {
        /* 阻塞等待事件 */
        if (xQueueReceive(g_app_event_queue, &evt, portMAX_DELAY) == pdTRUE) {
            app_state_t new_state = state_transition(s_state, evt);

            if (new_state != s_state) {
                ESP_LOGI(TAG, "状态切换: %s → %s",
                         app_state_name(s_state), app_state_name(new_state));
                s_state = new_state;
                on_state_enter(new_state);
            }
        }
    }
}

/* =========================================================
 * 对外接口
 * ========================================================= */

void app_state_init(void)
{
    if (g_app_event_queue == NULL) {
        g_app_event_queue = xQueueCreate(APP_EVENT_QUEUE_LEN, sizeof(app_event_t));
    }

    /* 创建状态机任务 */
    xTaskCreate(state_machine_task, "state_machine", 4096, NULL, 4, NULL);
}

app_state_t app_state_get(void)
{
    return s_state;
}

const char *app_state_name(app_state_t state)
{
    if (state < APP_STATE_BOOT || state > APP_STATE_FAULT) {
        return "UNKNOWN";
    }
    return s_state_names[state];
}

void app_state_send_event(app_event_t evt)
{
    if (g_app_event_queue != NULL) {
        xQueueSend(g_app_event_queue, &evt, 0);   /* 非阻塞发送 */
    }
}