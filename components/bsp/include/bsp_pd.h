/* =========================================================
 * bsp_pd.h — CH224Q PD 诱骗芯片接口
 *
 * 职责：
 *   1. I2C 通信（读写 CH224Q 寄存器）
 *   2. 读协议握手状态（PD/EPR/QC）
 *   3. 请求固定档位 / AVS 动态调压
 *   4. 读充电器能力表（PDO 解析）
 *   5. 通用协商（按充电器实际能力选最高档）
 * ========================================================= */

#ifndef BSP_PD_H
#define BSP_PD_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/* ---- 固定电压档位（对应寄存器 0x0A 的值）---- */
typedef enum {
    PD_VOLT_5V  = 0,
    PD_VOLT_9V  = 1,
    PD_VOLT_12V = 2,
    PD_VOLT_15V = 3,
    PD_VOLT_20V = 4,
    PD_VOLT_28V = 5,    /* 仅 EPR 支持 */
    PD_VOLT_PPS = 6,    /* PPS 模式 */
    PD_VOLT_AVS = 7,    /* AVS 模式（动态调压）*/
} bsp_pd_level_t;

/* ---- 协议握手状态（寄存器 0x09）---- */
typedef struct {
    bool epr_active;    /* PD3.1 EPR（28V）握手成功 */
    bool pd_active;     /* PD 握手成功 */
    bool qc3_active;    /* QC3.0 握手成功 */
    bool qc2_active;    /* QC2.0 握手成功 */
    bool bc_active;     /* BC1.2 检测到 */
} bsp_pd_state_t;

/* ---- PDO 档位类型 ---- */
typedef enum {
    PDO_TYPE_FIXED = 0,     /* 固定档位 */
    PDO_TYPE_PPS   = 1,     /* PPS / AVS 可调档 */
    PDO_TYPE_UNKNOWN = 2,
} pdo_type_t;

/* ---- 单个充电器档位 ---- */
typedef struct {
    pdo_type_t type;
    uint16_t   volt_max_mv;     /* 最大电压 mV */
    uint16_t   volt_min_mv;     /* 最小电压 mV（固定档 = vmax）*/
    uint16_t   curr_max_ma;     /* 最大电流 mA */
    uint32_t   power_mw;        /* 该档最大功率 mW */
} pd_pdo_t;

/* ---- 充电器能力表（解析后）---- */
#define PD_MAX_OPTIONS  7

typedef struct {
    pd_pdo_t pdo[PD_MAX_OPTIONS];
    int      count;             /* 有效 PDO 数量 */
    int      best_index;        /* 功率最大的档位索引，-1 = 无 */
} pd_capability_t;

/* =========================================================
 * 基础接口
 * ========================================================= */

/* 初始化（I2C + 探测芯片） */
esp_err_t bsp_pd_init(void);

/* 芯片是否在线 */
bool bsp_pd_is_present(void);

/* 读协议握手状态 */
esp_err_t bsp_pd_get_state(bsp_pd_state_t *out);

/* 读当前档位最大可用电流（mA），无握手/非 PD 返回 0 */
uint16_t bsp_pd_get_current_limit_ma(void);

/* 请求固定档位电压 */
esp_err_t bsp_pd_request_level(bsp_pd_level_t level);

/* AVS 动态调压（毫伏，100mV 步进），自动切到 AVS 模式 */
esp_err_t bsp_pd_set_avs_voltage(uint16_t mv);

/* 根据目标输出电压设置母线（= 目标 + 裕量，裁剪到 5~28V） */
esp_err_t bsp_pd_set_bus_for_output(uint16_t output_mv);

/* 读充电器能力表（0x60~0x8F 原始数据），返回读到的字节数 */
int bsp_pd_read_capability(uint8_t *buf, int buf_len);

/* =========================================================
 * PDO 解析 / 智能协商
 * ========================================================= */

/* 读充电器能力表并解析成 PDO 结构 */
esp_err_t bsp_pd_read_capability_parsed(pd_capability_t *out);

/* 请求功率最大的档位，返回预期电压 mV */
uint16_t bsp_pd_request_best(void);

/* 通用协商：按实际协议选最高可用电压，返回预期电压 mV */
uint16_t bsp_pd_negotiate_max(void);

/* =========================================================
 * 显示辅助
 * ========================================================= */

/* 协议名字符串 */
const char *bsp_pd_protocol_name(const bsp_pd_state_t *st);

/* 当前请求的目标电压（mV），未请求过返回 0 */
uint16_t bsp_pd_get_requested_mv(void);

/* 协商功率（mW）= 请求电压 × 档位电流。未知返回 0 */
uint32_t bsp_pd_get_negotiated_power_mw(void);

#endif /* BSP_PD_H */