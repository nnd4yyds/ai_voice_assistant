#ifndef WAKEUP_H
#define WAKEUP_H

#include "esp_afe_sr_models.h"
#include "driver/i2s_std.h"
#include <stdbool.h>

#define DETECT_MODE DET_MODE_90

esp_afe_sr_iface_t *wakeup_detection_init(void);
esp_afe_sr_data_t *wakeup_detection_create(esp_afe_sr_iface_t *afe_handle);
void task_wakeup_detection(void *pvParameters);
bool is_wakeup_detected(void);
void reset_wakeup_flag(void);

#endif // WAKEUP_H
