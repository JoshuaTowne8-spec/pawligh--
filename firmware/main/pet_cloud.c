#include "pet_cloud.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "freertos/task.h"
#include "pet_audio.h"
#include "pet_leds.h"
#include "pet_pins.h"

static const char *TAG = "pet_cloud";
static esp_websocket_client_handle_t s_client;
static StreamBufferHandle_t s_audio_stream;
static volatile bool s_connected;
static volatile bool s_flush_audio;
static volatile bool s_gateway_ready;
static volatile bool s_always_on;
static char s_text_message[1024];
static uint8_t s_audio_batch[PET_AUDIO_BATCH_BYTES];

void pet_cloud_notify_touch(bool fast)
{
    if (!s_connected || !s_client || !esp_websocket_client_is_connected(s_client)) {
        return;
    }
    const char *message = fast
        ? "{\"type\":\"touch\",\"kind\":\"quick\"}"
        : "{\"type\":\"touch\",\"kind\":\"slow\"}";
    int sent = esp_websocket_client_send_text(
        s_client,
        message,
        strlen(message),
        pdMS_TO_TICKS(250)
    );
    if (sent < 0) {
        ESP_LOGW(TAG, "Unable to report touch to gateway");
    }
}

static void update_audio_streaming(void)
{
    pet_audio_set_streaming(s_connected && s_gateway_ready && !s_always_on);
}

static void send_status(void)
{
    if (!s_connected || !esp_websocket_client_is_connected(s_client)) {
        return;
    }
    const char *status = s_always_on
        ? "{\"type\":\"status\",\"always_on\":true}"
        : "{\"type\":\"status\",\"always_on\":false}";
    esp_websocket_client_send_text(s_client, status, strlen(status), pdMS_TO_TICKS(1000));
}

static void process_json(const char *text, size_t length)
{
    cJSON *root = cJSON_ParseWithLength(text, length);
    if (!root) {
        ESP_LOGW(TAG, "Ignored invalid gateway JSON");
        return;
    }

    const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (cJSON_IsString(type)) {
        if (strcmp(type->valuestring, "ready") == 0) {
            s_gateway_ready = true;
            update_audio_streaming();
        } else if (strcmp(type->valuestring, "audio.pause") == 0) {
            s_gateway_ready = false;
            update_audio_streaming();
        } else if (strcmp(type->valuestring, "mode") == 0) {
            const cJSON *always_on = cJSON_GetObjectItemCaseSensitive(root, "always_on");
            s_always_on = cJSON_IsTrue(always_on);
            if (s_always_on) {
                s_gateway_ready = false;
            }
            s_flush_audio = true;
            pet_leds_set_always_on(s_always_on);
            update_audio_streaming();
            send_status();
        } else if (strcmp(type->valuestring, "emotion") == 0) {
            const cJSON *emotion = cJSON_GetObjectItemCaseSensitive(root, "emotion");
            if (cJSON_IsString(emotion)) {
                if (pet_leds_set_emotion(emotion->valuestring)) {
                    ESP_LOGI(TAG, "VOICE emotion=%s", emotion->valuestring);
                }
            }
        } else if (strcmp(type->valuestring, "effect") == 0) {
            const cJSON *emotion = cJSON_GetObjectItemCaseSensitive(root, "emotion");
            const cJSON *hold_ms = cJSON_GetObjectItemCaseSensitive(root, "hold_ms");
            if (cJSON_IsString(emotion)) {
                uint32_t duration = cJSON_IsNumber(hold_ms) ? (uint32_t)hold_ms->valuedouble : 12000;
                pet_leds_show_event(emotion->valuestring, duration);
                const cJSON *source = cJSON_GetObjectItemCaseSensitive(root, "source");
                if (cJSON_IsString(source) && strcmp(source->valuestring, "memory") == 0 && !s_always_on) {
                    ESP_LOGI(TAG, "VOICE emotion=%s", emotion->valuestring);
                }
            }
        } else if (strstr(type->valuestring, "error") != NULL) {
            const cJSON *message = cJSON_GetObjectItemCaseSensitive(root, "message");
            ESP_LOGE(TAG, "Gateway error: %s", cJSON_IsString(message) ? message->valuestring : "unknown");
        }
    }
    cJSON_Delete(root);
}

static void websocket_event(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    const esp_websocket_event_data_t *event = event_data;

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        s_connected = true;
        s_gateway_ready = false;
        ESP_LOGI(TAG, "Connected to emotion gateway");
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        s_connected = false;
        s_gateway_ready = false;
        s_flush_audio = true;
        update_audio_streaming();
        ESP_LOGW(TAG, "Gateway disconnected; client will reconnect");
        break;
    case WEBSOCKET_EVENT_DATA:
        if (event->op_code != 0x1 && event->op_code != 0x0) {
            break;
        }
        if (event->payload_len <= 0 || event->payload_len >= sizeof(s_text_message)) {
            ESP_LOGW(TAG, "Unexpected text payload length: %d", event->payload_len);
            break;
        }
        if (event->payload_offset + event->data_len <= event->payload_len) {
            memcpy(s_text_message + event->payload_offset, event->data_ptr, event->data_len);
            if (event->payload_offset + event->data_len == event->payload_len) {
                s_text_message[event->payload_len] = '\0';
                process_json(s_text_message, event->payload_len);
            }
        }
        break;
    case WEBSOCKET_EVENT_ERROR:
        s_flush_audio = true;
        ESP_LOGE(TAG, "WebSocket transport error");
        break;
    default:
        break;
    }
}

static void upload_task(void *arg)
{
    while (true) {
        if (s_flush_audio) {
            size_t discarded = 0;
            size_t received = 0;
            do {
                received = xStreamBufferReceive(
                    s_audio_stream,
                    s_audio_batch,
                    sizeof(s_audio_batch),
                    0
                );
                discarded += received;
            } while (received > 0);
            s_flush_audio = false;
            if (discarded > 0) {
                ESP_LOGI(TAG, "Discarded %u bytes of stale audio", (unsigned)discarded);
            }
        }

        if (!s_connected || !esp_websocket_client_is_connected(s_client)) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        size_t batch_bytes = 0;
        for (int frame_index = 0; frame_index < PET_AUDIO_BATCH_FRAMES; ++frame_index) {
            size_t received = xStreamBufferReceive(
                s_audio_stream,
                s_audio_batch + batch_bytes,
                PET_AUDIO_BYTES,
                pdMS_TO_TICKS(250)
            );
            if (received != PET_AUDIO_BYTES) {
                if (received > 0) {
                    ESP_LOGW(TAG, "Discarded incomplete audio frame: %u bytes", (unsigned)received);
                }
                batch_bytes = 0;
                break;
            }
            batch_bytes += received;
        }

        if (batch_bytes != PET_AUDIO_BATCH_BYTES ||
            !s_connected || !esp_websocket_client_is_connected(s_client)) {
            continue;
        }

        int sent = esp_websocket_client_send_bin(
            s_client,
            (const char *)s_audio_batch,
            batch_bytes,
            pdMS_TO_TICKS(5000)
        );
        if (sent != (int)batch_bytes) {
            ESP_LOGW(TAG, "Audio upload incomplete: sent=%d expected=%u", sent, (unsigned)batch_bytes);
            s_flush_audio = true;
        }
    }
}

esp_err_t pet_cloud_start(StreamBufferHandle_t audio_stream)
{
    if (!audio_stream) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(CONFIG_PET_GATEWAY_URI) == 0 || strlen(CONFIG_PET_DEVICE_TOKEN) < 16) {
        ESP_LOGE(TAG, "Gateway URI/device token missing; run idf.py menuconfig");
        return ESP_ERR_INVALID_STATE;
    }

    s_audio_stream = audio_stream;
    static char authorization[384];
    int written = snprintf(
        authorization,
        sizeof(authorization),
        "Authorization: Bearer %s\r\n",
        CONFIG_PET_DEVICE_TOKEN
    );
    if (written < 0 || (size_t)written >= sizeof(authorization)) {
        return ESP_ERR_INVALID_SIZE;
    }

    esp_websocket_client_config_t config = {
        .uri = CONFIG_PET_GATEWAY_URI,
        .headers = authorization,
        .buffer_size = 8192,
        .network_timeout_ms = 30000,
        .reconnect_timeout_ms = 3000,
        .keep_alive_enable = true,
        .keep_alive_idle = 5,
        .keep_alive_interval = 5,
        .keep_alive_count = 3,
        .ping_interval_sec = 15,
        .pingpong_timeout_sec = 10,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    s_client = esp_websocket_client_init(&config);
    if (!s_client) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_websocket_register_events(
        s_client,
        WEBSOCKET_EVENT_ANY,
        websocket_event,
        NULL
    );
    if (err != ESP_OK) {
        esp_websocket_client_destroy(s_client);
        return err;
    }
    err = esp_websocket_client_start(s_client);
    if (err != ESP_OK) {
        esp_websocket_client_destroy(s_client);
        return err;
    }
    if (xTaskCreate(upload_task, "pet_upload", 6144, NULL, 7, NULL) != pdPASS) {
        esp_websocket_client_stop(s_client);
        esp_websocket_client_destroy(s_client);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Cloud relay started: %s", CONFIG_PET_GATEWAY_URI);
    return ESP_OK;
}
