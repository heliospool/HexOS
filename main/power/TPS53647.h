#ifndef TPS53647_H_
#define TPS53647_H_

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "pmbus_commands.h"
#include "global_state.h"

#define TPS53647_I2CADDR      0x71    ///< Default I2C address
#define TPS53647_DEVICE_CODE  0x01F0  ///< Expected MFR_SPECIFIC_44 value

/* TPS53647 manufacturer-specific registers — mirrors PMBUS_TPS53647_* in pmbus_commands.h */
#define TPS53647_REG_VOUT_READBACK  PMBUS_TPS53647_VOUT_READBACK
#define TPS53647_REG_IMAX           PMBUS_TPS53647_IMAX
#define TPS53647_REG_FREQ           PMBUS_TPS53647_FREQ
#define TPS53647_REG_OP_MODE        PMBUS_TPS53647_OP_MODE
#define TPS53647_REG_PHASE_COUNT    PMBUS_TPS53647_PHASE_COUNT
#define TPS53647_REG_DEVICE_CODE    PMBUS_TPS53647_DEVICE_CODE

/* STATUS_WORD bits (standard PMBus layout) */
#define TPS53647_STATUS_VOUT    0x8000
#define TPS53647_STATUS_IOUT    0x4000
#define TPS53647_STATUS_INPUT   0x2000
#define TPS53647_STATUS_BUSY    0x0080
#define TPS53647_STATUS_OFF     0x0040
#define TPS53647_STATUS_VOUT_OV 0x0020
#define TPS53647_STATUS_IOUT_OC 0x0010
#define TPS53647_STATUS_VIN_UV  0x0008
#define TPS53647_STATUS_TEMP    0x0004

typedef struct {
    int   num_phases;   ///< Active phase count (1–6)
    int   imax_amps;    ///< Max current limit passed to REG_IMAX (1 A/LSB)
    float ifault_amps;  ///< IOUT overcurrent fault threshold
} TPS53647_CONFIG;

esp_err_t     TPS53647_init(TPS53647_CONFIG config);
void          TPS53647_clear_faults(void);
bool          TPS53647_set_vout(float volts);
float         TPS53647_get_vout(void);
float         TPS53647_get_vin(void);
float         TPS53647_get_iout(void);
float         TPS53647_get_temperature(void);
esp_err_t     TPS53647_set_iout_oc_limits(float warn_amps, float fault_amps);
void          TPS53647_print_status(void);
esp_err_t     TPS53647_check_status(GlobalState *GLOBAL_STATE);
const char   *TPS53647_get_error_message(void);

#endif /* TPS53647_H_ */
