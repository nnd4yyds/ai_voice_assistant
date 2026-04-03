#ifndef APP_TASKS_H
#define APP_TASKS_H

#include "driver/i2s_std.h"

void task_speech_recognition(void *pvParameters);
void task_speech_synthesis(void *pvParameters);
void task_main_loop(void *pvParameters);

#endif // APP_TASKS_H
