/* =========================================================
 * bsp_pd.c — CH224Q PD 诱骗芯片实现
 *
 * 基于 CH224Q 手册 V2.1：
 *   0x09 状态寄存器（只读）
 *   0x0A 电压控制寄存器（只写）
 *   0x50 电流数据寄存器（只读，单位 50mA，仅 PD 有效）
 *   0x51/0x52 AVS 电压配置（100mV 步进）
 *   0x60~0x8F 充电器能力表（PD SRCCAP，仅 PD 有效）
 * ========================================================= */

#include <string.h>
#include "bsp_pd.h"
#include "bsp_board.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "bsp_pd";

/* ---- 寄存器地址 ---- */
#define REG_STATUS      0x09
#define REG_VOLTAGE     0x0A
#define REG_CURRENT     0x50
#define REG_AVS_L       0x51
#define REG_AVS_H       0x52
#define REG_CAP_START   0x60
#define REG_CAP_END     0x8F

/* ---- 内部状态 ---- */
static bool     s_inited = false;
static bool     s_present = false;
static uint16_t s_requested_mv = 0;   /* 记录请求电压（0x0A 只写，读不回）*/

/* 固定档位 → mV 映射 */
static const uint16_t s_level_mv[] = {
    [PD_VOLT_5V]  = 5000,
    [PD_VOLT_9V]  = 9000,
    [PD_VOLT_12V] = 12000,
    [PD_VOLT_15V] = 15000,
    [PD_VOLT_20V] = 20000,
    [PD_VOLT_28V] = 28000,
};

/* =========================================================
 * I2C 基础读写
 * ========================================================= */

static esp_err_t pd_write_reg(uint8_t reg, uint8_t val)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (BSP_PD_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, val, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(BSP_PD_I2C_PORT, cmd,
                                          100 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t pd_read_reg(uint8_t reg, uint8_t *val)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (BSP_PD_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (BSP_PD_I2C_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, val, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(BSP_PD_I2C_PORT, cmd,
                                          100 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
}

static bool pd_probe(void)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (BSP_PD_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(BSP_PD_I2C_PORT, cmd,
                                          100 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    return (ret == ESP_OK);
}

/* =========================================================
 * 初始化
 * ========================================================= */
esp_err_t bsp_pd_init(void)
{
    if (s_inited) return ESP_OK;

    i2c_config_t cfg = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = BSP_PD_I2C_SDA_GPIO,
        .scl_io_num       = BSP_PD_I2C_SCL_GPIO,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = BSP_PD_I2C_FREQ_HZ,
    };
    esp_err_t ret = i2c_param_config(BSP_PD_I2C_PORT, &cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C 参数配置失败: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2c_driver_install(BSP_PD_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C 驱动安装失败: %s", esp_err_to_name(ret));
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(20));

    /* 探测芯片（重试 3 次）*/
    for (int i = 0; i < 3; i++) {
        if (pd_probe()) {
            s_present = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    s_inited = true;

    if (s_present) {
        ESP_LOGW(TAG, "CH224Q 在线 (地址 0x%02X)", BSP_PD_I2C_ADDR);
    } else {
        ESP_LOGW(TAG, "CH224Q 未响应（检查充电器是否插入）");
    }
    return ESP_OK;
}

bool bsp_pd_is_present(void)
{
    return s_present;
}

/* =========================================================
 * 读协议握手状态
 * ========================================================= */
esp_err_t bsp_pd_get_state(bsp_pd_state_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;

    uint8_t v = 0;
    esp_err_t ret = pd_read_reg(REG_STATUS, &v);
    if (ret != ESP_OK) {
        return ret;
    }

    out->epr_active = (v >> 4) & 1;
    out->pd_active  = (v >> 3) & 1;
    out->qc3_active = (v >> 2) & 1;
    out->qc2_active = (v >> 1) & 1;
    out->bc_active  = v & 1;
    return ESP_OK;
}

/* =========================================================
 * 读当前档位最大电流（单位 50mA，仅 PD 有效）
 * ========================================================= */
uint16_t bsp_pd_get_current_limit_ma(void)
{
    uint8_t v = 0;
    if (pd_read_reg(REG_CURRENT, &v) != ESP_OK) {
        return 0;
    }
    return (uint16_t)v * 50;
}

/* =========================================================
 * 请求固定档位
 * ========================================================= */
esp_err_t bsp_pd_request_level(bsp_pd_level_t level)
{
    if (level <= PD_VOLT_28V) {
        s_requested_mv = s_level_mv[level];
    }
    return pd_write_reg(REG_VOLTAGE, (uint8_t)level);
}

/* =========================================================
 * AVS 动态调压
 * ========================================================= */
esp_err_t bsp_pd_set_avs_voltage(uint16_t mv)
{
    if (mv < BSP_PD_BUS_MIN_MV) mv = BSP_PD_BUS_MIN_MV;
    if (mv > BSP_PD_BUS_MAX_MV) mv = BSP_PD_BUS_MAX_MV;

    s_requested_mv = mv;

    uint16_t val = mv / 100;    /* 100mV 步进 */

    esp_err_t ret = pd_write_reg(REG_AVS_L, (uint8_t)(val & 0xFF));
    if (ret != ESP_OK) return ret;

    ret = pd_write_reg(REG_AVS_H, (uint8_t)(((val >> 8) & 0x7F) | 0x80));
    if (ret != ESP_OK) return ret;

    return pd_write_reg(REG_VOLTAGE, PD_VOLT_AVS);
}

/* =========================================================
 * 根据目标输出电压设置母线
 * ========================================================= */
esp_err_t bsp_pd_set_bus_for_output(uint16_t output_mv)
{
    uint32_t bus = (uint32_t)output_mv + BSP_PD_BUS_MARGIN_MV;

    if (bus > BSP_PD_BUS_MAX_MV) bus = BSP_PD_BUS_MAX_MV;
    if (bus < BSP_PD_BUS_MIN_MV) bus = BSP_PD_BUS_MIN_MV;

    return bsp_pd_set_avs_voltage((uint16_t)bus);
}

/* =========================================================
 * 读充电器能力表（原始字节）
 * ========================================================= */
int bsp_pd_read_capability(uint8_t *buf, int buf_len)
{
    if (!buf || buf_len <= 0) return -1;

    int max_read = REG_CAP_END - REG_CAP_START + 1;
    if (buf_len > max_read) buf_len = max_read;

    int i;
    for (i = 0; i < buf_len; i++) {
        if (pd_read_reg(REG_CAP_START + i, &buf[i]) != ESP_OK) {
            break;
        }
    }
    return i;
}

/* =========================================================
 * PDO 解析（USB PD 规范编码）
 * ========================================================= */
static void parse_pdo(const uint8_t *raw, pd_pdo_t *out)
{
    uint32_t v = (uint32_t)raw[0]
               | ((uint32_t)raw[1] << 8)
               | ((uint32_t)raw[2] << 16)
               | ((uint32_t)raw[3] << 24);
    uint32_t type = (v >> 30) & 0x3;

    memset(out, 0, sizeof(*out));

    if (type == 0) {
        /* 固定档位：电压 50mV/LSB，电流 10mA/LSB */
        uint16_t mv = ((v >> 10) & 0x3FF) * 50;
        uint16_t ma = (v & 0x3FF) * 10;
        out->type        = PDO_TYPE_FIXED;
        out->volt_max_mv = mv;
        out->volt_min_mv = mv;
        out->curr_max_ma = ma;
        out->power_mw    = (uint32_t)mv * ma / 1000;
    } else if (type == 2 || type == 3) {
        /* PPS/APDO：可调档 */
        uint16_t vmax = ((v >> 8) & 0xFF) * 100;
        uint16_t vmin = (v & 0xFF) * 100;
        uint16_t imax = ((v >> 17) & 0x7F) * 50;
        out->type        = PDO_TYPE_PPS;
        out->volt_max_mv = vmax;
        out->volt_min_mv = vmin;
        out->curr_max_ma = imax;
        out->power_mw    = (uint32_t)vmax * imax / 1000;
    } else {
        out->type = PDO_TYPE_UNKNOWN;
    }
}

esp_err_t bsp_pd_read_capability_parsed(pd_capability_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;

    uint8_t raw[48];
    int n = bsp_pd_read_capability(raw, sizeof(raw));
    if (n <= 0) return ESP_FAIL;

    memset(out, 0, sizeof(*out));
    out->best_index = -1;

    int count = n / 4;
    if (count > PD_MAX_OPTIONS) count = PD_MAX_OPTIONS;

    uint32_t best_power = 0;
    for (int i = 0; i < count; i++) {
        parse_pdo(&raw[i * 4], &out->pdo[i]);
        if (out->pdo[i].power_mw > best_power) {
            best_power = out->pdo[i].power_mw;
            out->best_index = i;
        }
    }
    out->count = count;
    return ESP_OK;
}

/* =========================================================
 * 请求功率最大的档位
 * ========================================================= */
uint16_t bsp_pd_request_best(void)
{
    pd_capability_t cap;
    if (bsp_pd_read_capability_parsed(&cap) != ESP_OK ||
        cap.best_index < 0) {
        ESP_LOGW(TAG, "能力表无效，默认请求 20V");
        bsp_pd_request_level(PD_VOLT_20V);
        return 20000;
    }

    pd_pdo_t *best = &cap.pdo[cap.best_index];
    ESP_LOGW(TAG, "最佳档位[%d]: %u~%umV %umA %lumW",
             cap.best_index,
             best->volt_min_mv, best->volt_max_mv,
             best->curr_max_ma,
             (unsigned long)(best->power_mw / 1000));

    if (best->type == PDO_TYPE_FIXED) {
        uint16_t mv = best->volt_max_mv;
        if (mv <= 5000)       bsp_pd_request_level(PD_VOLT_5V);
        else if (mv <= 9000)  bsp_pd_request_level(PD_VOLT_9V);
        else if (mv <= 12000) bsp_pd_request_level(PD_VOLT_12V);
        else if (mv <= 15000) bsp_pd_request_level(PD_VOLT_15V);
        else if (mv <= 20000) bsp_pd_request_level(PD_VOLT_20V);
        else                  bsp_pd_request_level(PD_VOLT_28V);
        return mv;
    }

    if (best->type == PDO_TYPE_PPS) {
        bsp_pd_set_avs_voltage(best->volt_max_mv);
        return best->volt_max_mv;
    }

    bsp_pd_request_level(PD_VOLT_20V);
    return 20000;
}

/* =========================================================
 * 通用协商：任何充电器都尽量拿到最高电压
 * ========================================================= */
uint16_t bsp_pd_negotiate_max(void)
{
    bsp_pd_state_t st;
    if (bsp_pd_get_state(&st) != ESP_OK) {
        return 0;
    }

    /* 1. PD / EPR：读能力表选最优 */
    if (st.epr_active || st.pd_active) {
        ESP_LOGW(TAG, "PD%s: 按能力表选最优档",
                 st.epr_active ? "3.1 EPR" : "3.0");
        return bsp_pd_request_best();
    }

    /* 2. QC3.0：请求 20V，芯片自动向下兼容 */
    if (st.qc3_active) {
        ESP_LOGW(TAG, "QC3.0: 请求 20V（不支持自动降档）");
        bsp_pd_request_level(PD_VOLT_20V);
        return 20000;
    }

    /* 3. QC2.0 */
    if (st.qc2_active) {
        ESP_LOGW(TAG, "QC2.0: 请求 20V");
        bsp_pd_request_level(PD_VOLT_20V);
        return 20000;
    }

    /* 4. FCP/AFC/BC：试 12V */
    if (st.bc_active) {
        ESP_LOGW(TAG, "非 PD/QC 快充: 尝试 12V");
        bsp_pd_request_level(PD_VOLT_12V);
        return 12000;
    }

    /* 5. 普通充电器：5V */
    ESP_LOGW(TAG, "普通充电器: 仅 5V");
    return 5000;
}

/* =========================================================
 * 显示辅助
 * ========================================================= */
const char *bsp_pd_protocol_name(const bsp_pd_state_t *st)
{
    if (!st) return "NULL";
    if (st->epr_active) return "PD3.1 EPR";
    if (st->pd_active)  return "PD3.0";
    if (st->qc3_active) return "QC3.0";
    if (st->qc2_active) return "QC2.0";
    if (st->bc_active)  return "BC1.2";
    return "未握手";
}

uint16_t bsp_pd_get_requested_mv(void)
{
    return s_requested_mv;
}

uint32_t bsp_pd_get_negotiated_power_mw(void)
{
    uint16_t mv = s_requested_mv;
    uint16_t ma = bsp_pd_get_current_limit_ma();
    if (mv == 0 || ma == 0) return 0;
    return (uint32_t)mv * ma / 1000;
}