/*
 * screen_st7789.c — Colour screen support for TTGO T-Display S3 (ST7789, 320x170).
 *
 * Ported from upstream ESP-Miner-NerdQAxePlus ui.cpp / displayDriver.cpp
 * (C++ LVGL v8) to HexOS C / LVGL v9.
 *
 * Screen carousel (button press cycles):
 *   Splash → Mining → Settings → Mining → …
 *
 * Data is refreshed every SCREEN_UPDATE_MS via an LVGL timer callback that
 * reads directly from GlobalState — the same pattern used by screen.c.
 */

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_err.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"

#include "global_state.h"
#include "nvs_config.h"
#include "display.h"
#include "screen_st7789.h"

static const char *TAG = "screen_st7789";

#define SCREEN_UPDATE_MS 500
#define SPLASH1_MS       2000
#define SPLASH2_MS       3000

/* -------------------------------------------------------------------------
 * External font declarations (from display_st7789/fonts/)
 * ------------------------------------------------------------------------- */
LV_FONT_DECLARE(ui_font_DigitalNumbers16);
LV_FONT_DECLARE(ui_font_DigitalNumbers28);
LV_FONT_DECLARE(ui_font_OpenSansBold13);
LV_FONT_DECLARE(ui_font_OpenSansBold14);
LV_FONT_DECLARE(ui_font_OpenSansBold24);
LV_FONT_DECLARE(ui_font_OpenSansBold45);
LV_FONT_DECLARE(ui_font_vt323_21);
LV_FONT_DECLARE(ui_font_vt323_35);
extern const lv_font_t lv_font_portfolio_6x8; /* HexOS default small font */

/* -------------------------------------------------------------------------
 * External image declarations (from display_st7789/images/)
 * ------------------------------------------------------------------------- */
LV_IMG_DECLARE(ui_img_hexos_initscreen2_png);
LV_IMG_DECLARE(ui_img_hexos_splashscreen2_png);
LV_IMG_DECLARE(ui_img_hexos_portalscreen_png);
LV_IMG_DECLARE(ui_img_hexos_miningscreen2_png);
LV_IMG_DECLARE(ui_img_hexos_settingsscreen_png);
LV_IMG_DECLARE(ui_img_hexos_found_block_png);
LV_IMG_DECLARE(ui_img_hexos_safe_png);

/* -------------------------------------------------------------------------
 * Screen state machine
 * ------------------------------------------------------------------------- */
typedef enum {
    SCR_SPLASH1 = 0,
    SCR_SPLASH2,
    SCR_MINING,
    SCR_SETTINGS,
    SCR_PORTAL,
    SCR_POWEROFF,
} scr_state_t;

/* -------------------------------------------------------------------------
 * Module-level state
 * ------------------------------------------------------------------------- */
static GlobalState *gs;
static scr_state_t current_state = SCR_SPLASH1;
static int64_t     state_start_us = 0;

/* Display sleep tracking */
static int64_t     last_active_us  = 0;  /* updated on every button press */
static bool        display_sleeping = false;

/* Screen objects */
static lv_obj_t *scr_splash1    = NULL;
static lv_obj_t *scr_splash2    = NULL;
static lv_obj_t *scr_mining      = NULL;
static lv_obj_t *scr_settings    = NULL;
static lv_obj_t *scr_portal     = NULL;
static lv_obj_t *scr_poweroff   = NULL;

/* Mining screen labels */
static lv_obj_t *lb_hashrate    = NULL;
static lv_obj_t *lb_efficiency  = NULL;
static lv_obj_t *lb_power       = NULL;
static lv_obj_t *lb_vinput      = NULL;
static lv_obj_t *lb_vcore       = NULL;
static lv_obj_t *lb_current     = NULL;
static lv_obj_t *lb_temp        = NULL;
static lv_obj_t *lb_fan_rpm     = NULL;
static lv_obj_t *lb_time        = NULL;
static lv_obj_t *lb_ip          = NULL;
static lv_obj_t *lb_best_diff   = NULL;
static lv_obj_t *lb_asic        = NULL;

/* Settings screen labels */
static lv_obj_t *lb_hash_set    = NULL;
static lv_obj_t *lb_shares      = NULL;
static lv_obj_t *lb_best_diff_set = NULL;
static lv_obj_t *lb_ip_set      = NULL;
static lv_obj_t *lb_vcore_set   = NULL;
static lv_obj_t *lb_freq_set    = NULL;
static lv_obj_t *lb_fan_set     = NULL;
static lv_obj_t *lb_pool_set    = NULL;
static lv_obj_t *lb_port_set    = NULL;

/* Portal screen */
static lv_obj_t *lb_portal_ssid  = NULL;

/* Poweroff / overheat screen */
static lv_obj_t *lb_poweroff_ip  = NULL;

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */
static void format_uptime(char *buf, size_t len)
{
    double up_s = (double)(esp_timer_get_time() - gs->SYSTEM_MODULE.start_time) / 1e6;
    int days    = (int)(up_s / 86400);
    int rem     = (int)up_s % 86400;
    int hours   = rem / 3600;
    rem        %= 3600;
    int mins    = rem / 60;
    int secs    = rem % 60;
    snprintf(buf, len, "%dd %dh %dm %ds", days, hours, mins, secs);
}

static void format_hashrate(char *buf, size_t len, float gh)
{
    if (gh >= 1000.0f)
        snprintf(buf, len, "%.2fT", gh / 1000.0f);
    else
        snprintf(buf, len, "%.1fG", gh);
}

/* -------------------------------------------------------------------------
 * Screen init functions
 * ------------------------------------------------------------------------- */
static void splash1_screen_init(void)
{
    scr_splash1 = lv_obj_create(NULL);
    lv_obj_clear_flag(scr_splash1, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *img = lv_img_create(scr_splash1);
    lv_img_set_src(img, &ui_img_hexos_initscreen2_png);
    lv_obj_set_width(img, LV_SIZE_CONTENT);
    lv_obj_set_height(img, LV_SIZE_CONTENT);
    lv_obj_set_align(img, LV_ALIGN_CENTER);
    lv_obj_add_flag(img, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(img, LV_OBJ_FLAG_SCROLLABLE);
}

static void splash2_screen_init(void)
{
    scr_splash2 = lv_obj_create(NULL);
    lv_obj_clear_flag(scr_splash2, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *img = lv_img_create(scr_splash2);
    lv_img_set_src(img, &ui_img_hexos_splashscreen2_png);
    lv_obj_set_width(img, LV_SIZE_CONTENT);
    lv_obj_set_height(img, LV_SIZE_CONTENT);
    lv_obj_set_align(img, LV_ALIGN_CENTER);
    lv_obj_add_flag(img, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(img, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lb = lv_label_create(scr_splash2);
    lv_obj_set_width(lb, LV_SIZE_CONTENT);
    lv_obj_set_height(lb, LV_SIZE_CONTENT);
    lv_obj_set_x(lb, -31);
    lv_obj_set_y(lb, -40);
    lv_obj_set_align(lb, LV_ALIGN_RIGHT_MID);
    lv_label_set_text(lb, "Connecting...");
    lv_obj_set_style_text_color(lb, lv_color_hex(0xDEDADE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(lb, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lb, &ui_font_OpenSansBold13, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void portal_screen_init(void)
{
    scr_portal = lv_obj_create(NULL);
    lv_obj_clear_flag(scr_portal, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *img = lv_img_create(scr_portal);
    lv_img_set_src(img, &ui_img_hexos_portalscreen_png);
    lv_obj_set_width(img, LV_SIZE_CONTENT);
    lv_obj_set_height(img, LV_SIZE_CONTENT);
    lv_obj_set_align(img, LV_ALIGN_CENTER);
    lv_obj_add_flag(img, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(img, LV_OBJ_FLAG_SCROLLABLE);

    lb_portal_ssid = lv_label_create(scr_portal);
    lv_obj_set_width(lb_portal_ssid, LV_SIZE_CONTENT);
    lv_obj_set_height(lb_portal_ssid, LV_SIZE_CONTENT);
    lv_obj_set_x(lb_portal_ssid, 75);
    lv_obj_set_y(lb_portal_ssid, 52);
    lv_obj_set_align(lb_portal_ssid, LV_ALIGN_LEFT_MID);
    lv_label_set_text(lb_portal_ssid, gs ? gs->SYSTEM_MODULE.ap_ssid : "HexOS_????");
    lv_obj_set_style_text_font(lb_portal_ssid, &ui_font_OpenSansBold13, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void mining_screen_init(void)
{
    scr_mining = lv_obj_create(NULL);
    lv_obj_clear_flag(scr_mining, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *img = lv_img_create(scr_mining);
    lv_img_set_src(img, &ui_img_hexos_miningscreen2_png);
    lv_obj_set_width(img, LV_SIZE_CONTENT);
    lv_obj_set_height(img, LV_SIZE_CONTENT);
    lv_obj_set_align(img, LV_ALIGN_CENTER);
    lv_obj_add_flag(img, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(img, LV_OBJ_FLAG_SCROLLABLE);

    /* Vin */
    lb_vinput = lv_label_create(scr_mining);
    lv_obj_set_width(lb_vinput, LV_SIZE_CONTENT);
    lv_obj_set_height(lb_vinput, LV_SIZE_CONTENT);
    lv_obj_set_x(lb_vinput, 234); lv_obj_set_y(lb_vinput, -34);
    lv_obj_set_align(lb_vinput, LV_ALIGN_LEFT_MID);
    lv_label_set_text(lb_vinput, "12V");
    lv_obj_set_style_text_color(lb_vinput, lv_color_hex(0xDEDADE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lb_vinput, &ui_font_OpenSansBold14, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* Vcore */
    lb_vcore = lv_label_create(scr_mining);
    lv_obj_set_width(lb_vcore, LV_SIZE_CONTENT);
    lv_obj_set_height(lb_vcore, LV_SIZE_CONTENT);
    lv_obj_set_x(lb_vcore, 234); lv_obj_set_y(lb_vcore, -12);
    lv_obj_set_align(lb_vcore, LV_ALIGN_LEFT_MID);
    lv_label_set_text(lb_vcore, "1200mV");
    lv_obj_set_style_text_color(lb_vcore, lv_color_hex(0xDEDEDE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lb_vcore, &ui_font_OpenSansBold14, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* Current */
    lb_current = lv_label_create(scr_mining);
    lv_obj_set_width(lb_current, LV_SIZE_CONTENT);
    lv_obj_set_height(lb_current, LV_SIZE_CONTENT);
    lv_obj_set_x(lb_current, 234); lv_obj_set_y(lb_current, 10);
    lv_obj_set_align(lb_current, LV_ALIGN_LEFT_MID);
    lv_label_set_text(lb_current, "0mA");
    lv_obj_set_style_text_font(lb_current, &ui_font_OpenSansBold14, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* Power */
    lb_power = lv_label_create(scr_mining);
    lv_obj_set_width(lb_power, LV_SIZE_CONTENT);
    lv_obj_set_height(lb_power, LV_SIZE_CONTENT);
    lv_obj_set_x(lb_power, 234); lv_obj_set_y(lb_power, 32);
    lv_obj_set_align(lb_power, LV_ALIGN_LEFT_MID);
    lv_label_set_text(lb_power, "0W");
    lv_obj_set_style_text_font(lb_power, &ui_font_OpenSansBold14, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* Efficiency */
    lb_efficiency = lv_label_create(scr_mining);
    lv_obj_set_width(lb_efficiency, LV_SIZE_CONTENT);
    lv_obj_set_height(lb_efficiency, LV_SIZE_CONTENT);
    lv_obj_set_x(lb_efficiency, -43); lv_obj_set_y(lb_efficiency, 61);
    lv_obj_set_align(lb_efficiency, LV_ALIGN_RIGHT_MID);
    lv_label_set_text(lb_efficiency, "n/a");
    lv_obj_set_style_text_color(lb_efficiency, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lb_efficiency, &ui_font_DigitalNumbers16, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* Temp */
    lb_temp = lv_label_create(scr_mining);
    lv_obj_set_width(lb_temp, LV_SIZE_CONTENT);
    lv_obj_set_height(lb_temp, LV_SIZE_CONTENT);
    lv_obj_set_x(lb_temp, -139); lv_obj_set_y(lb_temp, 24);
    lv_obj_set_align(lb_temp, LV_ALIGN_RIGHT_MID);
    lv_label_set_text(lb_temp, "--");
    lv_obj_set_style_text_color(lb_temp, lv_color_hex(0xDEDADE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lb_temp, &ui_font_OpenSansBold14, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* Uptime */
    lb_time = lv_label_create(scr_mining);
    lv_obj_set_width(lb_time, LV_SIZE_CONTENT);
    lv_obj_set_height(lb_time, LV_SIZE_CONTENT);
    lv_obj_set_x(lb_time, -190); lv_obj_set_y(lb_time, 0);
    lv_obj_set_align(lb_time, LV_ALIGN_RIGHT_MID);
    lv_label_set_text(lb_time, "0d 0h 0m");
    lv_obj_set_style_text_color(lb_time, lv_color_hex(0xDEEE00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(lb_time, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lb_time, &ui_font_OpenSansBold13, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* IP address */
    lb_ip = lv_label_create(scr_mining);
    lv_obj_set_width(lb_ip, LV_SIZE_CONTENT);
    lv_obj_set_height(lb_ip, LV_SIZE_CONTENT);
    lv_obj_set_x(lb_ip, -16); lv_obj_set_y(lb_ip, -77);
    lv_obj_set_align(lb_ip, LV_ALIGN_CENTER);
    lv_label_set_text(lb_ip, "0.0.0.0");
    lv_obj_set_style_text_color(lb_ip, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(lb_ip, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lb_ip, &lv_font_portfolio_6x8, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* Best difficulty */
    lb_best_diff = lv_label_create(scr_mining);
    lv_obj_set_width(lb_best_diff, LV_SIZE_CONTENT);
    lv_obj_set_height(lb_best_diff, LV_SIZE_CONTENT);
    lv_obj_set_x(lb_best_diff, 34); lv_obj_set_y(lb_best_diff, 21);
    lv_obj_set_align(lb_best_diff, LV_ALIGN_LEFT_MID);
    lv_label_set_text(lb_best_diff, "0");
    lv_obj_set_style_text_color(lb_best_diff, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lb_best_diff, &ui_font_OpenSansBold14, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* Hashrate (large digital) */
    lb_hashrate = lv_label_create(scr_mining);
    lv_obj_set_width(lb_hashrate, LV_SIZE_CONTENT);
    lv_obj_set_height(lb_hashrate, LV_SIZE_CONTENT);
    lv_obj_set_x(lb_hashrate, -208); lv_obj_set_y(lb_hashrate, 59);
    lv_obj_set_align(lb_hashrate, LV_ALIGN_RIGHT_MID);
    lv_label_set_text(lb_hashrate, "0.0");
    lv_obj_set_style_text_color(lb_hashrate, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(lb_hashrate, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lb_hashrate, &ui_font_DigitalNumbers28, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* Fan RPM */
    lb_fan_rpm = lv_label_create(scr_mining);
    lv_obj_set_width(lb_fan_rpm, LV_SIZE_CONTENT);
    lv_obj_set_height(lb_fan_rpm, LV_SIZE_CONTENT);
    lv_obj_set_x(lb_fan_rpm, 20); lv_obj_set_y(lb_fan_rpm, -9);
    lv_obj_set_align(lb_fan_rpm, LV_ALIGN_CENTER);
    lv_label_set_text(lb_fan_rpm, "0");
    lv_obj_set_style_text_color(lb_fan_rpm, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lb_fan_rpm, &ui_font_OpenSansBold13, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* ASIC model */
    lb_asic = lv_label_create(scr_mining);
    lv_obj_set_width(lb_asic, LV_SIZE_CONTENT);
    lv_obj_set_height(lb_asic, LV_SIZE_CONTENT);
    lv_obj_set_x(lb_asic, 111); lv_obj_set_y(lb_asic, -66);
    lv_obj_set_align(lb_asic, LV_ALIGN_CENTER);
    lv_label_set_text(lb_asic, gs ? gs->DEVICE_CONFIG.family.name : "");
    lv_obj_set_style_text_color(lb_asic, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lb_asic, &ui_font_OpenSansBold14, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void settings_screen_init(void)
{
    scr_settings = lv_obj_create(NULL);
    lv_obj_clear_flag(scr_settings, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *img = lv_img_create(scr_settings);
    lv_img_set_src(img, &ui_img_hexos_settingsscreen_png);
    lv_obj_set_width(img, LV_SIZE_CONTENT);
    lv_obj_set_height(img, LV_SIZE_CONTENT);
    lv_obj_set_align(img, LV_ALIGN_CENTER);
    lv_obj_add_flag(img, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(img, LV_OBJ_FLAG_SCROLLABLE);

    /* IP */
    lb_ip_set = lv_label_create(scr_settings);
    lv_obj_set_x(lb_ip_set, -16); lv_obj_set_y(lb_ip_set, -77);
    lv_obj_set_align(lb_ip_set, LV_ALIGN_CENTER);
    lv_label_set_text(lb_ip_set, "0.0.0.0");
    lv_obj_set_style_text_color(lb_ip_set, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lb_ip_set, &lv_font_portfolio_6x8, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* Best diff */
    lb_best_diff_set = lv_label_create(scr_settings);
    lv_obj_set_x(lb_best_diff_set, 34); lv_obj_set_y(lb_best_diff_set, 21);
    lv_obj_set_align(lb_best_diff_set, LV_ALIGN_LEFT_MID);
    lv_label_set_text(lb_best_diff_set, "0");
    lv_obj_set_style_text_color(lb_best_diff_set, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lb_best_diff_set, &ui_font_OpenSansBold14, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* Vcore */
    lb_vcore_set = lv_label_create(scr_settings);
    lv_obj_set_x(lb_vcore_set, 43); lv_obj_set_y(lb_vcore_set, -45);
    lv_obj_set_align(lb_vcore_set, LV_ALIGN_LEFT_MID);
    lv_label_set_text(lb_vcore_set, "1200mV");
    lv_obj_set_style_text_color(lb_vcore_set, lv_color_hex(0xDEDADE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lb_vcore_set, &ui_font_OpenSansBold14, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* Frequency */
    lb_freq_set = lv_label_create(scr_settings);
    lv_obj_set_x(lb_freq_set, 43); lv_obj_set_y(lb_freq_set, -25);
    lv_obj_set_align(lb_freq_set, LV_ALIGN_LEFT_MID);
    lv_label_set_text(lb_freq_set, "490");
    lv_obj_set_style_text_color(lb_freq_set, lv_color_hex(0xDEDADE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lb_freq_set, &ui_font_OpenSansBold14, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* Fan */
    lb_fan_set = lv_label_create(scr_settings);
    lv_obj_set_x(lb_fan_set, 43); lv_obj_set_y(lb_fan_set, -5);
    lv_obj_set_align(lb_fan_set, LV_ALIGN_LEFT_MID);
    lv_label_set_text(lb_fan_set, "AUTO");
    lv_obj_set_style_text_color(lb_fan_set, lv_color_hex(0xDEDADE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lb_fan_set, &ui_font_OpenSansBold14, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* Pool URL */
    lb_pool_set = lv_label_create(scr_settings);
    lv_obj_set_x(lb_pool_set, 169); lv_obj_set_y(lb_pool_set, -9);
    lv_obj_set_align(lb_pool_set, LV_ALIGN_LEFT_MID);
    lv_label_set_text(lb_pool_set, "---");
    lv_obj_set_style_text_color(lb_pool_set, lv_color_hex(0xDEDADE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lb_pool_set, &ui_font_OpenSansBold13, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* Port */
    lb_port_set = lv_label_create(scr_settings);
    lv_obj_set_x(lb_port_set, 211); lv_obj_set_y(lb_port_set, 13);
    lv_obj_set_align(lb_port_set, LV_ALIGN_LEFT_MID);
    lv_label_set_text(lb_port_set, "3333");
    lv_obj_set_style_text_color(lb_port_set, lv_color_hex(0xDEDADE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lb_port_set, &ui_font_OpenSansBold13, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* Hashrate (copy of mining hashrate) */
    lb_hash_set = lv_label_create(scr_settings);
    lv_obj_set_x(lb_hash_set, -208); lv_obj_set_y(lb_hash_set, 59);
    lv_obj_set_align(lb_hash_set, LV_ALIGN_RIGHT_MID);
    lv_label_set_text(lb_hash_set, "0.0");
    lv_obj_set_style_text_color(lb_hash_set, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(lb_hash_set, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lb_hash_set, &ui_font_DigitalNumbers28, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* Shares accepted/rejected */
    lb_shares = lv_label_create(scr_settings);
    lv_obj_set_x(lb_shares, -23); lv_obj_set_y(lb_shares, 58);
    lv_obj_set_align(lb_shares, LV_ALIGN_RIGHT_MID);
    lv_label_set_text(lb_shares, "0/0");
    lv_obj_set_style_text_color(lb_shares, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lb_shares, &ui_font_OpenSansBold14, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void poweroff_screen_init(void)
{
    scr_poweroff = lv_obj_create(NULL);
    lv_obj_clear_flag(scr_poweroff, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr_poweroff, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr_poweroff, 255, LV_PART_MAIN);

    lv_obj_t *img = lv_img_create(scr_poweroff);
    lv_img_set_src(img, &ui_img_hexos_safe_png);
    lv_obj_set_align(img, LV_ALIGN_CENTER);

    /* IP address shown during overheat so user can connect and fix settings */
    lb_poweroff_ip = lv_label_create(scr_poweroff);
    lv_obj_set_align(lb_poweroff_ip, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_y(lb_poweroff_ip, -8);
    lv_label_set_text(lb_poweroff_ip, "");
    lv_obj_set_style_text_color(lb_poweroff_ip, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lb_poweroff_ip, &ui_font_OpenSansBold14, LV_PART_MAIN | LV_STATE_DEFAULT);
}

/* -------------------------------------------------------------------------
 * Screen transitions
 * ------------------------------------------------------------------------- */
static void enter_state(scr_state_t new_state)
{
    if (new_state == current_state) return;
    current_state  = new_state;
    state_start_us = esp_timer_get_time();

    switch (new_state) {
    case SCR_SPLASH1:
        lv_screen_load_anim(scr_splash1, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, false);
        break;
    case SCR_SPLASH2:
        lv_screen_load_anim(scr_splash2, LV_SCR_LOAD_ANIM_FADE_IN, 500, 0, false);
        break;
    case SCR_MINING:
        lv_screen_load_anim(scr_mining, LV_SCR_LOAD_ANIM_FADE_IN, 400, 0, false);
        break;
    case SCR_SETTINGS:
        lv_screen_load_anim(scr_settings, LV_SCR_LOAD_ANIM_MOVE_LEFT, 350, 0, false);
        break;

    case SCR_PORTAL:
        if (lb_portal_ssid && gs)
            lv_label_set_text(lb_portal_ssid, gs->SYSTEM_MODULE.ap_ssid);
        lv_screen_load_anim(scr_portal, LV_SCR_LOAD_ANIM_FADE_IN, 500, 0, false);
        break;
    case SCR_POWEROFF:
        lv_screen_load_anim(scr_poweroff, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
        break;
    }
}

/* -------------------------------------------------------------------------
 * Data update — called every SCREEN_UPDATE_MS from LVGL timer
 * ------------------------------------------------------------------------- */
static void update_mining_labels(void)
{
    if (!gs) return;
    SystemModule          *sys = &gs->SYSTEM_MODULE;
    PowerManagementModule *pwr = &gs->POWER_MANAGEMENT_MODULE;
    char buf[32];

    /* Hashrate */
    format_hashrate(buf, sizeof(buf), sys->current_hashrate);
    lv_label_set_text(lb_hashrate, buf);
    lv_label_set_text(lb_hash_set,  buf);

    /* Efficiency (W/GH) */
    float eff = (sys->current_hashrate > 0) ? pwr->power / (sys->current_hashrate / 1000.0f) : 0.0f;
    if (eff > 0 && eff < 10000.0f)
        snprintf(buf, sizeof(buf), "%.1f", eff);
    else
        snprintf(buf, sizeof(buf), "n/a");
    lv_label_set_text(lb_efficiency, buf);

    /* Power */
    snprintf(buf, sizeof(buf), "%.2fW", pwr->power);
    lv_label_set_text(lb_power, buf);

    /* Input voltage (mV → display as V) */
    snprintf(buf, sizeof(buf), "%.1fV", pwr->voltage);
    lv_label_set_text(lb_vinput, buf);

    /* Vcore (already in mV) */
    snprintf(buf, sizeof(buf), "%umV", (unsigned)pwr->core_voltage);
    lv_label_set_text(lb_vcore, buf);
    lv_label_set_text(lb_vcore_set, buf);

    /* Current */
    snprintf(buf, sizeof(buf), "%.0fmA", pwr->current * 1000.0f);
    lv_label_set_text(lb_current, buf);

    /* Temperature */
    snprintf(buf, sizeof(buf), "%.0f", pwr->chip_temp_avg);
    lv_label_set_text(lb_temp, buf);

    /* Fan RPM */
    snprintf(buf, sizeof(buf), "%u", (unsigned)pwr->fan_rpm);
    lv_label_set_text(lb_fan_rpm, buf);

    /* Uptime */
    format_uptime(buf, sizeof(buf));
    lv_label_set_text(lb_time, buf);

    /* IP */
    lv_label_set_text(lb_ip,     sys->ip_addr_str);
    lv_label_set_text(lb_ip_set, sys->ip_addr_str);

    /* Best diff */
    lv_label_set_text(lb_best_diff, sys->best_diff_string);
    lv_label_set_text(lb_best_diff_set, sys->best_diff_string);

    /* Shares */
    snprintf(buf, sizeof(buf), "%llu/%llu",
             (unsigned long long)sys->shares_accepted,
             (unsigned long long)sys->shares_rejected);
    lv_label_set_text(lb_shares, buf);

    /* Pool settings */
    const char *pool_host = sys->is_using_fallback ? sys->fallback_pool_url : sys->pool_url;
    uint16_t    pool_port = sys->is_using_fallback ? sys->fallback_pool_port : sys->pool_port;
    if (pool_host) lv_label_set_text(lb_pool_set, pool_host);
    snprintf(buf, sizeof(buf), "%u", pool_port);
    lv_label_set_text(lb_port_set, buf);

    /* Frequency */
    snprintf(buf, sizeof(buf), "%.0f", pwr->frequency_value);
    lv_label_set_text(lb_freq_set, buf);

    /* Fan mode */
    if (pwr->fan_perc == 0)
        lv_label_set_text(lb_fan_set, "AUTO");
    else {
        snprintf(buf, sizeof(buf), "%.0f%%", pwr->fan_perc * 100.0f);
        lv_label_set_text(lb_fan_set, buf);
    }

}

static void screen_update_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!gs) return;

    int64_t now = esp_timer_get_time();
    SystemModule *sys = &gs->SYSTEM_MODULE;

    /* ── Overheat: force display on and lock to poweroff screen ─────────── */
    if (sys->overheat_mode) {
        if (display_sleeping) {
            last_active_us = now;
            display_on(true);
            display_sleeping = false;
        }
        if (current_state != SCR_POWEROFF) {
            if (lb_poweroff_ip) lv_label_set_text(lb_poweroff_ip, sys->ip_addr_str);
            enter_state(SCR_POWEROFF);
        }
        return;
    }

    /* ── Display sleep / wake via displayTimeout NVS key ─────────────────
     * -1 = always on, 0 = always off, >0 = minutes until sleep           */
    int32_t timeout_cfg = nvs_config_get_i32(NVS_CONFIG_DISPLAY_TIMEOUT);
    if (timeout_cfg == 0) {
        /* always off */
        if (!display_sleeping) {
            display_on(false);
            display_sleeping = true;
        }
    } else if (timeout_cfg > 0) {
        int64_t inactive_ms = (now - last_active_us) / 1000;
        int64_t timeout_ms  = (int64_t)timeout_cfg * 60 * 1000;
        if (!display_sleeping && inactive_ms >= timeout_ms) {
            display_on(false);
            display_sleeping = true;
        }
    }
    /* timeout_cfg == -1: always on, nothing to do */

    /* Don't update labels while sleeping */
    if (display_sleeping) return;

    /* Transition out of splash screens after timeout */
    int64_t elapsed_ms = (now - state_start_us) / 1000;

    if (current_state == SCR_SPLASH1 && elapsed_ms >= SPLASH1_MS) {
        enter_state(SCR_SPLASH2);
        return;
    }
    if (current_state == SCR_SPLASH2 && elapsed_ms >= SPLASH2_MS) {
        if (sys->ap_enabled)
            enter_state(SCR_PORTAL);
        else
            enter_state(SCR_MINING);
        return;
    }

    /* Once we're in portal, check if we connected */
    if (current_state == SCR_PORTAL && sys->is_connected) {
        enter_state(SCR_MINING);
        return;
    }

    /* Update live labels on the data screens */
    if (current_state == SCR_MINING ||
        current_state == SCR_SETTINGS) {
        update_mining_labels();
        /* During OTA show progress in the ASIC name slot */
        if (sys->is_firmware_update && lb_asic)
            lv_label_set_text(lb_asic,
                sys->firmware_update_status[0] ? sys->firmware_update_status : "Updating...");
    }
}

/* -------------------------------------------------------------------------
 * Button press — cycles the carousel for active data screens
 * ------------------------------------------------------------------------- */
void screen_st7789_button_press(void)
{
    if (!lvgl_port_lock(0)) return;

    /* Any button press resets the sleep timer and wakes the display */
    last_active_us = esp_timer_get_time();
    if (display_sleeping) {
        display_on(true);
        display_sleeping = false;
        lvgl_port_unlock();
        return;  /* first press just wakes; don't also cycle the screen */
    }

    switch (current_state) {
    case SCR_MINING:
        enter_state(SCR_SETTINGS);
        break;
    case SCR_SETTINGS:
        enter_state(SCR_MINING);
        break;
    default:
        break;
    }

    lvgl_port_unlock();
}

void screen_st7789_show_mining(void)
{
    if (!lvgl_port_lock(0)) return;
    enter_state(SCR_MINING);
    lvgl_port_unlock();
}

void screen_st7789_show_portal(const char *ap_ssid)
{
    if (!lvgl_port_lock(0)) return;
    if (lb_portal_ssid && ap_ssid)
        lv_label_set_text(lb_portal_ssid, ap_ssid);
    enter_state(SCR_PORTAL);
    lvgl_port_unlock();
}

void screen_st7789_toggle_display(void)
{
    if (!lvgl_port_lock(0)) return;
    last_active_us = esp_timer_get_time();
    if (display_sleeping) {
        display_on(true);
        display_sleeping = false;
    } else {
        display_on(false);
        display_sleeping = true;
    }
    lvgl_port_unlock();
}

/* -------------------------------------------------------------------------
 * Entry point — called from system.c after display_init()
 * ------------------------------------------------------------------------- */
esp_err_t screen_st7789_start(void *pvParameters)
{
    gs = (GlobalState *)pvParameters;

    if (!gs->SYSTEM_MODULE.is_screen_active) {
        ESP_LOGW(TAG, "Screen not active, skipping ST7789 UI init");
        return ESP_OK;
    }

    if (!lvgl_port_lock(0)) {
        ESP_LOGE(TAG, "Failed to lock LVGL");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Initialising ST7789 screens");

    splash1_screen_init();
    splash2_screen_init();
    portal_screen_init();
    mining_screen_init();
    settings_screen_init();
    poweroff_screen_init();

    state_start_us  = esp_timer_get_time();
    last_active_us  = state_start_us;
    current_state   = SCR_SPLASH1;
    lv_screen_load(scr_splash1);

    lv_timer_create(screen_update_cb, SCREEN_UPDATE_MS, NULL);

    lvgl_port_unlock();

    ESP_LOGI(TAG, "ST7789 screens ready");
    return ESP_OK;
}
