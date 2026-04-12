#pragma once

#include "esp_err.h"
#include <stdint.h>

/* Public API for the ST7789 colour screen (TTGO T-Display S3, 320x170). */

esp_err_t screen_st7789_start(void *pvParameters);
void      screen_st7789_button_press(void);
void      screen_st7789_show_mining(void);
void      screen_st7789_show_portal(const char *ap_ssid);
void      screen_st7789_toggle_display(void);
