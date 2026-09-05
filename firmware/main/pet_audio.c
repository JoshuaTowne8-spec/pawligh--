#include "pet_audio.h"

#include <limits.h>
#include <stdint.h>

#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "pet_pins.h"

static const char *TAG = "pet_audio";
static i2s_chan_handle_t s_rx_channel;
static StreamBufferHandle_t s_output_stream;
static volatile bool s_streaming;

static int16_t convert_sample(int32_t raw)
{
    // INMP441 provides signed 24-bit data left-aligned in a 32-bit I2S slot.
    int32_t sample_24 = raw >> 8;
    int32_t sample_16 = (sample_24 * CONFIG_PET_MIC_GAIN) >> 8;
    if (sample_16 > INT16_MAX) {
        sample_16 = INT16_MAX;
    } else if (sample_16 < INT16_MIN) {
        sample_16 = INT16_MIN;
    }
    return (int16_t)sample_16;
}

static void audio_task(void *arg)
{
    int32_t raw[PET_AUDIO_SAMPLES];
    int16_t pcm[PET_AUDIO_SAMPLES];

    while (true) {
        size_t bytes_read = 0;
        esp_err_t err = i2s_channel_read(
            s_rx_channel,
            raw,
            sizeof(raw),
            &bytes_read,
            portMAX_DELAY
        );
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "I2S read failed: %s", esp_err_to_name(err));
            continue;
        }

        size_t samples = bytes_read / sizeof(raw[0]);
        for (size_t i = 0; i < samples; ++i) {
            pcm[i] = convert_sample(raw[i]);
        }

        if (s_streaming) {
            size_t bytes = samples * sizeof(pcm[0]);
            size_t sent = xStreamBufferSend(s_output_stream, pcm, bytes, 0);
            if (sent != bytes) {
                ESP_LOGW(TAG, "Audio queue full; dropped %u bytes", (unsigned)(bytes - sent));
            }
        }
    }
}

esp_err_t pet_audio_start(StreamBufferHandle_t output_stream)
{
    if (!output_stream) {
        return ESP_ERR_INVALID_ARG;
    }
    s_output_stream = output_stream;

    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    channel_config.dma_desc_num = 8;
    channel_config.dma_frame_num = 320;
    ESP_RETURN_ON_ERROR(
        i2s_new_channel(&channel_config, NULL, &s_rx_channel),
        TAG,
        "I2S channel creation failed"
    );

    i2s_std_config_t standard_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(PET_AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_32BIT,
            I2S_SLOT_MODE_MONO
        ),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = PET_I2S_BCLK_GPIO,
            .ws = PET_I2S_WS_GPIO,
            .dout = I2S_GPIO_UNUSED,
            .din = PET_I2S_SD_GPIO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    standard_config.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

    ESP_RETURN_ON_ERROR(
        i2s_channel_init_std_mode(s_rx_channel, &standard_config),
        TAG,
        "I2S standard mode failed"
    );
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_rx_channel), TAG, "I2S enable failed");

    if (xTaskCreate(audio_task, "pet_audio", 4096, NULL, 8, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "INMP441 started: 16 kHz, mono, 40 ms frames");
    return ESP_OK;
}

void pet_audio_set_streaming(bool enabled)
{
    s_streaming = enabled;
    ESP_LOGI(TAG, "Audio streaming %s", enabled ? "enabled" : "disabled");
}
