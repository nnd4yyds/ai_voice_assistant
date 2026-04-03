#ifndef LLM_VOICE_H
#define LLM_VOICE_H

#include "esp_err.h"
#include <stdbool.h>

typedef enum {
    LLM_VOICE_STATE_IDLE,
    LLM_VOICE_STATE_CONNECTING,
    LLM_VOICE_STATE_CONNECTED,
    LLM_VOICE_STATE_ERROR
} llm_voice_state_t;

typedef void (*llm_voice_text_cb_t)(const char *text);
typedef void (*llm_voice_audio_cb_t)(const char *audio_data, int audio_len);
typedef void (*llm_voice_state_cb_t)(llm_voice_state_t state);

typedef struct {
    llm_voice_text_cb_t on_text;
    llm_voice_audio_cb_t on_audio;
    llm_voice_state_cb_t on_state;
} llm_voice_callbacks_t;

esp_err_t llm_voice_init(const char *access_token, const char *model);
esp_err_t llm_voice_set_callbacks(llm_voice_callbacks_t *cbs);
esp_err_t llm_voice_start(void);
esp_err_t llm_voice_stop(void);
esp_err_t llm_voice_send_audio(const char *audio_data, int audio_len);
esp_err_t llm_voice_commit_audio(void);
llm_voice_state_t llm_voice_get_state(void);
bool llm_voice_is_session_ready(void);

#endif // LLM_VOICE_H
