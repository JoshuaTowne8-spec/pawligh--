#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"

esp_err_t pet_audio_start(StreamBufferHandle_t output_stream);
void pet_audio_set_streaming(bool enabled);
