#ifndef WAKEUP_H
#define WAKEUP_H

#include "esp_afe_sr_models.h"
#include "driver/i2s_std.h"
#include <stdbool.h>

esp_afe_sr_iface_t *wakeup_detection_init(void);
esp_afe_sr_data_t *wakeup_detection_create(esp_afe_sr_iface_t *afe_handle, i2s_chan_handle_t rx, i2s_chan_handle_t tx);
void task_wakeup_detection(void *pvParameters);
bool is_wakeup_detected(void);
void reset_wakeup_flag(void);
void wakeup_set_handles(i2s_chan_handle_t rx, i2s_chan_handle_t tx);

extern volatile bool wakeup_detected;

#endif