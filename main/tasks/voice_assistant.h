#ifndef VOICE_ASSISTANT_H
#define VOICE_ASSISTANT_H

#include "driver/i2s_std.h"

void task_voice_assistant(void *pvParameters);
void voice_assistant_set_buffers(char *buf, i2s_chan_handle_t rx, i2s_chan_handle_t tx);

#endif