#include "pet_cloud.h"

#include <stdio.h>
#include <stdlib.h>
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
static char *s_text_message;
static size_t s_text_capacity;

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
            ESP_LOGI(TAG, "Gateway ready; microphone upload enabled");
            pet_audio_set_streaming(true);
        } else if (strcmp(type->valuestring, "effect.select") == 0) {
            const cJSON *preset = cJSON_GetObjectItemCaseSensitive(root, "preset");
            const cJSON *config = cJSON_GetObjectItemCaseSensitive(root, "config");
            if (cJSON_IsString(preset)) {
                pet_leds_apply_config(preset->valuestring, config);
            }
        } else if (strcmp(type->valuestring, "emotion") == 0) {
            const cJSON *emotion = cJSON_GetObjectItemCaseSensitive(root, "emotion");
            if (cJSON_IsString(emotion)) {
                pet_leds_set_emotion(emotion->valuestring);
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
        ESP_LOGI(TAG, "Connected to emotion gateway");
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        s_connected = false;
        pet_audio_set_streaming(false);
        ESP_LOGW(TAG, "Gateway disconnected; client will reconnect");
        break;
    case WEBSOCKET_EVENT_DATA:
        if (event->op_code != 0x1 && event->op_code != 0x0) {
            break;
        }
        if (event->payload_len <= 0 || event->payload_len > 4096) {
            ESP_LOGW(TAG, "Unexpected text payload length: %d", event->payload_len);
            break;
        }
        if ((size_t)event->payload_len + 1 > s_text_capacity) {
            char *next = realloc(s_text_message, event->payload_len + 1);
            if (!next) {
                ESP_LOGE(TAG, "No memory for WebSocket message");
                break;
            }
            s_text_message = next;
            s_text_capacity = event->payload_len + 1;
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
        ESP_LOGE(TAG, "WebSocket transport error");
        break;
    default:
        break;
    }
}

static void upload_task(void *arg)
{
    uint8_t frame[PET_AUDIO_BYTES];

    while (true) {
        size_t received = xStreamBufferReceive(
            s_audio_stream,
            frame,
            sizeof(frame),
            pdMS_TO_TICKS(1000)
        );
        if (received == 0 || !s_connected || !esp_websocket_client_is_connected(s_client)) {
            continue;
        }

        int sent = esp_websocket_client_send_bin(
            s_client,
            (const char *)frame,
            received,
            pdMS_TO_TICKS(1000)
        );
        if (sent < 0) {
            ESP_LOGW(TAG, "Audio upload failed");
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
        .buffer_size = 2048,
        .network_timeout_ms = 10000,
        .reconnect_timeout_ms = 3000,
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
