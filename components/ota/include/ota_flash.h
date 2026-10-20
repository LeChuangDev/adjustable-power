#ifndef OTA_FLASH_H
#define OTA_FLASH_H

#include <stdint.h>

/* =========================================================
 * OTA Flash 写入层：负责分区操作、SHA256 校验、回滚
 * ========================================================= */

/* 开始升级：预擦除整个目标分区（消除传输中途擦除卡顿） */
int ota_flash_begin(void);

/* 写入数据（边写边计算 SHA256） */
int ota_flash_write(const uint8_t *data, int len);

/* 完成升级：用传入的 SHA256 校验整包，通过则切换启动分区 */
int ota_flash_finish(const uint8_t *expect_sha256);

/* 中止升级（传输中断时调用，清理状态） */
void ota_flash_abort(void);

/* 新固件启动成功后调用：标记有效，取消自动回滚 */
void ota_flash_mark_valid(void);

/* 新固件启动失败时调用：回滚到上一版本并重启 */
void ota_flash_rollback_and_reboot(void);

/* 纯写 Flash，不算 SHA（写入任务专用，最快） */
int ota_flash_write_raw(const uint8_t *data, int len);

/* 只更新 SHA，不写 Flash（接收任务专用，极快） */
void ota_flash_update_sha(const uint8_t *data, int len);

/* 设置期望固件总长度（START 帧声明，finish 时校验） */
void ota_flash_set_total_len(uint32_t total_len);

#endif