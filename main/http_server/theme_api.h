#ifndef THEME_API_H
#define THEME_API_H

#include "esp_http_server.h"

#define DEFAULT_THEME "dark"
#define DEFAULT_COLORS "{ "\
        "\"--primary-color\":\"#4caf50\", "\
        "\"--primary-color-text\":\"#ffffff\", "\
        "\"--highlight-bg\":\"#4caf50\", "\
        "\"--highlight-text-color\":\"#ffffff\", "\
        "\"--focus-ring\":\"0 0 0 0.2rem rgba(76,175,80,0.2)\", "\
        "\"--slider-bg\":\"#dee2e6\", "\
        "\"--slider-range-bg\":\"#4caf50\", "\
        "\"--slider-handle-bg\":\"#4caf50\", "\
        "\"--progressbar-bg\":\"#dee2e6\", "\
        "\"--progressbar-value-bg\":\"#4caf50\", "\
        "\"--checkbox-border\":\"#4caf50\", "\
        "\"--checkbox-bg\":\"#4caf50\", "\
        "\"--checkbox-hover-bg\":\"#43a047\", "\
        "\"--button-bg\":\"#4caf50\", "\
        "\"--button-hover-bg\":\"#43a047\", "\
        "\"--button-focus-shadow\":\"0 0 0 2px #ffffff, 0 0 0 4px #4caf50\", "\
        "\"--togglebutton-bg\":\"#4caf50\", "\
        "\"--togglebutton-border\":\"1px solid #4caf50\", "\
        "\"--togglebutton-hover-bg\":\"#43a047\", "\
        "\"--togglebutton-hover-border\":\"1px solid #43a047\", "\
        "\"--togglebutton-text-color\":\"#ffffff\" "\
        "}"

// Register theme API endpoints
esp_err_t register_theme_api_endpoints(httpd_handle_t server, void* ctx);

#endif // THEME_API_H
