/* =========================================================
 * bsp_uart.c — UART 通信实现
 *
 * 功能：
 *   1. 初始化 UART1（引脚/波特率可配）
 *   2. 独立接收任务：收数据 → 组装帧 → 回调上层
 *   3. 提供发送接口
 *
 * 帧格式（可自定义）：
 *   [0xAA][len][data...][checksum]
 *   收到完整帧后通过回调交给上层。
 * ========================================================= */

#include <string.h>
#include "bsp_board.h"
#include "bsp_uart.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "bsp_uart";

/* ---- 内部状态 ---- */
static bsp_uart_rx_cb_t s_rx_cb = NULL;   /* 上层回调 */

/* ---- 帧解析器状态 ---- */
#define FRAME_HEAD 0xAA                    /* 帧头 */

static uint8_t  s_frame_buf[BSP_UART_MAX_FRAME];  /* 帧缓冲 */
static int      s_frame_len = 0;                    /* 已接收长度 */

/* ---- 接收任务：循环读 UART，组装帧 ---- */
static void uart_rx_task(void *arg)
{
    uint8_t buf[128];
    while (1) {
        /* 读取 UART 数据（阻塞 100ms 超时） */
        int len = uart_read_bytes(BSP_UART_NUM, buf, sizeof(buf),
                                  pdMS_TO_TICKS(100));
        for (int i = 0; i < len; i++) {
            uint8_t b = buf[i];

            /* 帧解析状态机：
             * 1. 空闲状态：等帧头 0xAA
             * 2. 收到帧头后，第 2 字节是长度
             * 3. 攒够数据后，校验并回调
             */
            if (s_frame_len == 0) {
                /* 找帧头 */
                if (b == FRAME_HEAD) {
                    s_frame_buf[s_frame_len++] = b;
                }
            } else if (s_frame_len == 1) {
                /* 第 2 字节：帧长度（不含帧头和长度本身） */
                s_frame_buf[s_frame_len++] = b;
                /* 长度合法性检查 */
                if (b == 0 || b > BSP_UART_MAX_FRAME - 2) {
                    s_frame_len = 0;   /* 非法长度，丢弃重来 */
                }
            } else {
                /* 数据部分 */
                s_frame_buf[s_frame_len++] = b;

                /* 帧长度 = 帧头(1) + 长度(1) + 数据(len) */
                int frame_total = 2 + s_frame_buf[1];
                if (s_frame_len >= frame_total) {
                    /* 完整帧到了，回调上层 */
                    if (s_rx_cb) {
                        s_rx_cb(s_frame_buf + 2, s_frame_buf[1]);
                    }
                    s_frame_len = 0;   /* 重置，准备下一帧 */
                }
            }
        }
    }
}

/* ================= 对外接口 ================= */

void bsp_uart_init(bsp_uart_rx_cb_t rx_cb)
{
    s_rx_cb = rx_cb;

    /* UART 配置 */
    uart_config_t uart_cfg = {
        .baud_rate = BSP_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    /* 安装驱动 */
    uart_driver_install(BSP_UART_NUM,
                        BSP_UART_RX_BUF,   /* 接收缓冲 */
                        BSP_UART_TX_BUF,   /* 发送缓冲 */
                        0, NULL, 0);

    /* 应用配置 */
    uart_param_config(BSP_UART_NUM, &uart_cfg);

    /* 设置引脚 */
    uart_set_pin(BSP_UART_NUM,
                 BSP_UART_TX_GPIO,
                 BSP_UART_RX_GPIO,
                 UART_PIN_NO_CHANGE,
                 UART_PIN_NO_CHANGE);

    /* 创建接收任务 */
    xTaskCreate(uart_rx_task, "uart_rx", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "UART 初始化: TX=%d, RX=%d, 波特率=%d",
             BSP_UART_TX_GPIO, BSP_UART_RX_GPIO, BSP_UART_BAUD);
}

int bsp_uart_send(const uint8_t *data, int len)
{
    return uart_write_bytes(BSP_UART_NUM, data, len);
}

void bsp_uart_send_str(const char *str)
{
    uart_write_bytes(BSP_UART_NUM, str, strlen(str));
}