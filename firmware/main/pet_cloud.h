#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"

esp_err_t pet_cloud_start(StreamBufferHandle_t audio_stream);
