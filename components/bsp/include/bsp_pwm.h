/* =========================================================
 * bsp_pwm.h — PWM 对外接口
 *
 * 上层（main、控制环、UI）只需调用这些函数，
 * 不需要了解 LEDC 底层细节。
 * ========================================================= */

#ifndef BSP_PWM_H
#define BSP_PWM_H

#include <stdbool.h>

/* 初始化 PWM（频率、分辨率、引脚） */
void bsp_pwm_init(void);

/* 直接设置占空比 0.0 ~ 100.0（浮点，高精度） */
void bsp_pwm_set_duty(float percent);

/* 平滑渐变到目标占空比（软启动，防冲击） */
void bsp_pwm_ramp_to(float target_percent);

/* 使能/关闭输出 */
void bsp_pwm_enable(bool enable);

/* 查询当前占空比 */
float bsp_pwm_get_duty(void);

#endif /* BSP_PWM_H */