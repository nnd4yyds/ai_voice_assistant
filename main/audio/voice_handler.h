#ifndef VOICE_HANDLER_H
#define VOICE_HANDLER_H

#include "driver/i2s_std.h"

int voice_handler_init(i2s_chan_handle_t rx, i2s_chan_handle_t tx);
void voice_handler_process(void);

#endif