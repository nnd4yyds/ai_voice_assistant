#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_event.h"
#include "esp_system.h"
#include "esp_random.h"
#include "llm_voice.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/x509.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "llm_voice";

#define WEBSOCKET_URI "aip.baidubce.com"
#define WEBSOCKET_PATH "/ws/2.0/speech/v1/realtime"
#define DEFAULT_MODEL "audio-mini-realtime-near"
#define WEBSOCKET_PORT 443

#define RECV_BUFFER_SIZE 4096
#define SEND_BUFFER_SIZE 4096

typedef struct {
    int socket;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
} websocket_ssl_t;

static websocket_ssl_t s_ws = {0};
static llm_voice_state_t s_state = LLM_VOICE_STATE_IDLE;
static llm_voice_callbacks_t s_cbs = {0};
static char s_access_token[512] = {0};
static char s_model[64] = {0};
static bool s_session_started = false;
static bool s_session_configured = false;
static bool s_connected = false;

static void send_session_update(void);

static char s_recv_buffer[RECV_BUFFER_SIZE];
static int s_recv_len = 0;

static const char *SESSION_UPDATE_FMT =
    "{\"type\": \"session.update\", \"session\": {"
    "\"input_audio_transcription\": {\"model\": \"default\"},"
    "\"turn_detection\": {\"type\": \"server_vad\", \"threshold\": 0.5, \"prefix_padding_ms\": 300, \"silence_duration_ms\": 200, \"interrupt_response\": true},"
    "\"voice\": \"default\","
    "\"modalities\": [\"text\", \"audio\"],"
    "\"output_audio_format\": \"pcm16\""
    "}}";

static void llm_voice_set_state(llm_voice_state_t state)
{
    s_state = state;
    if (s_cbs.on_state) {
        s_cbs.on_state(state);
    }
}

static int base64_encode(const uint8_t *src, int src_len, char *dst, int dst_len)
{
    static const char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    
    if (src_len <= 0 || !src || !dst || dst_len < ((src_len + 2) / 3) * 4 + 1) {
        return -1;
    }

    int i, j;
    for (i = 0, j = 0; i < src_len; i += 3) {
        int a = src[i];
        int b = (i + 1 < src_len) ? src[i + 1] : 0;
        int c = (i + 2 < src_len) ? src[i + 2] : 0;

        dst[j++] = base64_table[(a >> 2) & 0x3F];
        dst[j++] = base64_table[((a << 4) | (b >> 4)) & 0x3F];
        dst[j++] = (i + 1 < src_len) ? base64_table[((b << 2) | (c >> 6)) & 0x3F] : '=';
        dst[j++] = (i + 2 < src_len) ? base64_table[c & 0x3F] : '=';
    }
    dst[j] = '\0';
    return j;
}

static int base64_decode(const char *src, int src_len, uint8_t *dst, int dst_len)
{
    static const int8_t base64_decode_table[256] = {
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63,
        52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1,
        -1,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
        15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1,
        -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
        41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
    };

    if (!src || src_len <= 0 || !dst || dst_len < ((src_len + 3) / 4) * 3) {
        return -1;
    }

    int i, j;
    int padding = 0;
    
    if (src_len >= 1 && src[src_len - 1] == '=') padding++;
    if (src_len >= 2 && src[src_len - 2] == '=') padding++;

    for (i = 0, j = 0; i < src_len; i += 4) {
        int8_t c0 = (i < src_len) ? base64_decode_table[(unsigned char)src[i]] : -1;
        int8_t c1 = (i + 1 < src_len) ? base64_decode_table[(unsigned char)src[i + 1]] : -1;
        int8_t c2 = (i + 2 < src_len) ? base64_decode_table[(unsigned char)src[i + 2]] : -1;
        int8_t c3 = (i + 3 < src_len) ? base64_decode_table[(unsigned char)src[i + 3]] : -1;

        if (c0 < 0 || c1 < 0 || (i + 2 < src_len && c2 < 0) || (i + 3 < src_len && c3 < 0)) {
            return -1;
        }

        dst[j++] = (c0 << 2) | (c1 >> 4);
        if (j < dst_len && c2 >= 0) {
            dst[j++] = ((c1 & 0x0F) << 4) | (c2 >> 2);
        }
        if (j < dst_len && c3 >= 0) {
            dst[j++] = ((c2 & 0x03) << 6) | c3;
        }
    }

    return j - padding;
}

static int websocket_create_masked_frame(const char *data, int len, char *buf, int buf_size, int opcode)
{
    if (buf_size < len + 14) {
        return -1;
    }

    buf[0] = 0x80 | opcode;
    
    int mask_bit = 0x80;
    int payload_len = len;
    int header_len = 2;
    
    if (payload_len < 126) {
        buf[1] = mask_bit | payload_len;
    } else if (payload_len < 65536) {
        buf[1] = mask_bit | 126;
        buf[2] = (payload_len >> 8) & 0xFF;
        buf[3] = payload_len & 0xFF;
        header_len = 4;
    } else {
        buf[1] = mask_bit | 127;
        buf[2] = 0;
        buf[3] = 0;
        buf[4] = 0;
        buf[5] = 0;
        buf[6] = (payload_len >> 24) & 0xFF;
        buf[7] = (payload_len >> 16) & 0xFF;
        buf[8] = (payload_len >> 8) & 0xFF;
        buf[9] = payload_len & 0xFF;
        header_len = 10;
    }

    char mask[4];
    mask[0] = (char)(esp_random() & 0xFF);
    mask[1] = (char)(esp_random() & 0xFF);
    mask[2] = (char)(esp_random() & 0xFF);
    mask[3] = (char)(esp_random() & 0xFF);

    memcpy(&buf[header_len], mask, 4);
    header_len += 4;

    for (int i = 0; i < len; i++) {
        buf[header_len + i] = data[i] ^ mask[i % 4];
    }

    return header_len + len;
}

static int websocket_create_text_frame(const char *data, int len, char *buf, int buf_size)
{
    return websocket_create_masked_frame(data, len, buf, buf_size, 0x01);
}

static int ssl_send_all(mbedtls_ssl_context *ssl, const char *data, int len)
{
    int total_sent = 0;
    while (total_sent < len) {
        int sent = mbedtls_ssl_write(ssl, (const unsigned char *)data + total_sent, len - total_sent);
        if (sent < 0) {
            if (sent == MBEDTLS_ERR_SSL_WANT_WRITE || sent == MBEDTLS_ERR_SSL_WANT_READ) {
                vTaskDelay(1);
                continue;
            }
            return -1;
        }
        total_sent += sent;
    }
    return total_sent;
}

static int ssl_recv_all(mbedtls_ssl_context *ssl, char *data, int len, int timeout_ms)
{
    int total_recv = 0;
    int deadline = timeout_ms / 10;
    int attempts = 0;
    
    while (total_recv < len && attempts < deadline) {
        int recv = mbedtls_ssl_read(ssl, (unsigned char *)data + total_recv, len - total_recv);
        if (recv < 0) {
            if (recv == MBEDTLS_ERR_SSL_WANT_READ || recv == MBEDTLS_ERR_SSL_WANT_WRITE) {
                vTaskDelay(10 / portTICK_PERIOD_MS);
                attempts++;
                continue;
            }
            ESP_LOGE(TAG, "SSL read error: -0x%04x", -recv);
            return -1;
        } else if (recv == 0) {
            ESP_LOGW(TAG, "Connection closed by peer (received 0 bytes)");
            break;
        }
        total_recv += recv;
        attempts = 0;
    }
    
    if (total_recv == 0 && attempts >= deadline) {
        ESP_LOGW(TAG, "Receive timeout after %d ms", timeout_ms);
    }
    
    return total_recv;
}

static bool websocket_handshake(const char *host, const char *path, const char *token, const char *model)
{
    char key_nonce[16];
    for (int i = 0; i < 16; i++) {
        key_nonce[i] = (char)(esp_random() & 0xFF);
    }
    char key_b64[32];
    base64_encode((uint8_t *)key_nonce, 16, key_b64, sizeof(key_b64));

    char request[1024];
    int req_len = snprintf(request, sizeof(request),
        "GET %s?model=%s&token=%s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Sec-WebSocket-Protocol: speech.v1\r\n"
        "User-Agent: ESP32\r\n"
        "\r\n",
        path, model, token, host, key_b64);

    if (ssl_send_all(&s_ws.ssl, request, req_len) < 0) {
        ESP_LOGE(TAG, "Failed to send WebSocket handshake");
        return false;
    }
    ESP_LOGI(TAG, "Sent handshake request (%d bytes): %s", req_len, request);

    char response[2048];
    int recv_len = ssl_recv_all(&s_ws.ssl, response, sizeof(response) - 1, 30000);
    if (recv_len <= 0) {
        ESP_LOGE(TAG, "Failed to receive WebSocket handshake response, recv_len=%d", recv_len);
        return false;
    }
    response[recv_len] = '\0';
    ESP_LOGI(TAG, "Received response (%d bytes): %s", recv_len, response);
    
    if (strstr(response, "HTTP/1.1 400") || strstr(response, "HTTP/1.1 401") || 
        strstr(response, "HTTP/1.1 403") || strstr(response, "HTTP/1.1 404")) {
        ESP_LOGE(TAG, "Server returned error response");
        char *body = strstr(response, "\r\n\r\n");
        if (body) {
            ESP_LOGE(TAG, "Error body: %s", body + 4);
        }
        return false;
    }

    if (strstr(response, "101 Switching Protocols") == NULL) {
        ESP_LOGE(TAG, "WebSocket handshake failed - no 101 response");
        return false;
    }

    ESP_LOGI(TAG, "WebSocket handshake successful");
    return true;
}

static bool parse_websocket_frame(const char *data, int len, char *payload, int *payload_len, int *opcode)
{
    if (len < 2) {
        return false;
    }

    *opcode = data[0] & 0x0F;
    int payload_len_field = data[1] & 0x7F;
    int header_len = 2;
    int mask_bit = data[1] & 0x80;

    if (payload_len_field == 126) {
        if (len < 4) return false;
        payload_len_field = (data[2] << 8) | data[3];
        header_len = 4;
    } else if (payload_len_field == 127) {
        if (len < 10) return false;
        payload_len_field = 0;
        for (int i = 0; i < 8; i++) {
            payload_len_field = (payload_len_field << 8) | ((unsigned char)data[2 + i]);
        }
        header_len = 10;
    }

    if (mask_bit) {
        header_len += 4;
    }

    if (len < header_len + payload_len_field) {
        return false;
    }

    *payload_len = payload_len_field;
    if (payload && payload_len_field > 0) {
        memcpy(payload, data + header_len, payload_len_field);
    }

    return true;
}

static void parse_server_message(const char *json_str, int json_len)
{
    char *json = (char *)malloc(json_len + 1);
    if (!json) return;
    memcpy(json, json_str, json_len);
    json[json_len] = '\0';

    ESP_LOGI(TAG, "Server message: %s", json);

    if (strstr(json, "\"type\":\"session.created\"")) {
        s_session_started = true;
        ESP_LOGI(TAG, "Session created!");
    } else if (strstr(json, "\"type\":\"session.updated\"")) {
        s_session_configured = true;
        ESP_LOGI(TAG, "Session configured!");
    } else if (strstr(json, "\"type\":\"response.audio_transcript.delta\"")) {
        char *delta_start = strstr(json, "\"delta\":\"");
        if (delta_start) {
            delta_start += 8;
            char *delta_end = strstr(delta_start, "\"");
            if (delta_end) {
                int delta_len = delta_end - delta_start;
                char *transcript = (char *)malloc(delta_len + 1);
                if (transcript) {
                    memcpy(transcript, delta_start, delta_len);
                    transcript[delta_len] = '\0';
                    ESP_LOGI(TAG, "Transcript delta: %s", transcript);
                    if (s_cbs.on_text) {
                        s_cbs.on_text(transcript);
                    }
                    free(transcript);
                }
            }
        }
    } else if (strstr(json, "\"type\":\"response.audio_transcript.done\"")) {
        char *transcript_start = strstr(json, "\"transcript\":\"");
        if (transcript_start) {
            transcript_start += 13;
            char *transcript_end = strstr(transcript_start, "\"");
            if (transcript_end) {
                int len = transcript_end - transcript_start;
                char *transcript = (char *)malloc(len + 1);
                if (transcript) {
                    memcpy(transcript, transcript_start, len);
                    transcript[len] = '\0';
                    ESP_LOGI(TAG, "Final transcript: %s", transcript);
                    if (s_cbs.on_text) {
                        s_cbs.on_text(transcript);
                    }
                    free(transcript);
                }
            }
        }
    } else if (strstr(json, "\"type\":\"response.audio.delta\"")) {
        char *delta_start = strstr(json, "\"delta\":\"");
        if (delta_start) {
            delta_start += 8;
            char *delta_end = strstr(delta_start, "\"");
            if (delta_end) {
                int delta_len = delta_end - delta_start;
                uint8_t *audio_buf = (uint8_t *)malloc(delta_len);
                if (audio_buf) {
                    int audio_len = base64_decode(delta_start, delta_len, audio_buf, delta_len);
                    if (audio_len > 0 && s_cbs.on_audio) {
                        s_cbs.on_audio((const char *)audio_buf, audio_len);
                    }
                    free(audio_buf);
                }
            }
        }
    }

    free(json);
}

static void websocket_task(void *pvParameters)
{
    char recv_buf[RECV_BUFFER_SIZE];

    while (s_connected) {
        int recv = ssl_recv_all(&s_ws.ssl, recv_buf, sizeof(recv_buf) - 1, 100);
        if (recv > 0) {
            recv_buf[recv] = '\0';
            
            if (s_recv_len + recv >= RECV_BUFFER_SIZE) {
                s_recv_len = 0;
            }
            memcpy(s_recv_buffer + s_recv_len, recv_buf, recv);
            s_recv_len += recv;

            char payload[8192];
            int payload_len;
            int opcode;

            while (parse_websocket_frame(s_recv_buffer, s_recv_len, payload, &payload_len, &opcode)) {
                int frame_len = 2;
                int mask_bit = s_recv_buffer[1] & 0x80;
                int payload_len_field = s_recv_buffer[1] & 0x7F;
                if (payload_len_field == 126) frame_len = 4;
                else if (payload_len_field == 127) frame_len = 10;
                if (mask_bit) frame_len += 4;
                frame_len += payload_len;

                if (opcode == 1) {
                    parse_server_message(payload, payload_len);
                } else if (opcode == 2) {
                    if (s_cbs.on_audio && payload_len > 0) {
                        s_cbs.on_audio(payload, payload_len);
                    }
                } else if (opcode == 8) {
                    ESP_LOGI(TAG, "WebSocket close frame received");
                    s_connected = false;
                    break;
                }

                memmove(s_recv_buffer, s_recv_buffer + frame_len, s_recv_len - frame_len);
                s_recv_len -= frame_len;
                if (s_recv_len < 0) s_recv_len = 0;
            }
        } else if (recv < 0) {
            ESP_LOGE(TAG, "Connection error");
            s_connected = false;
            break;
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    llm_voice_set_state(LLM_VOICE_STATE_IDLE);
    s_session_started = false;
    s_session_configured = false;
    vTaskDelete(NULL);
}

static bool init_ssl(void)
{
    int ret;

    mbedtls_entropy_init(&s_ws.entropy);
    mbedtls_ctr_drbg_init(&s_ws.ctr_drbg);
    mbedtls_ssl_init(&s_ws.ssl);
    mbedtls_ssl_config_init(&s_ws.conf);

    ret = mbedtls_ctr_drbg_seed(&s_ws.ctr_drbg, mbedtls_entropy_func, &s_ws.entropy, NULL, 0);
    if (ret != 0) {
        ESP_LOGE(TAG, "mbedtls_ctr_drbg_seed failed: -0x%04x", -ret);
        return false;
    }

    ret = mbedtls_ssl_config_defaults(&s_ws.conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) {
        ESP_LOGE(TAG, "mbedtls_ssl_config_defaults failed: -0x%04x", -ret);
        return false;
    }

    mbedtls_ssl_conf_rng(&s_ws.conf, mbedtls_ctr_drbg_random, &s_ws.ctr_drbg);
    mbedtls_ssl_conf_authmode(&s_ws.conf, MBEDTLS_SSL_VERIFY_NONE);

    ret = mbedtls_ssl_setup(&s_ws.ssl, &s_ws.conf);
    if (ret != 0) {
        ESP_LOGE(TAG, "mbedtls_ssl_setup failed: -0x%04x", -ret);
        return false;
    }

    return true;
}

static void free_ssl(void)
{
    mbedtls_ssl_free(&s_ws.ssl);
    mbedtls_ssl_config_free(&s_ws.conf);
    mbedtls_ctr_drbg_free(&s_ws.ctr_drbg);
    mbedtls_entropy_free(&s_ws.entropy);
}

esp_err_t llm_voice_init(const char *access_token, const char *model)
{
    if (!access_token) {
        return ESP_ERR_INVALID_ARG;
    }
    
    strncpy(s_access_token, access_token, sizeof(s_access_token) - 1);
    s_access_token[sizeof(s_access_token) - 1] = '\0';
    
    if (model && model[0]) {
        strncpy(s_model, model, sizeof(s_model) - 1);
    } else {
        strncpy(s_model, DEFAULT_MODEL, sizeof(s_model) - 1);
    }
    s_model[sizeof(s_model) - 1] = '\0';
    
    return ESP_OK;
}

esp_err_t llm_voice_set_callbacks(llm_voice_callbacks_t *cbs)
{
    if (!cbs) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(&s_cbs, cbs, sizeof(llm_voice_callbacks_t));
    return ESP_OK;
}

static int socket_send(void *ctx, const unsigned char *buf, size_t len)
{
    int sock = *(int *)ctx;
    return lwip_send(sock, buf, len, 0);
}

static int socket_recv(void *ctx, unsigned char *buf, size_t len)
{
    int sock = *(int *)ctx;
    return lwip_recv(sock, buf, len, 0);
}

esp_err_t llm_voice_start(void)
{
    if (s_connected) {
        ESP_LOGW(TAG, "WebSocket already started");
        return ESP_OK;
    }

    llm_voice_set_state(LLM_VOICE_STATE_CONNECTING);

    if (!init_ssl()) {
        ESP_LOGE(TAG, "Failed to init SSL");
        llm_voice_set_state(LLM_VOICE_STATE_ERROR);
        return ESP_FAIL;
    }

    struct hostent *host = gethostbyname(WEBSOCKET_URI);
    if (!host) {
        ESP_LOGE(TAG, "Failed to resolve host: %s", WEBSOCKET_URI);
        free_ssl();
        llm_voice_set_state(LLM_VOICE_STATE_ERROR);
        return ESP_FAIL;
    }

    struct sockaddr_in dest_addr;
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(WEBSOCKET_PORT);
    dest_addr.sin_addr.s_addr = ((struct in_addr *)host->h_addr_list[0])->s_addr;

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        ESP_LOGE(TAG, "Failed to create socket");
        free_ssl();
        llm_voice_set_state(LLM_VOICE_STATE_ERROR);
        return ESP_FAIL;
    }

    if (connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) != 0) {
        ESP_LOGE(TAG, "Socket connect failed");
        close(sock);
        free_ssl();
        llm_voice_set_state(LLM_VOICE_STATE_ERROR);
        return ESP_FAIL;
    }

    mbedtls_ssl_set_bio(&s_ws.ssl, &sock, socket_send, socket_recv, NULL);

    ESP_LOGI(TAG, "Performing TLS handshake...");
    int ret = mbedtls_ssl_handshake(&s_ws.ssl);
    if (ret != 0) {
        ESP_LOGE(TAG, "TLS handshake failed: -0x%04x", -ret);
        close(sock);
        free_ssl();
        llm_voice_set_state(LLM_VOICE_STATE_ERROR);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "TLS handshake successful");

    if (!websocket_handshake(WEBSOCKET_URI, WEBSOCKET_PATH, s_access_token, s_model)) {
        close(sock);
        free_ssl();
        llm_voice_set_state(LLM_VOICE_STATE_ERROR);
        return ESP_FAIL;
    }

    s_ws.socket = sock;
    s_connected = true;
    llm_voice_set_state(LLM_VOICE_STATE_CONNECTED);

    xTaskCreate(websocket_task, "websocket_task", 8192, NULL, 5, NULL);

    vTaskDelay(100 / portTICK_PERIOD_MS);
    send_session_update();

    return ESP_OK;
}

esp_err_t llm_voice_stop(void)
{
    if (!s_connected && s_ws.socket < 0) {
        return ESP_OK;
    }

    s_connected = false;

    if (s_ws.socket >= 0) {
        char close_frame[6] = {0x88, 0x80, 0x00, 0x00, 0x00, 0x00};
        ssl_send_all(&s_ws.ssl, close_frame, 6);
        close(s_ws.socket);
        s_ws.socket = -1;
    }

    free_ssl();
    s_session_started = false;
    s_session_configured = false;
    llm_voice_set_state(LLM_VOICE_STATE_IDLE);

    return ESP_OK;
}

static void send_session_update(void)
{
    if (!s_connected) return;
    
    char frame[SEND_BUFFER_SIZE];
    int frame_len = websocket_create_text_frame(SESSION_UPDATE_FMT, strlen(SESSION_UPDATE_FMT), frame, sizeof(frame));
    if (frame_len > 0) {
        if (ssl_send_all(&s_ws.ssl, frame, frame_len) > 0) {
            ESP_LOGI(TAG, "Sent session.update (%d bytes)", frame_len);
        }
    }
}

esp_err_t llm_voice_send_audio(const char *audio_data, int audio_len)
{
    if (!s_connected) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!audio_data || audio_len <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    int b64_len = ((audio_len + 2) / 3) * 4 + 1;
    char *b64_audio = (char *)malloc(b64_len);
    if (!b64_audio) {
        return ESP_ERR_NO_MEM;
    }

    int encoded_len = base64_encode((const uint8_t *)audio_data, audio_len, b64_audio, b64_len);
    if (encoded_len < 0) {
        free(b64_audio);
        return ESP_FAIL;
    }

    int json_len = encoded_len + 64;
    char *json_str = (char *)malloc(json_len);
    if (!json_str) {
        free(b64_audio);
        return ESP_ERR_NO_MEM;
    }

    int written = snprintf(json_str, json_len, 
        "{\"type\": \"input_audio_buffer.append\", \"audio\": \"%s\"}", b64_audio);
    
    free(b64_audio);

    if (written >= json_len) {
        free(json_str);
        return ESP_ERR_NO_MEM;
    }

    char frame[SEND_BUFFER_SIZE];
    int frame_len = websocket_create_text_frame(json_str, written, frame, sizeof(frame));
    free(json_str);

    if (frame_len < 0) {
        return ESP_FAIL;
    }

    if (ssl_send_all(&s_ws.ssl, frame, frame_len) < 0) {
        ESP_LOGE(TAG, "Failed to send audio");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t llm_voice_commit_audio(void)
{
    if (!s_connected) {
        return ESP_ERR_INVALID_STATE;
    }

    const char *commit_msg = "{\"type\": \"input_audio_buffer.commit\"}";
    char frame[SEND_BUFFER_SIZE];
    int frame_len = websocket_create_text_frame(commit_msg, strlen(commit_msg), frame, sizeof(frame));
    if (frame_len < 0) {
        return ESP_FAIL;
    }

    if (ssl_send_all(&s_ws.ssl, frame, frame_len) < 0) {
        ESP_LOGE(TAG, "Failed to commit audio");
        return ESP_FAIL;
    }

    return ESP_OK;
}

llm_voice_state_t llm_voice_get_state(void)
{
    return s_state;
}

bool llm_voice_is_session_ready(void)
{
    return s_session_started && s_session_configured;
}