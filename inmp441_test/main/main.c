#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define MIC_SAMPLE_RATE 16000
#define MIC_SAMPLE_COUNT 512

#define MIC_SD_GPIO   GPIO_NUM_16
#define MIC_SCK_GPIO  GPIO_NUM_17
#define MIC_WS_GPIO   GPIO_NUM_18

static const char *TAG = "inmp441_test";
static i2s_chan_handle_t s_rx_channel;
static int32_t s_raw_samples[MIC_SAMPLE_COUNT];

static esp_err_t microphone_start(void)
{
    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(
        I2S_NUM_AUTO,
        I2S_ROLE_MASTER
    );
    channel_config.dma_desc_num = 8;
    channel_config.dma_frame_num = 320;

    ESP_RETURN_ON_ERROR(
        i2s_new_channel(&channel_config, NULL, &s_rx_channel),
        TAG,
        "I2S channel creation failed"
    );

    i2s_std_config_t standard_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(MIC_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_32BIT,
            I2S_SLOT_MODE_MONO
        ),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = MIC_SCK_GPIO,
            .ws = MIC_WS_GPIO,
            .dout = I2S_GPIO_UNUSED,
            .din = MIC_SD_GPIO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    // L/R is connected to GND, so INMP441 transmits in the left slot.
    standard_config.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

    ESP_RETURN_ON_ERROR(
        i2s_channel_init_std_mode(s_rx_channel, &standard_config),
        TAG,
        "I2S mode initialization failed"
    );
    return i2s_channel_enable(s_rx_channel);
}

static void print_volume_bar(uint32_t rms)
{
    int bars = rms / 150;
    if (bars > 40) {
        bars = 40;
    }

    putchar('[');
    for (int i = 0; i < 40; ++i) {
        putchar(i < bars ? '#' : '.');
    }
    putchar(']');
}

void app_main(void)
{
    ESP_ERROR_CHECK(microphone_start());

    printf("\nINMP441 standalone test started\n");
    printf("Pins: SD=GPIO16, SCK=GPIO17, WS=GPIO18, L/R=GND\n");
    printf("No Wi-Fi or cloud connection is used. Speak near the microphone.\n\n");

    while (true) {
        int64_t dc_sum = 0;
        int64_t sum_squares = 0;
        int32_t peak = 0;
        size_t total_samples = 0;

        // Sixteen blocks are about 0.5 seconds at 16 kHz.
        for (int block = 0; block < 16; ++block) {
            size_t bytes_read = 0;
            esp_err_t err = i2s_channel_read(
                s_rx_channel,
                s_raw_samples,
                sizeof(s_raw_samples),
                &bytes_read,
                pdMS_TO_TICKS(1000)
            );
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "I2S read failed: %s", esp_err_to_name(err));
                break;
            }

            size_t samples = bytes_read / sizeof(s_raw_samples[0]);
            if (samples == 0) {
                continue;
            }

            int64_t block_sum = 0;
            for (size_t i = 0; i < samples; ++i) {
                // INMP441 output is signed 24-bit, left-aligned in a 32-bit slot.
                int32_t sample = s_raw_samples[i] >> 16;
                block_sum += sample;
            }

            int32_t block_dc = block_sum / (int64_t)samples;
            for (size_t i = 0; i < samples; ++i) {
                int32_t sample = (s_raw_samples[i] >> 16) - block_dc;
                int32_t magnitude = abs(sample);
                if (magnitude > peak) {
                    peak = magnitude;
                }
                sum_squares += (int64_t)sample * sample;
            }

            dc_sum += block_sum;
            total_samples += samples;
        }

        if (total_samples == 0) {
            continue;
        }

        int32_t dc = dc_sum / (int64_t)total_samples;
        uint32_t rms = (uint32_t)sqrt((double)sum_squares / total_samples);
        printf("RMS=%5" PRIu32 "  PEAK=%5" PRId32 "  DC=%6" PRId32 "  ", rms, peak, dc);
        print_volume_bar(rms);

        if (peak >= 32000) {
            printf("  CLIPPING");
        } else if (rms < 5 && peak < 20) {
            printf("  NO SIGNAL / VERY QUIET");
        }
        putchar('\n');
        fflush(stdout);
    }
}
