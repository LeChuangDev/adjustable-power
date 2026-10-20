/*
 * @Author: error: error: git config user.name & please set dead value or install git && error: git config user.email & please set dead value or install git & please set dead value or install git
 * @Date: 2026-08-30 10:25:03
 * @LastEditors: error: error: git config user.name & please set dead value or install git && error: git config user.email & please set dead value or install git & please set dead value or install git
 * @LastEditTime: 2026-08-30 17:04:21
 * @FilePath: \adjustable-power\main\app_task.c
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
/* =========================================================
 * app_task.c — 应用任务实现
 *
 * 创建并运行：
 *   1. 控制环任务（高优先级，1ms 周期）
 *   2. 显示任务（低优先级，100ms 周期）
 *
 * 数据采集任务待 ADC MCU 通信确定后接入。
 * ========================================================= */

#include "app_task.h"
#include "app_state.h"
#include "bsp_pwm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "app_task";

/* =========================================================
 * 控制环任务
 *
 * 周期：1ms（实时调节 PWM）
 * 当前：只维护状态，闭环算法待 ADC 接入后实现
 * ========================================================= */
static void control_task(void *arg)
{
    TickType_t last = xTaskGetTickCount();

    while (1) {
        /* 精确延时 1ms */
        vTaskDelayUntil(&last, pdMS_TO_TICKS(10));

        /* 根据状态执行控制逻辑 */
        switch (app_state_get()) {
        case APP_STATE_RUN:
            /* 闭环控制：读取反馈 → 计算误差 → 调 PWM（待 ADC 接入） */
            break;
        default:
            break;
        }
    }
}

/* =========================================================
 * 显示任务
 *
 * 周期：100ms
 * 当前：空实现（屏幕焊接后接入 LVGL）
 * ========================================================= */
#include "bsp_display.h"

static void display_task(void *arg)
{
    //bsp_display_init();
   // bsp_display_create_ui();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* =========================================================
 * app_task_init()
 *
 * 创建所有应用任务。
 * ========================================================= */
void app_task_init(void)
{
    /* 控制环：最高优先级，实时性要求高 */
    xTaskCreate(control_task, "control", 4096, NULL, 6, NULL);

    /* 显示：低优先级，刷新慢没关系 */
    xTaskCreate(display_task, "display", 4096, NULL, 1, NULL);

    ESP_LOGI(TAG, "应用任务创建完成");
}