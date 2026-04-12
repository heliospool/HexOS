#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <string.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "driver/gpio.h"

#include "pmbus_commands.h"
#include "TPS53647.h"
#include "i2c_bitaxe.h"
#include "global_state.h"

static const char *TAG = "TPS53647";

/* Power enable GPIO — same pin as CONFIG_GPIO_ASIC_ENABLE (default 10).
 * The TPS53647 EN pin is wired to this GPIO on NerdQaxe+ and NerdOCTAXE boards. */
#define TPS53647_EN_PIN  CONFIG_GPIO_ASIC_ENABLE

/* VID voltage scheme: Vout = (vid - 1) * 0.005 + 0.25, vid=0 means off */
#define TPS53647_HW_MIN_VOLTAGE  0.25f
#define TPS53647_VID_STEP        0.005f
#define TPS53647_VOUT_MIN        1.005f
#define TPS53647_VOUT_MAX        1.4f

/* Over-temperature limits */
#define TPS53647_OT_WARN_LIMIT   95.0f
#define TPS53647_OT_FAULT_LIMIT  125.0f

/* ON_OFF_CONFIG: use CONTROL pin for on/off, soft start/stop enabled */
#define TPS53647_ON_OFF_CONFIG   0x17

static i2c_master_dev_handle_t tps53647_i2c_handle;
static bool tps53647_initialized = false;
static char tps53647_error_message[256] = "Power Fault Detected.";

/* ---- I2C helpers ---- */

static esp_err_t read_byte(uint8_t command, uint8_t *data)
{
    return i2c_bitaxe_register_read(tps53647_i2c_handle, command, data, 1);
}

static esp_err_t write_byte(uint8_t command, uint8_t data)
{
    return i2c_bitaxe_register_write_byte(tps53647_i2c_handle, command, data);
}

static esp_err_t write_command(uint8_t command)
{
    return i2c_bitaxe_register_write_addr(tps53647_i2c_handle, command);
}

static esp_err_t read_word(uint8_t command, uint16_t *result)
{
    uint8_t data[2];
    ESP_RETURN_ON_ERROR(i2c_bitaxe_register_read(tps53647_i2c_handle, command, data, 2),
                        TAG, "read_word failed (cmd 0x%02x)", command);
    *result = ((uint16_t)data[1] << 8) | data[0];
    return ESP_OK;
}

static esp_err_t write_word(uint8_t command, uint16_t data)
{
    return i2c_bitaxe_register_write_word(tps53647_i2c_handle, command, data);
}

/* ---- VID / SLINEAR11 conversions ---- */

static uint8_t volt_to_vid(float volts)
{
    if (volts == 0.0f) return 0x00;
    int reg = (int)((volts - TPS53647_HW_MIN_VOLTAGE) / TPS53647_VID_STEP) + 1;
    if (reg > 0xFF) {
        ESP_LOGE(TAG, "volt_to_vid: %.3fV out of range", volts);
        return 0;
    }
    return (uint8_t)reg;
}

static float slinear11_to_float(uint16_t value)
{
    int32_t exponent = value >> 11;
    int32_t mantissa = value & 0x7FF;
    exponent |= (exponent & 0x10) ? 0xFFFFFFE0 : 0;
    mantissa |= (mantissa & 0x400) ? 0xFFFFF800 : 0;
    return (float)mantissa * powf(2.0f, (float)exponent);
}

static uint16_t float_to_slinear11(float x)
{
    if (x <= 0.0f) return 0;
    int32_t e = -16, m;
    while (e <= 15) {
        m = (int32_t)roundf(x / powf(2.0f, (float)e));
        if (m >= 0 && m <= 1023) break;
        e++;
    }
    if (e > 15) return 0;
    return ((uint16_t)(e & 0x1F) << 11) | ((uint16_t)m & 0x7FF);
}

/* ---- Public API ---- */

esp_err_t TPS53647_init(TPS53647_CONFIG config)
{
    ESP_RETURN_ON_ERROR(
        i2c_bitaxe_add_device(TPS53647_I2CADDR, &tps53647_i2c_handle, TAG),
        TAG, "Failed to register TPS53647 on I2C bus");

    /* Verify device identity */
    uint16_t device_code = 0;
    read_word(TPS53647_REG_DEVICE_CODE, &device_code);
    ESP_LOGI(TAG, "Device code: 0x%04X", device_code);
    if (device_code != TPS53647_DEVICE_CODE) {
        ESP_LOGE(TAG, "TPS53647 not found (got 0x%04X, expected 0x%04X)",
                 device_code, TPS53647_DEVICE_CODE);
        return ESP_ERR_NOT_FOUND;
    }

    /* Clear latched faults and restore NVM defaults */
    write_command(PMBUS_CLEAR_FAULTS);
    write_command(PMBUS_RESTORE_DEFAULT_ALL);

    /* ON_OFF_CONFIG: use CONTROL pin, soft start/stop */
    write_byte(PMBUS_ON_OFF_CONFIG, TPS53647_ON_OFF_CONFIG);

    /* Switching frequency: 500 kHz */
    write_byte(TPS53647_REG_FREQ, 0x20);

    /* Max current (1 A/LSB) */
    write_byte(TPS53647_REG_IMAX, (uint8_t)config.imax_amps);

    /* Operation mode: VR12, dynamic phase shedding, slew rate 0.68 mV/µs */
    write_byte(TPS53647_REG_OP_MODE, 0x89);

    /* Re-apply switching frequency (mirrors reference init flow) */
    write_byte(TPS53647_REG_FREQ, 0x20);

    /* Phase count register: 0 = 1 phase, 1 = 2 phases, ... */
    if (config.num_phases < 1 || config.num_phases > 6) {
        ESP_LOGE(TAG, "num_phases out of range: %d", config.num_phases);
        return ESP_ERR_INVALID_ARG;
    }
    write_byte(TPS53647_REG_PHASE_COUNT, (uint8_t)(config.num_phases - 1));
    ESP_LOGI(TAG, "Phases: %d", config.num_phases);

    /* Over-temperature limits */
    write_word(PMBUS_OT_WARN_LIMIT,  float_to_slinear11(TPS53647_OT_WARN_LIMIT));
    write_word(PMBUS_OT_FAULT_LIMIT, float_to_slinear11(TPS53647_OT_FAULT_LIMIT));

    /* Overcurrent limits — warn = fault to match reference behaviour */
    write_word(PMBUS_IOUT_OC_WARN_LIMIT,  float_to_slinear11(config.ifault_amps));
    write_word(PMBUS_IOUT_OC_FAULT_LIMIT, float_to_slinear11(config.ifault_amps));

    tps53647_initialized = true;
    ESP_LOGI(TAG, "TPS53647 initialized: %d phases, imax=%dA, ifault=%.0fA",
             config.num_phases, config.imax_amps, config.ifault_amps);
    return ESP_OK;
}

void TPS53647_clear_faults(void)
{
    write_command(PMBUS_CLEAR_FAULTS);
}

bool TPS53647_set_vout(float volts)
{
    if (volts == 0.0f) {
        gpio_set_direction(TPS53647_EN_PIN, GPIO_MODE_OUTPUT);
        gpio_set_level(TPS53647_EN_PIN, 0);
        return true;
    }
    if (volts < TPS53647_VOUT_MIN || volts > TPS53647_VOUT_MAX) {
        ESP_LOGE(TAG, "Voltage %.3fV out of range [%.3f, %.3f]",
                 volts, TPS53647_VOUT_MIN, TPS53647_VOUT_MAX);
        return false;
    }
    gpio_set_direction(TPS53647_EN_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(TPS53647_EN_PIN, 1);
    write_word(PMBUS_VOUT_COMMAND, (uint16_t)volt_to_vid(volts));
    ESP_LOGI(TAG, "Vout → %.3fV (VID 0x%02X)", volts, volt_to_vid(volts));
    return true;
}

float TPS53647_get_vout(void)
{
    if (!tps53647_initialized) return 0.0f;
    uint16_t raw = 0;
    if (read_word(TPS53647_REG_VOUT_READBACK, &raw) != ESP_OK) return 0.0f;
    /* Linear format: value * 2^-9 */
    return (float)raw * powf(2.0f, -9.0f);
}

float TPS53647_get_vin(void)
{
    if (!tps53647_initialized) return 0.0f;
    uint16_t raw = 0;
    read_word(PMBUS_READ_VIN, &raw);
    return slinear11_to_float(raw);
}

float TPS53647_get_iout(void)
{
    if (!tps53647_initialized) return 0.0f;
    uint16_t raw = 0;
    read_word(PMBUS_READ_IOUT, &raw);
    return slinear11_to_float(raw);
}

float TPS53647_get_temperature(void)
{
    if (!tps53647_initialized) return 0.0f;
    uint16_t raw = 0;
    if (read_word(PMBUS_READ_TEMPERATURE_1, &raw) != ESP_OK) return 0.0f;
    return slinear11_to_float(raw);
}

esp_err_t TPS53647_set_iout_oc_limits(float warn_amps, float fault_amps)
{
    ESP_RETURN_ON_ERROR(write_word(PMBUS_IOUT_OC_WARN_LIMIT,  float_to_slinear11(warn_amps)),
                        TAG, "Failed to write OC warn limit");
    ESP_RETURN_ON_ERROR(write_word(PMBUS_IOUT_OC_FAULT_LIMIT, float_to_slinear11(fault_amps)),
                        TAG, "Failed to write OC fault limit");
    return ESP_OK;
}

void TPS53647_print_status(void)
{
    uint8_t  s_byte = 0, s_vout = 0, s_iout = 0, s_input = 0, s_mfr = 0;
    uint16_t s_word = 0;

    read_byte(PMBUS_STATUS_BYTE,         &s_byte);
    read_word(PMBUS_STATUS_WORD,         &s_word);
    read_byte(PMBUS_STATUS_VOUT,         &s_vout);
    read_byte(PMBUS_STATUS_IOUT,         &s_iout);
    read_byte(PMBUS_STATUS_INPUT,        &s_input);
    read_byte(PMBUS_STATUS_MFR_SPECIFIC, &s_mfr);

    /* Mask the spurious COMS error bit (bit 1) */
    s_byte &= ~0x02;
    s_word &= ~0x0002;

    bool ok = !(s_byte || s_word || s_vout || s_iout || s_input || s_mfr);
    if (ok) {
        ESP_LOGI(TAG, "status OK — byte=%02x word=%04x vout=%02x iout=%02x input=%02x mfr=%02x",
                 s_byte, s_word, s_vout, s_iout, s_input, s_mfr);
    } else {
        ESP_LOGE(TAG, "status ERR — byte=%02x word=%04x vout=%02x iout=%02x input=%02x mfr=%02x",
                 s_byte, s_word, s_vout, s_iout, s_input, s_mfr);
    }
}

esp_err_t TPS53647_check_status(GlobalState *GLOBAL_STATE)
{
    SystemModule *SYSTEM_MODULE = &GLOBAL_STATE->SYSTEM_MODULE;
    uint16_t status = 0;

    ESP_RETURN_ON_ERROR(read_word(PMBUS_STATUS_WORD, &status),
                        TAG, "Failed to read STATUS_WORD");

    if (status & (TPS53647_STATUS_OFF | TPS53647_STATUS_VOUT_OV |
                  TPS53647_STATUS_IOUT_OC | TPS53647_STATUS_VIN_UV | TPS53647_STATUS_TEMP)) {
        if (SYSTEM_MODULE->power_fault == 0) {
            ESP_LOGE(TAG, "Power fault: STATUS_WORD=0x%04X", status);
            snprintf(tps53647_error_message, sizeof(tps53647_error_message),
                     "TPS53647 fault (0x%04X):%s%s%s%s%s", status,
                     (status & TPS53647_STATUS_OFF)     ? " OFF"     : "",
                     (status & TPS53647_STATUS_VOUT_OV) ? " VOUT_OV" : "",
                     (status & TPS53647_STATUS_IOUT_OC) ? " IOUT_OC" : "",
                     (status & TPS53647_STATUS_VIN_UV)  ? " VIN_UV"  : "",
                     (status & TPS53647_STATUS_TEMP)    ? " TEMP"    : "");
            SYSTEM_MODULE->power_fault = 1;
        }
    } else {
        SYSTEM_MODULE->power_fault = 0;
    }
    return ESP_OK;
}

const char *TPS53647_get_error_message(void)
{
    return tps53647_error_message;
}
