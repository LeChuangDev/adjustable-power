/* =========================================================
 * app_queue.h — 任务间通信队列定义
 *
 * 统一管理系统内使用的 FreeRTOS 队列。
 * 以后加数据流（电压/电流）也在这里定义。
 * ========================================================= */

#ifndef APP_QUEUE_H
#define APP_QUEUE_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* 系统事件队列：状态机任务从这接收事件 */
extern QueueHandle_t g_app_event_queue;

/* 队列长度 */
#define APP_EVENT_QUEUE_LEN    8

#endif /* APP_QUEUE_H */