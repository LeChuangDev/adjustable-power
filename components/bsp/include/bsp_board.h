/*
 * @Author: error: error: git config user.name & please set dead value or install git && error: git config user.email & please set dead value or install git & please set dead value or install git
 * @Date: 2026-08-29 17:41:04
 * @LastEditors: error: error: git config user.name & please set dead value or install git && error: git config user.email & please set dead value or install git & please set dead value or install git
 * @LastEditTime: 2026-09-16 21:19:34
 * @FilePath: \adjustable-power\components\bsp\include\bsp_board.h
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
/* =========================================================
 * bsp_board.h — 板级引脚与参数定义
 *
 * ⭐ 设计原则：所有硬件相关参数集中在此，
 *    PCB 设计完成后只需修改本文件，代码逻辑不用动。
 * ========================================================= */

#ifndef BSP_BOARD_H
#define BSP_BOARD_H

/* ============ PWM 输出（开关电源功率级）============ */
#define BSP_PWM_GPIO        8       /* PWM 输出引脚 */
#define BSP_PWM_CHANNEL     0       /* LEDC 通道号 */
#define BSP_PWM_TIMER       0       /* LEDC 定时器号 */

/* PWM 频率与分辨率：两者必须匹配！
 * 硬件限制：freq × 2^res_bits ≤ 80MHz（LEDC 时钟）
 * 例：20kHz × 2^12 = 81.9MHz ❌ (超)
 *     20kHz × 2^11 = 40.9MHz ✅
 *     100kHz× 2^9  = 51.2MHz ✅
 *     200kHz× 2^8  = 51.2MHz ✅
 */
#define BSP_PWM_FREQ_HZ     20000   /* 开关频率（待硬件定后调整） */
#define BSP_PWM_RES_BITS    11      /* 分辨率位数（对应 2^11=2048 级） */

/* 平滑渐变参数 */
#define BSP_PWM_RAMP_STEP   1.0f    /* 渐变步进（%每5ms） */
#define BSP_PWM_RAMP_MS     5       /* 渐变间隔（ms） */

/* ============ UART（与 ADC MCU / 调试 通信）============ */
#define BSP_UART_NUM       UART_NUM_1   /* 用 UART1（UART0 通常留给日志/烧录） */
#define BSP_UART_TX_GPIO   17           /* TX 引脚（待 PCB 定） */
#define BSP_UART_RX_GPIO   18           /* RX 引脚（待 PCB 定） */
#define BSP_UART_BAUD      115200       /* 波特率 */
#define BSP_UART_TX_BUF    2048         /* 发送缓冲 */
#define BSP_UART_RX_BUF    2048         /* 接收缓冲 */

/* ============ 显示屏（CO5300 AMOLED 460x460 QSPI）============ */
/* 引脚默认值来自屏幕卖家例程，焊接后按实际修改 */

#define BSP_DISP_WIDTH          460
#define BSP_DISP_HEIGHT         460
#define BSP_DISP_X_GAP          10
#define BSP_DISP_Y_GAP          0
#define BSP_DISP_PIXEL_CLK_HZ   (40 * 1000 * 1000)   /* 40MHz */

/* QSPI 引脚 */
#define BSP_DISP_CS_GPIO        9
#define BSP_DISP_SCK_GPIO       12
#define BSP_DISP_D0_GPIO        13
#define BSP_DISP_D1_GPIO        14
#define BSP_DISP_D2_GPIO        15
#define BSP_DISP_D3_GPIO        16
#define BSP_DISP_RST_GPIO       10
#define BSP_DISP_TE_GPIO        11      /* 未使用 */

/* 触摸（CST820 I2C） */
#define BSP_TOUCH_I2C_SCL       40
#define BSP_TOUCH_I2C_SDA       39
#define BSP_TOUCH_RST_GPIO      41
#define BSP_TOUCH_INT_GPIO      42
#define BSP_TOUCH_I2C_CLK_HZ    (400 * 1000)

/* ============ CH224Q PD 诱骗芯片 ============ */
/* 使用 I2C_NUM_1，避开触摸用的 I2C_NUM_0（GPIO39/40）*/

#define BSP_PD_I2C_PORT        I2C_NUM_1
#define BSP_PD_I2C_SCL_GPIO    4        /* SCL → CFG2 */
#define BSP_PD_I2C_SDA_GPIO    5        /* SDA → CFG3 */
#define BSP_PD_I2C_FREQ_HZ     400000   /* 400kHz（手册上限）*/
#define BSP_PD_I2C_ADDR        0x22     /* 7位地址（0x23 也响应）*/

/* 母线目标：输出 + 裕量给 MP4247 */
#define BSP_PD_BUS_MARGIN_MV   2000     /* 预留裕量 mV */
#define BSP_PD_BUS_MAX_MV      28000    /* AVS 请求上限（EPR 28V）*/
#define BSP_PD_BUS_MIN_MV      5000     /* AVS 请求下限 */

#endif /* BSP_BOARD_H */