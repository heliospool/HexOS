#pragma once

#include "esp_err.h"
#include <stdint.h>
#include "lvgl.h"

/* Per-board image theme — one set of background images per board family.
 * Splash screens (initscreen2 / splashscreen2) are shared across all boards
 * and live in images/ui_img_hexos_initscreen2_png / ui_img_hexos_splashscreen2_png. */
typedef struct {
    const lv_image_dsc_t *img_portalscreen;
    const lv_image_dsc_t *img_miningscreen2;
    const lv_image_dsc_t *img_settingsscreen;
} st7789_theme_t;

/* Public API for the ST7789 colour screen (TTGO T-Display S3, 320x170). */

esp_err_t screen_st7789_start(void *pvParameters);
void      screen_st7789_button_press(void);
void      screen_st7789_show_mining(void);
void      screen_st7789_show_portal(const char *ap_ssid);
void      screen_st7789_toggle_display(void);
