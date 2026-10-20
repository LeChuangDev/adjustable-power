/* =========================================================
 * bsp_uart.h — UART 通信接口
 *
 * 提供：初始化、发送、接收（带帧解析）
 * 上层通过回调函数处理收到的完整帧。
 * ========================================================= */

#ifndef BSP_UART_H
#define BSP_UART_H

#include <stdint.h>
#include <stddef.h>

/* 帧最大长度 */
#define BSP_UART_MAX_FRAME   256

/* 收到完整帧的回调类型 */
typedef void (*bsp_uart_rx_cb_t)(const uint8_t *data, int len);

/**
 * @brief 初始化 UART
 * @param rx_cb 收到完整帧后的回调（可为 NULL）
 */
void bsp_uart_init(bsp_uart_rx_cb_t rx_cb);

/**
 * @brief 发送数据
 * @param data 数据指针
 * @param len  数据长度
 * @return 实际发送字节数
 */
int bsp_uart_send(const uint8_t *data, int len);

/**
 * @brief 发送字符串
 * @param str 以 \0 结尾的字符串
 */
void bsp_uart_send_str(const char *str);

#endif /* BSP_UART_H */