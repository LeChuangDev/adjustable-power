#include "bsp_display.h"
#include "bsp_board.h"
#include "co5300_init_cmds.h"

#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/i2c_master.h"

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_cst820.h"

#include "esp_lv_adapter.h"
#include "lvgl.h"

static const char *TAG = "bsp_display";
static esp_lcd_panel_handle_t s_panel = NULL;

/* CO5300 区域对齐回调（LVGL 需要） */
static void co5300_area_rounder_cb(lv_area_t *area, void *user_data)
{
    uint16_t x1 = area->x1;
    uint16_t x2 = area->x2;
    uint16_t y1 = area->y1;
    uint16_t y2 = area->y2;

    area->x1 = (x1 >> 1) << 1;
    area->y1 = (y1 >> 1) << 1;
    area->x2 = ((x2 >> 1) << 1) + 1;
    area->y2 = ((y2 >> 1) << 1) + 1;
}

/* 默认界面：显示项目名 */
static void create_default_ui(void)
{
    lv_obj_t *scr = lv_scr_act();

    lv_obj_set_style_bg_color(scr, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_STATE_DEFAULT);

    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "PSU\n可调电源");
    lv_obj_set_style_text_color(label, lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_center(label);
}

void bsp_display_init(void)
{
    ESP_LOGI(TAG, "初始化: CO5300 QSPI %dx%d", BSP_DISP_WIDTH, BSP_DISP_HEIGHT);

    /* 1. QSPI 总线 */
    spi_bus_config_t bus_cfg = CO5300_PANEL_BUS_QSPI_CONFIG(
        BSP_DISP_SCK_GPIO, BSP_DISP_D0_GPIO, BSP_DISP_D1_GPIO,
        BSP_DISP_D2_GPIO, BSP_DISP_D3_GPIO,
        BSP_DISP_WIDTH * 80 * sizeof(uint16_t));
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    /* 2. Panel IO */
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = BSP_DISP_CS_GPIO,
        .dc_gpio_num = -1,
        .spi_mode = 0,
        .pclk_hz = BSP_DISP_PIXEL_CLK_HZ,
        .trans_queue_depth = 1,
        .lcd_cmd_bits = 32,
        .lcd_param_bits = 8,
        .flags = { .quad_mode = true },
    };
    esp_lcd_panel_io_handle_t panel_io = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST,
                                             &io_cfg, &panel_io));

    /* 3. CO5300 Panel */
    co5300_vendor_config_t vendor_cfg = {
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = lcd_init_cmds_size,
        .flags = { .use_qspi_interface = 1 },
    };
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = BSP_DISP_RST_GPIO,
        .rgb_endian = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor_cfg,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_co5300(panel_io, &panel_cfg, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(s_panel, BSP_DISP_X_GAP, BSP_DISP_Y_GAP));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_co5300_set_brightness(s_panel, 80));

    ESP_LOGI(TAG, "CO5300 显示初始化完成");

    /* 4. CST820 触摸 */
    i2c_master_bus_config_t i2c_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = BSP_TOUCH_I2C_SCL,
        .sda_io_num = BSP_TOUCH_I2C_SDA,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t i2c_bus = NULL;
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_cfg, &i2c_bus));

    esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_CST820_CONFIG();
    tp_io_cfg.scl_speed_hz = BSP_TOUCH_I2C_CLK_HZ;
    esp_lcd_panel_io_handle_t tp_io = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus, &tp_io_cfg, &tp_io));

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = BSP_DISP_WIDTH,
        .y_max = BSP_DISP_HEIGHT,
        .rst_gpio_num = BSP_TOUCH_RST_GPIO,
        .int_gpio_num = BSP_TOUCH_INT_GPIO,
        .levels = { .reset = 0, .interrupt = 0 },
    };
    esp_lcd_touch_handle_t touch = NULL;
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_cst820(tp_io, &tp_cfg, &touch));

    ESP_LOGI(TAG, "CST820 触摸初始化完成");

    /* 5. LVGL 适配层 */
    esp_lv_adapter_config_t adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    adapter_cfg.stack_in_psram = true;
    ESP_ERROR_CHECK(esp_lv_adapter_init(&adapter_cfg));

    esp_lv_adapter_display_config_t disp_cfg =
        ESP_LV_ADAPTER_DISPLAY_SPI_WITH_PSRAM_DEFAULT_CONFIG(
            s_panel, panel_io, BSP_DISP_WIDTH, BSP_DISP_HEIGHT,
            ESP_LV_ADAPTER_ROTATE_0);

    lv_display_t *disp = esp_lv_adapter_register_display(&disp_cfg);
    assert(disp != NULL);

    ESP_ERROR_CHECK(esp_lv_adapter_set_area_rounder_cb(disp,
                                                       co5300_area_rounder_cb, NULL));

    esp_lv_adapter_touch_config_t touch_cfg =
        ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(disp, touch);
    esp_lv_adapter_register_touch(&touch_cfg);

    ESP_ERROR_CHECK(esp_lv_adapter_start());
    ESP_LOGI(TAG, "LVGL 适配层启动完成");
}

void bsp_display_set_brightness(uint8_t percent)
{
    if (s_panel) {
        esp_lcd_panel_co5300_set_brightness(s_panel, percent);
    }
}

uint16_t bsp_display_width(void)  { return BSP_DISP_WIDTH; }
uint16_t bsp_display_height(void) { return BSP_DISP_HEIGHT; }

void bsp_display_create_ui(void)
{
    if (esp_lv_adapter_lock(portMAX_DELAY) == ESP_OK) {
        create_default_ui();
        esp_lv_adapter_unlock();
    }
}