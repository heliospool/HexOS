#ifndef HASHRATE_MONITOR_TASK_H_
#define HASHRATE_MONITOR_TASK_H_

#include "global_state.h"

void hashrate_monitor_task(void *pvParameters);
void HASHRATE_update_diff_averages(SystemModule *sys_module, float diff);
void hashrate_monitor_register_read(void *pvParameters, register_type_t register_type, uint8_t asic_nr, uint32_t value);
void update_hashrate(measurement_t * measurement, uint32_t value);
void update_hash_counter(measurement_t * measurement, uint32_t value, uint64_t time_us);
#endif /* HASHRATE_MONITOR_TASK_H_ */
