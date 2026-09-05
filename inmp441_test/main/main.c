#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define MIC_SAMPLE_RATE 16000
#define MIC_SAMPLE_COUNT 512

#define MIC_SD_GPIO   GPIO_NUM_16
#define MIC_SCK_GPIO  GPIO_NUM_17
#define MIC_WS_GPIO   GPIO_NUM_18

#define TOUCH_SDA_GPIO GPIO_NUM_8
#define TOUCH_SCL_GPIO GPIO_NUM_9
#define MPR121_ADDRESS 0x5A

static const char *TAG = "inmp441_test";
static i2s_chan_handle_t s_rx_channel;
static int32_t s_raw_samples[MIC_SAMPLE_COUNT];
static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_mpr121;

static esp_err_t mpr121_write(uint8_t reg, uint8_t value)
{
    uint8_t data[2] = {reg, value};
    return i2c_master_transmit(s_mpr121, data, sizeof(data), 100);
}

static esp_err_t mpr121_read_mask(uint16_t *mask)
{
    uint8_t reg = 0x00;
    uint8_t data[2] = {0};
    esp_err_t err = i2c_master_transmit_receive(s_mpr121, &reg, 1, data, sizeof(data), 100);
    if (err == ESP_OK) {
        *mask = (((uint16_t)data[1] << 8) | data[0]) & 0x00FF;
    }
    return err;
}

static esp_err_t mpr121_configure(void)
{
    ESP_RETURN_ON_ERROR(mpr121_write(0x80, 0x63), TAG, "MPR121 reset failed");
    vTaskDelay(pdMS_TO_TICKS(5));
    ESP_RETURN_ON_ERROR(mpr121_write(0x5E, 0x00), TAG, "MPR121 stop failed");

    const struct {
        uint8_t reg;
        uint8_t value;
    } filters[] = {
        {0x2B, 0x01}, {0x2C, 0x01}, {0x2D, 0x00}, {0x2E, 0x00},
        {0x2F, 0x01}, {0x30, 0x01}, {0x31, 0xFF}, {0x32, 0x02},
        {0x33, 0x00}, {0x34, 0x00}, {0x35, 0x00},
        {0x5B, 0x00}, {0x5C, 0x10}, {0x5D, 0x20},
    };
    for (size_t i = 0; i < sizeof(filters) / sizeof(filters[0]); ++i) {
        ESP_RETURN_ON_ERROR(mpr121_write(filters[i].reg, filters[i].value), TAG, "MPR121 filter failed");
    }

    for (int electrode = 0; electrode < 8; ++electrode) {
        ESP_RETURN_ON_ERROR(mpr121_write(0x41 + electrode * 2, 12), TAG, "touch threshold failed");
        ESP_RETURN_ON_ERROR(mpr121_write(0x42 + electrode * 2, 6), TAG, "release threshold failed");
    }

    // Enable only E0-E7 and automatic baseline tracking.
    return mpr121_write(0x5E, 0x88);
}

static void print_electrodes(uint16_t mask)
{
    printf("electrode=");
    for (int electrode = 0; electrode < 8; ++electrode) {
        if (mask & (1U << electrode)) {
            printf("E%d(%s) ", electrode, electrode == 0 ? "HEAD" : "BACK");
        }
    }
}

static void touch_task(void *arg)
{
    bool touching = false;
    bool slow_reported = false;
    int64_t started_us = 0;
    uint16_t active_mask = 0;
    unsigned read_errors = 0;

    while (true) {
        uint16_t mask = 0;
        if (mpr121_read_mask(&mask) != ESP_OK) {
            if (++read_errors % 20 == 1) {
                ESP_LOGW(TAG, "MPR121 read failed; check 3.3V, GND, SDA and SCL");
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        read_errors = 0;

        int64_t now_us = esp_timer_get_time();
        if (!touching && mask != 0) {
            touching = true;
            slow_reported = false;
            started_us = now_us;
            active_mask = mask;
            printf("\nTOUCH START  mask=0x%02X  ", active_mask);
            print_electrodes(active_mask);
            putchar('\n');
        } else if (touching && mask != 0) {
            active_mask |= mask;
            if (!slow_reported && now_us - started_us >= 800000) {
                printf("\nSLOW TOUCH   duration>=800ms  ");
                print_electrodes(active_mask);
                putchar('\n');
                slow_reported = true;
            }
        } else if (touching) {
            int64_t duration_ms = (now_us - started_us) / 1000;
            if (!slow_reported && duration_ms >= 50) {
                printf("\nQUICK TOUCH  duration=%" PRId64 "ms  ", duration_ms);
                print_electrodes(active_mask);
                putchar('\n');
            }
            touching = false;
            active_mask = 0;
        }

        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(25));
    }
}

static esp_err_t touch_start(void)
{
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = TOUCH_SDA_GPIO,
        .scl_io_num = TOUCH_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &s_i2c_bus), TAG, "I2C bus failed");

    i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MPR121_ADDRESS,
        .scl_speed_hz = 100000,
    };
    ESP_RETURN_ON_ERROR(
        i2c_master_bus_add_device(s_i2c_bus, &device_config, &s_mpr121),
        TAG,
        "MPR121 device failed"
    );
    ESP_RETURN_ON_ERROR(mpr121_configure(), TAG, "MPR121 configuration failed");

    uint16_t initial_mask = 0;
    ESP_RETURN_ON_ERROR(mpr121_read_mask(&initial_mask), TAG, "MPR121 not responding at 0x5A");
    if (xTaskCreate(touch_task, "touch_test", 4096, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

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
    // Initialize I2C before I2S, matching the main firmware's stable startup order.
    ESP_ERROR_CHECK(touch_start());
    ESP_ERROR_CHECK(microphone_start());

    printf("\nINMP441 + MPR121 standalone test started\n");
    printf("Pins: SD=GPIO16, SCK=GPIO17, WS=GPIO18, L/R=GND\n");
    printf("Touch: SDA=GPIO8, SCL=GPIO9; E0=HEAD, E1-E7=BACK\n");
    printf("Short touch=QUICK, hold for 0.8s=SLOW\n");
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
