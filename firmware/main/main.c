#include "esp_check.h"
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

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(pet_leds_start());

    StreamBufferHandle_t audio_stream = xStreamBufferCreate(PET_AUDIO_BYTES * 25, PET_AUDIO_BYTES);
    if (!audio_stream) {
        ESP_LOGE(TAG, "Unable to allocate audio stream buffer");
        abort();
    }
    ESP_ERROR_CHECK(pet_audio_start(audio_stream));

    err = pet_touch_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Touch disabled: %s", esp_err_to_name(err));
    }

    ESP_ERROR_CHECK(pet_wifi_connect());
    ESP_ERROR_CHECK(pet_cloud_start(audio_stream));
    ESP_LOGI(TAG, "Pet emotion lamp is running");
}
