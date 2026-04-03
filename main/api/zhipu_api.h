#ifndef ZHIPU_API_H
#define ZHIPU_API_H

#include "esp_err.h"

esp_err_t zhipu_api_init(void);
char* zhipu_chat(const char *message, char *response, int max_len);
void zhipu_clear_history(void);

#endif