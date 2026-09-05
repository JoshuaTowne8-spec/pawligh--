#include <stdlib.h>

#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include "pet_audio.h"
#include "pet_cloud.h"
#include "pet_leds.h"
#include "pet_pins.h"
#include "pet_touch.h"
#include "pet_wifi.h"

static const char *TAG = "pet_main";
static StaticStreamBuffer_t s_audio_stream_state;
static uint8_t s_audio_stream_storage[PET_AUDIO_BYTES * 12 + 1];

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(pet_leds_start());

    // Initialize I2C before I2S. This keeps peripheral setup deterministic and
    // avoids the I2C/GDMA startup conflict seen on some ESP32-S3 boards.
    err = pet_touch_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MPR121 unavailable; voice mode will continue: %s", esp_err_to_name(err));
    }

    StreamBufferHandle_t audio_stream = xStreamBufferCreateStatic(
        PET_AUDIO_BYTES * 12,
        PET_AUDIO_BYTES,
        s_audio_stream_storage,
        &s_audio_stream_state
    );
    if (!audio_stream) {
        ESP_LOGE(TAG, "Unable to create static audio queue");
        abort();
    }
    ESP_ERROR_CHECK(pet_audio_start(audio_stream));

    ESP_ERROR_CHECK(pet_wifi_connect());
    ESP_ERROR_CHECK(pet_cloud_start(audio_stream));
    ESP_LOGI(TAG, "Pet emotion lamp is running");
}
