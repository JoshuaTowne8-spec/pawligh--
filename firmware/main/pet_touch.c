#include "pet_touch.h"

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pet_leds.h"
#include "pet_pins.h"

#define MPR121_ADDRESS       0x5A
#define MPR121_TOUCH_STATUS  0x00
#define MPR121_ECR           0x5E
#define MPR121_SOFT_RESET    0x80

static const char *TAG = "pet_touch";
static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_mpr121;

static esp_err_t write_reg(uint8_t reg, uint8_t value)
{
    uint8_t data[2] = {reg, value};
    return i2c_master_transmit(s_mpr121, data, sizeof(data), 100);
}

static esp_err_t read_touch_mask(uint16_t *mask)
{
    uint8_t reg = MPR121_TOUCH_STATUS;
    uint8_t data[2];
    esp_err_t err = i2c_master_transmit_receive(s_mpr121, &reg, 1, data, sizeof(data), 100);
    if (err == ESP_OK) {
        *mask = ((uint16_t)data[1] << 8 | data[0]) & 0x01FF;
    }
    return err;
}

static esp_err_t configure_mpr121(void)
{
    ESP_RETURN_ON_ERROR(write_reg(MPR121_SOFT_RESET, 0x63), TAG, "soft reset failed");
    vTaskDelay(pdMS_TO_TICKS(5));
    ESP_RETURN_ON_ERROR(write_reg(MPR121_ECR, 0x00), TAG, "stop mode failed");

    const struct { uint8_t reg; uint8_t value; } filters[] = {
        {0x2B, 0x01}, {0x2C, 0x01}, {0x2D, 0x00}, {0x2E, 0x00},
        {0x2F, 0x01}, {0x30, 0x01}, {0x31, 0xFF}, {0x32, 0x02},
        {0x33, 0x00}, {0x34, 0x00}, {0x35, 0x00},
        {0x5B, 0x00}, {0x5C, 0x10}, {0x5D, 0x20},
    };
    for (size_t i = 0; i < sizeof(filters) / sizeof(filters[0]); ++i) {
        ESP_RETURN_ON_ERROR(write_reg(filters[i].reg, filters[i].value), TAG, "filter setup failed");
    }
    for (int electrode = 0; electrode < 9; ++electrode) {
        ESP_RETURN_ON_ERROR(write_reg(0x41 + electrode * 2, 12), TAG, "touch threshold failed");
        ESP_RETURN_ON_ERROR(write_reg(0x42 + electrode * 2, 6), TAG, "release threshold failed");
    }

    // CL=2 (baseline tracking) and ELEPROX=0, ELE=9 active electrodes.
    ESP_RETURN_ON_ERROR(write_reg(MPR121_ECR, 0x89), TAG, "run mode failed");
    return ESP_OK;
}

static void touch_task(void *arg)
{
    uint16_t previous = 0;
    int64_t gesture_start_us = 0;
    unsigned transitions = 0;
    bool slow_fired = false;

    while (true) {
        uint16_t mask = 0;
        if (read_touch_mask(&mask) != ESP_OK) {
            ESP_LOGW(TAG, "MPR121 read failed");
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        int64_t now = esp_timer_get_time();

        if (previous == 0 && mask != 0) {
            gesture_start_us = now;
            transitions = 1;
            slow_fired = false;
        } else if (mask != previous && mask != 0) {
            ++transitions;
        }

        if (mask != 0 && !slow_fired && now - gesture_start_us >= 800000) {
            pet_leds_trigger_touch(false);
            slow_fired = true;
        }

        if (previous != 0 && mask == 0) {
            int64_t duration = now - gesture_start_us;
            if (!slow_fired && transitions >= 3 && duration <= 1200000) {
                pet_leds_trigger_touch(true);
            } else if (!slow_fired && duration >= 500000) {
                pet_leds_trigger_touch(false);
            }
            gesture_start_us = 0;
        }

        previous = mask;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

esp_err_t pet_touch_start(void)
{
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PET_I2C_SDA_GPIO,
        .scl_io_num = PET_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &s_bus), TAG, "I2C bus failed");

    i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MPR121_ADDRESS,
        .scl_speed_hz = 400000,
    };
    ESP_RETURN_ON_ERROR(
        i2c_master_bus_add_device(s_bus, &device_config, &s_mpr121),
        TAG,
        "MPR121 device failed"
    );

    gpio_config_t irq_config = {
        .pin_bit_mask = 1ULL << PET_MPR121_IRQ_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&irq_config), TAG, "IRQ GPIO failed");
    ESP_RETURN_ON_ERROR(configure_mpr121(), TAG, "MPR121 configuration failed");

    uint16_t initial;
    ESP_RETURN_ON_ERROR(read_touch_mask(&initial), TAG, "MPR121 not responding at 0x5A");
    if (xTaskCreate(touch_task, "pet_touch", 4096, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "MPR121 started with electrodes E0-E8");
    return ESP_OK;
}
