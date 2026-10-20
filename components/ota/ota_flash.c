#include <string.h>
#include "ota_flash.h"
#include "ota_config.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_log.h"
#include "esp_system.h"
#include "mbedtls/sha256.h"

static const char *TAG = "ota_flash";

static esp_ota_handle_t s_handle;
static const esp_partition_t *s_part = NULL;
static bool s_active = false;
static uint32_t s_written = 0;
static uint32_t s_total_len = 0;   /* 期望总长度（START 声明） */

static mbedtls_sha256_context s_sha;
static uint8_t s_sha_result[32];

/* 记录期望固件总长度 */
void ota_flash_set_total_len(uint32_t total_len)
{
    s_total_len = total_len;
}

/* =========================================================
 * 开始升级：
 *   1. 取下一个可用 OTA 分区
 *   2. 预擦除整个分区（一次性，几秒）
 *   3. 初始化 SHA256
 * ========================================================= */
int ota_flash_begin(void)
{
    s_part = esp_ota_get_next_update_partition(NULL);
    if (!s_part) {
        ESP_LOGE(TAG, "无可用 OTA 分区");
        return -1;
    }

    /* 预擦除：擦除只发生一次，之后写入不再卡顿 */
    ESP_LOGW(TAG, "预擦除分区 %s (%.1fMB)...",
             s_part->label, (float)s_part->size / 1024 / 1024);
    esp_err_t e = esp_partition_erase_range(s_part, 0, s_part->size);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "预擦除失败: %s", esp_err_to_name(e));
        return -1;
    }
    ESP_LOGW(TAG, "预擦除完成");

    e = esp_ota_begin(s_part, OTA_SIZE_UNKNOWN, &s_handle);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin 失败: %s", esp_err_to_name(e));
        return -1;
    }

    s_active = true;
    s_written = 0;
    mbedtls_sha256_init(&s_sha);
    mbedtls_sha256_starts(&s_sha, 0);

    ESP_LOGW(TAG, "OTA 开始, 目标: %s", s_part->label);
    return 0;
}

/* =========================================================
 * 写入数据：写 Flash + 更新 SHA256
 * ========================================================= */
int ota_flash_write(const uint8_t *data, int len)
{
    if (!s_active) return -1;

    esp_err_t e = esp_ota_write(s_handle, data, len);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "写入失败: %s", esp_err_to_name(e));
        return -1;
    }

    mbedtls_sha256_update(&s_sha, data, len);
    s_written += len;
    return 0;
}

/* 纯写 Flash，不算 SHA */
int ota_flash_write_raw(const uint8_t *data, int len)
{
    if (!s_active) return -1;
    esp_err_t e = esp_ota_write(s_handle, data, len);
    if (e != ESP_OK) return -1;
    s_written += len;
    return 0;
}

/* 只更新 SHA，不写 Flash */
void ota_flash_update_sha(const uint8_t *data, int len)
{
    if (!s_active) return;
    mbedtls_sha256_update(&s_sha, data, len);
}


/* =========================================================
 * 完成升级：
 *   1. 计算写入数据的 SHA256
 *   2. 与上位机传来的 SHA256 比对（真校验）
 *   3. 结束写入、切换启动分区
 * ========================================================= */
int ota_flash_finish(const uint8_t *expect_sha256)
{
    if (!s_active) return -1;

    mbedtls_sha256_finish(&s_sha, s_sha_result);

    /* ★ 真校验：写入数据的 SHA256 必须和上位机一致 */
    if (expect_sha256 != NULL &&
        memcmp(s_sha_result, expect_sha256, 32) != 0) {
        ESP_LOGE(TAG, "SHA256 校验失败，丢弃固件");
        esp_ota_abort(s_handle);
        s_active = false;
        return -2;
    }

    /* ★ 长度校验：实际写入必须等于声明长度 */
    if (s_total_len > 0 && s_written != s_total_len) {
        ESP_LOGE(TAG, "长度不符: 写入 %lu, 期望 %lu",
                 (unsigned long)s_written, (unsigned long)s_total_len);
        esp_ota_abort(s_handle);
        s_active = false;
        return -3;
    }

    esp_err_t e = esp_ota_end(s_handle);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end 失败: %s", esp_err_to_name(e));
        s_active = false;
        return -1;
    }

    e = esp_ota_set_boot_partition(s_part);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "设置启动分区失败: %s", esp_err_to_name(e));
        s_active = false;
        return -1;
    }

    s_active = false;
    ESP_LOGW(TAG, "OTA 完成, 共 %lu 字节, SHA256 校验通过",
             (unsigned long)s_written);
    return 0;
}

/* =========================================================
 * 中止升级：传输中断时清理
 * ========================================================= */
void ota_flash_abort(void)
{
    if (s_active) {
        esp_ota_abort(s_handle);
        s_active = false;
        ESP_LOGW(TAG, "升级已中止");
    }
}

/* =========================================================
 * 新固件启动成功后调用（app_main 里延迟几秒）：
 * 标记有效，取消 bootloader 的自动回滚。
 * ========================================================= */
void ota_flash_mark_valid(void)
{
    esp_err_t e = esp_ota_mark_app_valid_cancel_rollback();
    if (e == ESP_OK) {
        ESP_LOGW(TAG, "新固件已标记有效（取消回滚）");
    } else {
        /* 没有待确认的 OTA 时返回错误，属正常情况 */
        ESP_LOGD(TAG, "无待确认固件: %s", esp_err_to_name(e));
    }
}

/* =========================================================
 * 新固件启动失败时调用：
 * 标记无效并回滚到上一版本（配合 bootloader 回滚机制）。
 * ========================================================= */
void ota_flash_rollback_and_reboot(void)
{
    ESP_LOGE(TAG, "新固件异常，回滚旧版本");
    esp_ota_mark_app_invalid_rollback_and_reboot();
}