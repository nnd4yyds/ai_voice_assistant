#ifndef BAIDU_API_H
#define BAIDU_API_H

#include "esp_err.h"

esp_err_t baidu_get_access_token(void);
char *baidu_speech_to_text(const char *audio_data, int audio_len);
esp_err_t baidu_text_to_speech(const char *text, char **audio_data, int *audio_len);

#endif // BAIDU_API_H
