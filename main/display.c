#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "lvgl.h"
#include "lvgl__lvgl/src/themes/lv_theme_private.h"
#include "esp_lvgl_port.h"
#include "global_state.h"
#include "nvs_config.h"
#include "i2c_bitaxe.h"
#include "driver/i2c_master.h"
#include "driver/i2c_types.h"
#include "esp_lcd_panel_ssd1306.h"
#include "esp_lcd_sh1107.h"
#include "esp_lcd_panel_st7789.h"
#include "driver/gpio.h"

/* TTGO T-Display S3 ST7789 pin definitions */
#define ST7789_PIN_D0       39
#define ST7789_PIN_D1       40
#define ST7789_PIN_D2       41
#define ST7789_PIN_D3       42
#define ST7789_PIN_D4       45
#define ST7789_PIN_D5       46
#define ST7789_PIN_D6       47
#define ST7789_PIN_D7       48
#define ST7789_PIN_WR        8
#define ST7789_PIN_DC        7
#define ST7789_PIN_RST       5
#define ST7789_PIN_CS        6
#define ST7789_PIN_BK_LIGHT 38
#define ST7789_PIN_PWR      15
#define ST7789_PIN_RD        9
#define ST7789_PIXEL_CLK_HZ  (6528000)
#define ST7789_BK_LIGHT_ON   1
#define ST7789_BK_LIGHT_OFF  0
#define ST7789_PSRAM_ALIGN   64
#define ST7789_SRAM_ALIGN     4

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#define DISPLAY_I2C_ADDRESS    0x3C

#define LCD_CMD_BITS           8
#define LCD_PARAM_BITS         8

static const char * TAG = "display";
static const char * LVGL_TAG = "lvgl";

static esp_lcd_panel_handle_t panel_handle = NULL;
static bool display_state_on = false;
static bool s_is_st7789 = false;

static lv_theme_t theme;
static lv_style_t scr_style;

extern const lv_font_t lv_font_portfolio_6x8;

esp_err_t display_on(bool display_on);

static void theme_apply(lv_theme_t *theme, lv_obj_t *obj) {
    if (lv_obj_get_parent(obj) == NULL) {
        lv_obj_add_style(obj, &scr_style, LV_PART_MAIN);
    }
}

static esp_err_t read_display_config(GlobalState * GLOBAL_STATE)
{
    char * display_config_name = nvs_config_get_string(NVS_CONFIG_DISPLAY);
    const DisplayConfig * display_config = get_display_config(display_config_name);

    if (display_config) {
        GLOBAL_STATE->DISPLAY_CONFIG = *display_config;

        ESP_LOGI(TAG, "%s", GLOBAL_STATE->DISPLAY_CONFIG.name);
        free(display_config_name);
        return ESP_OK;
    }

    free(display_config_name);
    return ESP_FAIL;
}

static void my_log_cb(lv_log_level_t level, const char * buf)
{
    switch (level) {
        case LV_LOG_LEVEL_TRACE:
            ESP_LOGV(LVGL_TAG, "%s", buf);
            break;
        case LV_LOG_LEVEL_INFO:
            ESP_LOGI(LVGL_TAG, "%s", buf);
            break;
        case LV_LOG_LEVEL_WARN:
            ESP_LOGW(LVGL_TAG, "%s", buf);
            break;
        case LV_LOG_LEVEL_ERROR:
            ESP_LOGE(LVGL_TAG, "%s", buf);
            break;
        case LV_LOG_LEVEL_USER:
            ESP_LOGI(LVGL_TAG, "%s", buf);
            break;
        case LV_LOG_LEVEL_NONE:
            break;
    }
}

esp_err_t display_init(void * pvParameters)
{
    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;

    ESP_RETURN_ON_ERROR(read_display_config(GLOBAL_STATE), TAG, "Failed to read display config");

    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();

    lvgl_cfg.task_stack_caps = MALLOC_CAP_SPIRAM;

    if (GLOBAL_STATE->DISPLAY_CONFIG.display == NONE) {
        ESP_LOGI(TAG, "Initialize LVGL");
        ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG, "LVGL init failed");
        lv_display_create(1, 1);
        return ESP_OK;
    }

    /* -----------------------------------------------------------------------
     * ST7789 path — Intel 8080 8-bit parallel bus (TTGO T-Display S3)
     * --------------------------------------------------------------------- */
    if (GLOBAL_STATE->DISPLAY_CONFIG.display == ST7789_320x170) {
        s_is_st7789 = true;
        ESP_LOGI(TAG, "Initialize LVGL for ST7789");
        ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG, "LVGL init failed");

        /* GPIO setup */
        gpio_config_t bk_gpio = {
            .pin_bit_mask = (1ULL << ST7789_PIN_BK_LIGHT),
            .mode = GPIO_MODE_OUTPUT,
        };
        ESP_RETURN_ON_ERROR(gpio_config(&bk_gpio), TAG, "BK gpio config failed");
        gpio_set_direction(ST7789_PIN_RD,  GPIO_MODE_OUTPUT);
        gpio_set_direction(ST7789_PIN_PWR, GPIO_MODE_OUTPUT);
        gpio_set_level(ST7789_PIN_RD,  1);
        gpio_set_level(ST7789_PIN_BK_LIGHT, ST7789_BK_LIGHT_OFF);

        /* i80 bus */
        esp_lcd_i80_bus_handle_t i80_bus = NULL;
        esp_lcd_i80_bus_config_t bus_cfg = {
            .dc_gpio_num   = ST7789_PIN_DC,
            .wr_gpio_num   = ST7789_PIN_WR,
            .clk_src       = LCD_CLK_SRC_DEFAULT,
            .data_gpio_nums = {
                ST7789_PIN_D0, ST7789_PIN_D1, ST7789_PIN_D2, ST7789_PIN_D3,
                ST7789_PIN_D4, ST7789_PIN_D5, ST7789_PIN_D6, ST7789_PIN_D7,
            },
            .bus_width            = 8,
            .max_transfer_bytes   = (320 * 170) * sizeof(uint16_t),
            .psram_trans_align    = ST7789_PSRAM_ALIGN,
            .sram_trans_align     = ST7789_SRAM_ALIGN,
        };
        ESP_RETURN_ON_ERROR(esp_lcd_new_i80_bus(&bus_cfg, &i80_bus), TAG, "i80 bus init failed");

        /* Panel IO */
        esp_lcd_panel_io_handle_t io_handle = NULL;
        esp_lcd_panel_io_i80_config_t io_cfg = {
            .cs_gpio_num       = ST7789_PIN_CS,
            .pclk_hz           = ST7789_PIXEL_CLK_HZ,
            .trans_queue_depth = 20,
            .lcd_cmd_bits      = 8,
            .lcd_param_bits    = 8,
            .dc_levels = {
                .dc_idle_level  = 0,
                .dc_cmd_level   = 0,
                .dc_dummy_level = 0,
                .dc_data_level  = 1,
            },
        };
        ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i80(i80_bus, &io_cfg, &io_handle), TAG, "panel IO init failed");

        /* ST7789 panel */
        esp_lcd_panel_dev_config_t panel_cfg = {
            .reset_gpio_num  = ST7789_PIN_RST,
            .rgb_ele_order   = LCD_RGB_ELEMENT_ORDER_RGB,
            .bits_per_pixel  = 16,
        };
        ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(io_handle, &panel_cfg, &panel_handle), TAG, "ST7789 init failed");

        esp_lcd_panel_reset(panel_handle);
        esp_lcd_panel_init(panel_handle);
        /* invert_color=false: our image data is stored in normal (non-inverted) RGB565.
         * swap_xy + mirror are handled via disp_cfg.rotation below so that
         * lvgl_port_disp_rotation_update() applies them and does NOT overwrite them. */
        esp_lcd_panel_invert_color(panel_handle, nvs_config_get_bool(NVS_CONFIG_INVERT_SCREEN));
        esp_lcd_panel_set_gap(panel_handle, 0, 35);
        esp_lcd_panel_disp_on_off(panel_handle, true);

        const lvgl_port_display_cfg_t disp_cfg = {
            .io_handle    = io_handle,
            .panel_handle = panel_handle,
            .buffer_size  = (320 * 170) / 10,
            .double_buffer = false,
            .hres         = 320,
            .vres         = 170,
            .monochrome   = false,
            .color_format = LV_COLOR_FORMAT_RGB565,
            /* rotation: lvgl_port_disp_rotation_update() will apply these via
             * esp_lcd_panel_swap_xy / mirror. Must NOT call those manually above,
             * or the port will overwrite them with zeros. */
            .rotation = {
                .swap_xy  = true,
                .mirror_x = true,
                .mirror_y = false,
            },
            .flags = {
                .buff_dma   = true,
                .swap_bytes = true,
                .sw_rotate  = false,
            },
        };

        lv_disp_t *disp = lvgl_port_add_disp(&disp_cfg);
        if (!disp) {
            ESP_LOGE(TAG, "lvgl_port_add_disp failed for ST7789");
            return ESP_FAIL;
        }

        /* ST7789: hardware rotation is handled by swap_xy+mirror above.
         * Do NOT apply LVGL software rotation — that would swap hres/vres in LVGL
         * (making it see a 170x320 display) which causes only ~53% horizontal fill. */

        /* Power on backlight */
        gpio_set_level(ST7789_PIN_PWR, 1);
        gpio_set_level(ST7789_PIN_BK_LIGHT, ST7789_BK_LIGHT_ON);
        display_state_on = true;

        GLOBAL_STATE->SYSTEM_MODULE.is_screen_active = true;
        ESP_LOGI(TAG, "ST7789 display init success");
        return ESP_OK;
    }

    /* -----------------------------------------------------------------------
     * I2C OLED path (SSD1306 / SSD1309 / SH1107)
     * --------------------------------------------------------------------- */
    i2c_master_bus_handle_t i2c_master_bus_handle;
    ESP_RETURN_ON_ERROR(i2c_bitaxe_get_master_bus_handle(&i2c_master_bus_handle), TAG, "Failed to get i2c master bus handle");

    ESP_LOGI(TAG, "Install panel IO");
    esp_lcd_panel_io_i2c_config_t io_config = {
        .scl_speed_hz = I2C_BUS_SPEED_HZ,
        .dev_addr = DISPLAY_I2C_ADDRESS,
        .control_phase_bytes = 1,
        .lcd_cmd_bits = LCD_CMD_BITS,
        .lcd_param_bits = LCD_PARAM_BITS,
    };

    switch (GLOBAL_STATE->DISPLAY_CONFIG.display) {
        case SSD1306:
        case SSD1309:
            io_config.dc_bit_offset = 6;
            break;
        case SH1107:
            io_config.dc_bit_offset = 0;
            io_config.flags.disable_control_phase = 1;
            break;
        default:
            return ESP_FAIL;
    }
    
    esp_lcd_panel_io_handle_t io_handle = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(i2c_master_bus_handle, &io_config, &io_handle), TAG, "Failed to initialise i2c panel bus");

    ESP_LOGI(TAG, "Install panel driver");
    esp_lcd_panel_dev_config_t panel_config = {
        .bits_per_pixel = 1,
        .reset_gpio_num = -1,
    };

    switch (GLOBAL_STATE->DISPLAY_CONFIG.display) {
        case SSD1306:
        case SSD1309:
            esp_lcd_panel_ssd1306_config_t ssd1306_config = {
                .height = GLOBAL_STATE->DISPLAY_CONFIG.v_res,
            };
            panel_config.vendor_config = &ssd1306_config;
            ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ssd1306(io_handle, &panel_config, &panel_handle), TAG, "No display found");
            break;
        case SH1107:
            ESP_RETURN_ON_ERROR(esp_lcd_new_panel_sh1107(io_handle, &panel_config, &panel_handle), TAG, "No display found");
            break;
        default:
            return ESP_FAIL;
    }

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel_handle), TAG, "Panel reset failed");
    esp_err_t esp_lcd_panel_init_err = esp_lcd_panel_init(panel_handle);
    if (esp_lcd_panel_init_err != ESP_OK) {
        ESP_LOGE(TAG, "Panel init failed, no display connected?");
    }  else {
        bool invert_screen = nvs_config_get_bool(NVS_CONFIG_INVERT_SCREEN);
        ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(panel_handle, invert_screen), TAG, "Panel invert failed");
        // ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(panel_handle, false, false), TAG, "Panel mirror failed");

        if (GLOBAL_STATE->DISPLAY_CONFIG.display == SH1107) {
            uint8_t display_offset = nvs_config_get_u16(NVS_CONFIG_DISPLAY_OFFSET);
            if (display_offset != LCD_SH1107_PARAM_DEFAULT_DISP_OFFSET) {
                ESP_LOGI(TAG, "SH1107 Display Offset: 0x%02x", display_offset);
                esp_lcd_panel_io_tx_param(io_handle, LCD_SH1107_I2C_CMD, (uint8_t[]) { LCD_SH1107_PARAM_SET_DISP_OFFSET, display_offset }, 2);
            }
        }
    }

    ESP_LOGI(TAG, "Initialize LVGL");

    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG, "LVGL init failed");

    lv_log_register_print_cb(my_log_cb);

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel_handle,
        .buffer_size = GLOBAL_STATE->DISPLAY_CONFIG.h_res * GLOBAL_STATE->DISPLAY_CONFIG.v_res,
        .double_buffer = true,
        .hres = GLOBAL_STATE->DISPLAY_CONFIG.h_res,
        .vres = GLOBAL_STATE->DISPLAY_CONFIG.v_res,
        .monochrome = true,
        .color_format = LV_COLOR_FORMAT_I1,
        .flags = {
            .swap_bytes = false,
            .sw_rotate = false,
        }
    };

    lv_disp_t * disp = lvgl_port_add_disp(&disp_cfg);
    if (!disp) { // Check if disp is NULL
        ESP_LOGE(TAG, "lvgl_port_add_disp failed!");
        // Potential cleanup
        // if (panel_handle) esp_lcd_panel_del(panel_handle);
        // if (io_handle) esp_lcd_panel_io_del(io_handle);
        return ESP_FAIL;
    }

    if (esp_lcd_panel_init_err == ESP_OK) {
        if (lvgl_port_lock(0)) {

            uint16_t rotation = nvs_config_get_u16(NVS_CONFIG_ROTATION);

            ESP_LOGI(TAG, "Rotation: %d", rotation);
            switch(rotation) {
                case 90:
                    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_90);
                    break;
                case 180:
                    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_180);
                    break;
                case 270:
                    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_270);
                    break;
            }

            lv_style_init(&scr_style);
            lv_style_set_text_font(&scr_style, &lv_font_portfolio_6x8);
            lv_style_set_bg_opa(&scr_style, LV_OPA_COVER);

            lv_theme_set_apply_cb(&theme, theme_apply);
            
            lv_display_set_theme(disp, &theme);
            lvgl_port_unlock();
        }

        // Only turn on the screen when it has been cleared
        ESP_RETURN_ON_ERROR(display_on(true), TAG, "Display on failed");

        GLOBAL_STATE->SYSTEM_MODULE.is_screen_active = true;
    } else {
        ESP_LOGW(TAG, "No display found or panel init failed. Screen not active.");
        GLOBAL_STATE->SYSTEM_MODULE.is_screen_active = false;
    }

    ESP_LOGI(TAG, "Display init success!");

    return ESP_OK;
}

esp_err_t display_on(bool on)
{
    if (NULL == panel_handle) return ESP_OK;

    bool is_st7789 = s_is_st7789;

    if (on && !display_state_on) {
        if (is_st7789) {
            /* Restore backlight before un-blanking the panel */
            gpio_set_level(ST7789_PIN_BK_LIGHT, ST7789_BK_LIGHT_ON);
        }
        ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel_handle, true), TAG, "Panel display on failed");
        display_state_on = true;
    } else if (!on && display_state_on) {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel_handle, false), TAG, "Panel display off failed");
        if (is_st7789) {
            /* Cut backlight power so the display is physically dark */
            gpio_set_level(ST7789_PIN_BK_LIGHT, ST7789_BK_LIGHT_OFF);
        }
        display_state_on = false;
    }

    return ESP_OK;
}

esp_err_t display_toggle(void)
{
    return display_on(!display_state_on);
}

const DisplayConfig * get_display_config(const char * name)
{
    for (int i = 0 ; i < ARRAY_SIZE(display_configs); i++) {
        if (strcmp(display_configs[i].name, name) == 0) {
            return &display_configs[i];
        }
    }
    return NULL;
}
