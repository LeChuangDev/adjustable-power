/* =========================================================
 * ota_wifi.c — WiFi OTA 传输层（产品版）
 *
 * 功能：
 *   1. 启动前自动扫描，选择干扰最小的信道（1/6/11）
 *   2. SoftAP + TCP 升级服务
 *   3. 接收任务入队，写入任务异步写 Flash
 *
 * 可靠性设计：
 *   - 队列满阻塞 5 秒（防死锁，不触发 NAK 风暴）
 *   - 跨任务变量 volatile 保护
 *   - SHA256 以 START 帧为准，写入后整包校验
 * ========================================================= */

#include <string.h>
#include <stdlib.h>
#include "ota_config.h"
#include "ota_protocol.h"
#include "ota_flash.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "ota_wifi";

/* ---- 升级状态（volatile：跨任务访问） ---- */
typedef enum { ST_IDLE, ST_AUTHED, ST_STARTED, ST_DONE } ota_state_t;
static volatile ota_state_t s_state = ST_IDLE;
static volatile int s_client_fd = -1;

/* ---- 数据队列（接收任务 → 写入任务） ---- */
typedef struct {
    uint16_t len;
    uint8_t  data[OTA_MAX_PAYLOAD];
} ota_data_item_t;
static QueueHandle_t s_data_queue = NULL;

/* 整包校验信息（START 帧声明，写入任务结束时使用） */
static uint8_t s_expected_sha[32];

/* =========================================================
 * 自动选择最优信道
 *
 * 流程：临时切 STA 模式扫描 → 统计各信道 AP 数量
 *      → 在 1/6/11（互不重叠）中选最空闲的
 * ========================================================= */
static uint8_t choose_best_channel(void)
{
    uint8_t best = 1;
    uint16_t ap_num = 0;

    /* 扫描需要 STA 接口，临时切换 */
    esp_wifi_set_mode(WIFI_MODE_STA);
    wifi_config_t sta_cfg = {0};
    esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
    esp_wifi_start();
    vTaskDelay(pdMS_TO_TICKS(200));   /* 等 STA 就绪 */

    wifi_scan_config_t scan_cfg = {
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 200,
    };

    esp_err_t e = esp_wifi_scan_start(&scan_cfg, true);   /* 阻塞扫描 */
    if (e == ESP_OK) {
        esp_wifi_scan_get_ap_num(&ap_num);

        if (ap_num > 0) {
            wifi_ap_record_t *list = calloc(ap_num, sizeof(wifi_ap_record_t));
            if (list) {
                esp_wifi_scan_get_ap_records(&ap_num, list);

                int cnt[14] = {0};   /* 统计 1~13 信道 AP 数 */
                for (int i = 0; i < ap_num; i++) {
                    int ch = list[i].primary;
                    if (ch >= 1 && ch <= 13) cnt[ch]++;
                }

                /* 只在 1/6/11 中选最空闲的 */
                best = 1;
                int min = cnt[1];
                if (cnt[6] < min) { best = 6; min = cnt[6]; }
                if (cnt[11] < min) { best = 11; min = cnt[11]; }

                ESP_LOGW(TAG, "信道统计: ch1=%d ch6=%d ch11=%d",
                         cnt[1], cnt[6], cnt[11]);
                free(list);
            }
        }
    } else {
        ESP_LOGW(TAG, "扫描失败，用默认信道 6");
        best = 6;
    }

    esp_wifi_stop();
    return best;
}

/* =========================================================
 * 发送响应帧
 * ========================================================= */
static void send_frame(uint8_t type, uint16_t seq,
                       const uint8_t *payload, uint16_t len)
{
    static uint8_t out[OTA_HEADER_SIZE + OTA_MAX_PAYLOAD + 1];
    int n = ota_build_frame(type, seq, payload, len, out, sizeof(out));
    if (n > 0 && s_client_fd >= 0) {
        send(s_client_fd, out, n, 0);
    }
}

/* =========================================================
 * 处理认证
 * ========================================================= */
static void handle_auth(const uint8_t *payload, uint16_t len)
{
    if (len == strlen(OTA_AUTH_TOKEN) &&
        memcmp(payload, OTA_AUTH_TOKEN, len) == 0) {
        s_state = ST_AUTHED;
        ESP_LOGW(TAG, "认证通过");
        send_frame(OTA_TYPE_AUTH_ACK, 0, NULL, 0);
    } else {
        ESP_LOGW(TAG, "认证失败");
        send_frame(OTA_TYPE_ERR, 0, NULL, 0);
    }
}

/* =========================================================
 * 处理开始帧：记录总长和期望 SHA256
 * ========================================================= */
static void handle_start(const uint8_t *payload, uint16_t len)
{
    if (s_state != ST_AUTHED) return;
    if (len < (int)sizeof(ota_start_payload_t)) {
        send_frame(OTA_TYPE_ERR, 0, NULL, 0);
        return;
    }

    ota_start_payload_t *p = (ota_start_payload_t *)payload;
    memcpy(s_expected_sha, p->sha256, 32);
    /* 把期望长度传给 flash 层，finish 时校验 */
    ota_flash_set_total_len(p->total_len);
    /* 把期望长度传给 flash 层，finish 时校验 */
    ota_flash_set_total_len(p->total_len);

    ESP_LOGW(TAG, "START: 总长=%lu", (unsigned long)p->total_len);

    if (ota_flash_begin() != 0) {
        send_frame(OTA_TYPE_ERR, 0, NULL, 0);
        return;
    }
    s_state = ST_STARTED;
    send_frame(OTA_TYPE_START_ACK, 0, NULL, 0);
}

/* =========================================================
 * 写入任务（低优先级 1）：只负责写 Flash
 * 不抢接收任务 CPU，ACK 秒回
 * ========================================================= */
static void flash_writer_task(void *arg)
{
    ota_data_item_t item;

    while (1) {
        if (xQueueReceive(s_data_queue, &item, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (item.len == 0xFFFF) {
            break;   /* 结束标志 */
        }

        if (ota_flash_write_raw(item.data, item.len) != 0) {
            ESP_LOGW(TAG, "写入失败，中止");
            break;
        }
    }

    /* 整包校验：SHA256 + 长度（以 START 帧声明为准） */
    if (ota_flash_finish(s_expected_sha) == 0) {
        ESP_LOGW(TAG, "升级成功，2 秒后重启");
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
    } else {
        ESP_LOGW(TAG, "升级校验失败，保持旧固件");
    }

    vTaskDelete(NULL);
}

/* =========================================================
 * TCP 升级主任务
 * ========================================================= */
static void tcp_ota_task(void *arg)
{
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(OTA_TCP_PORT),
    };
    bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr));
    listen(listen_fd, 1);
    ESP_LOGW(TAG, "TCP 升级服务: 端口 %d", OTA_TCP_PORT);

    while (1) {
        s_client_fd = accept(listen_fd, NULL, NULL);
        if (s_client_fd < 0) continue;

        ESP_LOGW(TAG, "客户端已连接");
        s_state = ST_IDLE;

        /* 创建队列和写入任务（优先级 1 最低，不抢接收） */
        s_data_queue = xQueueCreate(OTA_QUEUE_LEN, sizeof(ota_data_item_t));
        xTaskCreate(flash_writer_task, "ota_writer", 8192, NULL, 1, NULL);

        /* 接收超时 60 秒 */
        struct timeval tv = { .tv_sec = 60, .tv_usec = 0 };
        setsockopt(s_client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        int running = 1;
        while (running) {
            /* ---- 读帧头（8 字节） ---- */
            uint8_t hdr_buf[OTA_HEADER_SIZE];
            int got = 0;
            while (got < OTA_HEADER_SIZE) {
                int r = recv(s_client_fd, hdr_buf + got,
                             OTA_HEADER_SIZE - got, 0);
                if (r <= 0) { running = 0; break; }
                got += r;
            }
            if (!running) break;

            ota_frame_hdr_t *hdr = (ota_frame_hdr_t *)hdr_buf;
            if (hdr->magic != OTA_MAGIC) continue;

            int body_len = hdr->len + 1;
            if (body_len > OTA_MAX_PAYLOAD + 1) {
                ESP_LOGW(TAG, "帧长度非法: %d", hdr->len);
                break;
            }

            /* ---- 读 payload + tail ---- */
            static uint8_t body[OTA_MAX_PAYLOAD + 1];
            got = 0;
            while (got < body_len) {
                int r = recv(s_client_fd, body + got, body_len - got, 0);
                if (r <= 0) { running = 0; break; }
                got += r;
            }
            if (!running) break;

            /* ---- 分发 ---- */
            switch (hdr->type) {
            case OTA_TYPE_AUTH:
                handle_auth(body, hdr->len);
                break;

            case OTA_TYPE_START:
                handle_start(body, hdr->len);
                break;

            case OTA_TYPE_DATA: {
                if (s_state != ST_STARTED) break;

                ota_data_item_t item;
                item.len = hdr->len;
                memcpy(item.data, body, hdr->len);

                /* 队列满阻塞 5 秒；超时说明写入任务异常，中止 */
                if (xQueueSend(s_data_queue, &item,
                               pdMS_TO_TICKS(5000)) != pdTRUE) {
                    ESP_LOGW(TAG, "写入任务无响应，中止升级");
                    ota_flash_abort();
                    running = 0;
                    break;
                }

                ota_flash_update_sha(body, hdr->len);
                send_frame(OTA_TYPE_ACK, hdr->seq, NULL, 0);
                break;
            }

            case OTA_TYPE_END: {
                /* SHA 以 START 帧为准，END 帧内容忽略 */
                ota_data_item_t item;
                item.len = 0xFFFF;
                xQueueSend(s_data_queue, &item, portMAX_DELAY);
                send_frame(OTA_TYPE_END_ACK, 0, NULL, 0);
                s_state = ST_DONE;
                running = 0;
                break;
            }

            default:
                break;
            }
        }

        /* ---- 收尾 ---- */
        if (s_state == ST_STARTED) {
            /* 中断：通知写入任务结束并清理 */
            ota_data_item_t item;
            item.len = 0xFFFF;
            xQueueSend(s_data_queue, &item, portMAX_DELAY);
            ota_flash_abort();
            ESP_LOGW(TAG, "升级中断，已清理");
        }

        if (s_data_queue) {
            vQueueDelete(s_data_queue);
            s_data_queue = NULL;
        }

        close(s_client_fd);
        s_client_fd = -1;
        ESP_LOGW(TAG, "客户端断开");
    }
}

/* =========================================================
 * WiFi 初始化：先扫描选信道，再启动 SoftAP
 * ========================================================= */
static void wifi_ap_init(void)
{
    nvs_flash_init();
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    /* 1. 扫描选信道（内部先 STA 后停） */
    uint8_t channel = choose_best_channel();
    ESP_LOGW(TAG, "选择信道: %d", channel);

    /* 2. 切回 AP 模式，用选中的信道 */
    esp_wifi_set_mode(WIFI_MODE_AP);

    wifi_config_t wifi_cfg = {
        .ap = {
            .ssid = OTA_AP_SSID,
            .ssid_len = strlen(OTA_AP_SSID),
            .password = OTA_AP_PASS,
            .max_connection = OTA_AP_MAX_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
            .channel = channel,     /* ★ 动态信道 */
        },
    };
    esp_wifi_set_config(WIFI_IF_AP, &wifi_cfg);
    esp_wifi_start();

    ESP_LOGW(TAG, "SoftAP: %s (信道 %d)", OTA_AP_SSID, channel);
}

void ota_wifi_init(void)
{
    wifi_ap_init();
    xTaskCreate(tcp_ota_task, "tcp_ota", 16384, NULL, 5, NULL);
}