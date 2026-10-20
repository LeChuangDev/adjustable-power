/*
 * @Author: error: error: git config user.name & please set dead value or install git && error: git config user.email & please set dead value or install git & please set dead value or install git
 * @Date: 2026-08-30 10:24:02
 * @LastEditors: error: error: git config user.name & please set dead value or install git && error: git config user.email & please set dead value or install git & please set dead value or install git
 * @LastEditTime: 2026-08-30 10:29:17
 * @FilePath: \adjustable-power\main\app_state.h
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
/* =========================================================
 * app_state.h — 可调电源系统状态机定义
 *
 * 定义系统运行状态和触发状态切换的事件。
 * 上层模块通过发送事件来驱动状态机。
 * ========================================================= */

#ifndef APP_STATE_H
#define APP_STATE_H

/* ---- 系统状态 ---- */
typedef enum {
    APP_STATE_BOOT,      /* 开机：自检、初始化 */
    APP_STATE_STANDBY,   /* 待机：无输出，等待用户操作 */
    APP_STATE_RUN,       /* 运行：正常输出，闭环调节 */
    APP_STATE_PROTECT,   /* 保护：过压/过流/过温，输出关闭 */
    APP_STATE_FAULT,     /* 故障：严重错误，需人工处理 */
} app_state_t;

/* ---- 系统事件（驱动状态切换）---- */
typedef enum {
    APP_EVT_BOOT_DONE = 0,   /* 初始化完成 */
    APP_EVT_ENABLE_OUTPUT,   /* 用户请求开输出（BLE/按键） */
    APP_EVT_DISABLE_OUTPUT,  /* 用户请求关输出 */
    APP_EVT_PROTECT_TRIG,    /* 保护触发（过压/过流/过温） */
    APP_EVT_PROTECT_CLEAR,   /* 保护解除 */
    APP_EVT_FAULT,           /* 发生严重故障 */
    APP_EVT_FAULT_CLEAR,     /* 故障清除 */
} app_event_t;

/* =========================================================
 * 对外接口
 * ========================================================= */

/* 初始化状态机（创建事件队列） */
void app_state_init(void);

/* 获取当前状态 */
app_state_t app_state_get(void);

/* 获取状态名字符串（用于日志/显示） */
const char *app_state_name(app_state_t state);

/* 发送事件（任意任务/回调都可调用，线程安全） */
void app_state_send_event(app_event_t evt);

#endif /* APP_STATE_H */