#ifndef BSP_DISPLAY_H
#define BSP_DISPLAY_H

#include <stdint.h>

/* 初始化显示屏（QSPI + CO5300 + 触摸 + LVGL） */
void bsp_display_init(void);

/* 设置亮度 0~100 */
void bsp_display_set_brightness(uint8_t percent);

/* 获取屏幕宽/高 */
uint16_t bsp_display_width(void);
uint16_t bsp_display_height(void);

/* 在 LVGL 锁内创建 UI（显示任务里调用） */
void bsp_display_create_ui(void);

#endif