#ifndef ERNIE_API_H
#define ERNIE_API_H

#include "esp_err.h"
#include <stdbool.h>

esp_err_t ernie_api_init(void);
esp_err_t qianfan_get_access_token(void);
const char *qianfan_get_access_token_str(void);
char* ernie_chat(const char *user_message, char *response_buffer, int buffer_size);
void ernie_clear_history(void);

#endif // ERNIE_API_H