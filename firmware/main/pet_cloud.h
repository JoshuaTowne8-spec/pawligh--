#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include <stdbool.h>

esp_err_t pet_cloud_start(StreamBufferHandle_t audio_stream);
void pet_cloud_notify_touch(bool fast);
