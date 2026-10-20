/* =========================================================
 * bsp_pwm.c — PWM 模块实现（高效、可扩展）
 *
 * 特点：
 *   1. 浮点占空比，换算时四舍五入，精度更高
 *   2. 渐变用独立 FreeRTOS 任务，不阻塞控制环
 *   3. 带防重复初始化、错误检查
 *   4. 参数全部来自 bsp_board.h，便于调整
 * ========================================================= */

#include <math.h>
#include <string.h>
#include "bsp_board.h"
#include "bsp_pwm.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "bsp_pwm";

/* ---- 内部状态 ---- */
static bool   s_inited = false;     /* 初始化标志 */
static bool   s_enabled = false;    /* 输出使能状态 */
static float  s_cur_duty = 0.0f;    /* 当前占空比 0~100 */
static uint32_t s_max_duty;         /* 最大占空比级数 (2^bits - 1) */

/* 渐变任务用的队列（从任务发渐变请求） */
static QueueHandle_t s_ramp_q = NULL;

/* 渐变请求结构 */
typedef struct {
    float target;
} ramp_req_t;

/* ---- 内部函数：写占空比到 LEDC ---- */
static void pwm_write(float percent)
{
    if (!s_inited) return;

    /* 限幅 */
    if (percent < 0.0f) percent = 0.0f;
    if (percent > 100.0f) percent = 100.0f;

    /* 浮点换算 + 四舍五入（提高精度） */
    uint32_t duty = (uint32_t)roundf(percent / 100.0f * s_max_duty);

    if (s_enabled) {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, BSP_PWM_CHANNEL, duty);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, BSP_PWM_CHANNEL);
    }
    s_cur_duty = percent;
}

/* ---- 渐变任务：平滑过渡，不阻塞调用方 ---- */
static void ramp_task(void *arg)
{
    ramp_req_t req;
    while (1) {
        /* 等待渐变请求 */
        if (xQueueReceive(s_ramp_q, &req, portMAX_DELAY) == pdTRUE) {
            float target = req.target;
            /* 逐步逼近目标 */
            while (fabsf(s_cur_duty - target) > BSP_PWM_RAMP_STEP) {
                if (s_cur_duty < target) {
                    pwm_write(s_cur_duty + BSP_PWM_RAMP_STEP);
                } else {
                    pwm_write(s_cur_duty - BSP_PWM_RAMP_STEP);
                }
                vTaskDelay(pdMS_TO_TICKS(BSP_PWM_RAMP_MS));
            }
            /* 最终精确到达 */
            pwm_write(target);
        }
    }
}

/* ================= 对外接口 ================= */

void bsp_pwm_init(void)
{
    if (s_inited) return;

    /* 计算最大占空比级数 */
    s_max_duty = (1 << BSP_PWM_RES_BITS) - 1;   /* 2^11 - 1 = 2047 */

    /* ---- 定时器配置 ---- */
    ledc_timer_config_t timer = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = (ledc_timer_bit_t)(BSP_PWM_RES_BITS),  /* 动态分辨率 */
        .timer_num       = BSP_PWM_TIMER,
        .freq_hz         = BSP_PWM_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    esp_err_t e = ledc_timer_config(&timer);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "定时器配置失败: %s", esp_err_to_name(e));
        return;
    }

    /* ---- 通道配置 ---- */
    ledc_channel_config_t ch = {
        .gpio_num   = BSP_PWM_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = BSP_PWM_CHANNEL,
        .timer_sel  = BSP_PWM_TIMER,
        .duty       = 0,
    };
    e = ledc_channel_config(&ch);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "通道配置失败: %s", esp_err_to_name(e));
        return;
    }

    /* ---- 创建渐变任务和队列 ---- */
    s_ramp_q = xQueueCreate(4, sizeof(ramp_req_t));
    xTaskCreate(ramp_task, "pwm_ramp", 2048, NULL, 2, NULL);

    s_inited = true;
    ESP_LOGI(TAG, "PWM 初始化: 引脚=%d, 频率=%dHz, %d位(%u级)",
         BSP_PWM_GPIO, BSP_PWM_FREQ_HZ, BSP_PWM_RES_BITS,
         (unsigned)(s_max_duty + 1));
}

void bsp_pwm_set_duty(float percent)
{
    pwm_write(percent);
}

void bsp_pwm_ramp_to(float target_percent)
{
    if (!s_inited || s_ramp_q == NULL) return;
    ramp_req_t req = { .target = target_percent };
    xQueueSend(s_ramp_q, &req, 0);   /* 非阻塞发送 */
}

void bsp_pwm_enable(bool enable)
{
    if (!s_inited) return;
    s_enabled = enable;
    if (enable) {
        /* 恢复当前占空比 */
        pwm_write(s_cur_duty);
    } else {
        /* 输出 0 */
        ledc_set_duty(LEDC_LOW_SPEED_MODE, BSP_PWM_CHANNEL, 0);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, BSP_PWM_CHANNEL);
    }
}

float bsp_pwm_get_duty(void)
{
    return s_cur_duty;
}