#ifndef APP_TASKS_H
#define APP_TASKS_H

#include "driver/i2s_std.h"

void task_speech_recognition(void *pvParameters);
void task_speech_synthesis(void *pvParameters);
void task_main_loop(void *pvParameters);
void task_voice_assistant(void *pvParameters);
void set_audio_buffer(char *buffer);
void set_i2s_handles(i2s_chan_handle_t rx, i2s_chan_handle_t tx);

#endif
