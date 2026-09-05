#include "pet_leds.h"

#include <stdint.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "led_strip_rmt.h"
#include "pet_pins.h"

static const char *TAG = "pet_leds";

typedef enum {
    EMOTION_NEUTRAL,
    EMOTION_HAPPY,
    EMOTION_SAD,
    EMOTION_SURPRISED,
    EMOTION_ANGRY,
    EMOTION_FEARFUL,
    EMOTION_DISGUSTED,
} emotion_t;

static led_strip_handle_t s_strip_a;
#if !CONFIG_PET_LED_CHAINED
static led_strip_handle_t s_strip_b;
#endif
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;
static emotion_t s_emotion = EMOTION_NEUTRAL;
static int64_t s_overlay_until_us;
static bool s_overlay_fast;

static uint8_t scaled(uint16_t value)
{
    value = value * CONFIG_PET_LED_BRIGHTNESS / 255;
    return value > 255 ? 255 : (uint8_t)value;
}

static uint8_t triangle(uint8_t phase)
{
    return phase < 128 ? (uint8_t)(phase * 2) : (uint8_t)((255 - phase) * 2);
}

static void set_row_pixel(int row, int index, uint8_t red, uint8_t green, uint8_t blue)
{
    if (row == 0) {
        led_strip_set_pixel(s_strip_a, index, scaled(red), scaled(green), scaled(blue));
        return;
    }

#if CONFIG_PET_LED_CHAINED
    // Row B runs in the opposite physical direction after A DOUT -> B DIN.
    int chain_index = CONFIG_PET_LED_COUNT_A + (CONFIG_PET_LED_COUNT_B - 1 - index);
    led_strip_set_pixel(s_strip_a, chain_index, scaled(red), scaled(green), scaled(blue));
#else
    led_strip_set_pixel(
        s_strip_b,
        CONFIG_PET_LED_COUNT_B - 1 - index,
        scaled(red),
        scaled(green),
        scaled(blue)
    );
#endif
}

static void render_pixel(
    emotion_t emotion,
    bool overlay_active,
    bool overlay_fast,
    int row,
    int index,
    int count,
    uint8_t phase
)
{
    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;
    uint8_t wave = triangle((uint8_t)(phase + index * 20));

    if (overlay_active && overlay_fast) {
        // Quick touch: a warm golden chase that travels across both rows.
        red = 120 + wave / 2;
        green = 35 + wave / 3;
        blue = wave / 12;
    } else if (overlay_active) {
        // Slow touch: close, quiet rose-gold breathing.
        uint8_t breath = 36 + triangle((uint8_t)(phase / 2)) / 3;
        red = breath * 2;
        green = breath;
        blue = breath / 2;
    } else {
        switch (emotion) {
        case EMOTION_HAPPY:
            red = 90 + wave / 2;
            green = 32 + wave / 3;
            blue = 8 + wave / 10;
            if ((esp_random() & 0x7F) == 0) {
                red = 255;
                green = 150;
                blue = 60;
            }
            break;
        case EMOTION_SAD: {
            uint8_t breath = 18 + triangle((uint8_t)(phase / 2)) / 5;
            red = breath / 4;
            green = breath / 2;
            blue = breath * 2;
            break;
        }
        case EMOTION_SURPRISED:
            red = 80 + triangle(phase) / 2;
            green = 60 + triangle(phase) / 3;
            blue = 30 + triangle(phase) / 4;
            break;
        case EMOTION_ANGRY: {
            // Deliberately soothing amber rather than an alarming pure red.
            uint8_t heartbeat = triangle((uint8_t)(phase * 2));
            red = 65 + heartbeat / 2;
            green = 18 + heartbeat / 8;
            blue = 2;
            break;
        }
        case EMOTION_FEARFUL: {
            int center2 = count - 1;
            int distance2 = index * 2 - center2;
            if (distance2 < 0) {
                distance2 = -distance2;
            }
            uint8_t inward = triangle((uint8_t)(phase + distance2 * 18));
            red = 18 + inward / 5;
            green = 8 + inward / 10;
            blue = 42 + inward / 3;
            break;
        }
        case EMOTION_DISGUSTED:
            red = 18 + wave / 5;
            green = 28 + wave / 6;
            blue = 6;
            break;
        case EMOTION_NEUTRAL:
        default: {
            uint8_t breath = 22 + triangle((uint8_t)(phase / 2)) / 6;
            red = breath * 2;
            green = breath;
            blue = breath / 4;
            break;
        }
        }
    }
    set_row_pixel(row, index, red, green, blue);
}

static void led_task(void *arg)
{
    uint8_t phase = 0;
    while (true) {
        emotion_t emotion;
        bool overlay_fast;
        int64_t overlay_until;
        portENTER_CRITICAL(&s_state_lock);
        emotion = s_emotion;
        overlay_fast = s_overlay_fast;
        overlay_until = s_overlay_until_us;
        portEXIT_CRITICAL(&s_state_lock);

        bool overlay_active = esp_timer_get_time() < overlay_until;
        for (int i = 0; i < CONFIG_PET_LED_COUNT_A; ++i) {
            render_pixel(emotion, overlay_active, overlay_fast, 0, i, CONFIG_PET_LED_COUNT_A, phase);
        }
        for (int i = 0; i < CONFIG_PET_LED_COUNT_B; ++i) {
            render_pixel(emotion, overlay_active, overlay_fast, 1, i, CONFIG_PET_LED_COUNT_B, phase);
        }

        led_strip_refresh(s_strip_a);
#if !CONFIG_PET_LED_CHAINED
        led_strip_refresh(s_strip_b);
#endif
        phase += overlay_fast ? 10 : 3;
        vTaskDelay(pdMS_TO_TICKS(33));
    }
}

static esp_err_t create_strip(gpio_num_t gpio, int count, led_strip_handle_t *result)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = gpio,
        .max_leds = count,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags.invert_out = false,
    };
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 64,
        .flags.with_dma = true,
    };
    return led_strip_new_rmt_device(&strip_config, &rmt_config, result);
}

esp_err_t pet_leds_start(void)
{
#if CONFIG_PET_LED_CHAINED
    ESP_RETURN_ON_ERROR(
        create_strip(PET_LED_A_GPIO, CONFIG_PET_LED_COUNT_A + CONFIG_PET_LED_COUNT_B, &s_strip_a),
        TAG,
        "chained strip creation failed"
    );
#else
    ESP_RETURN_ON_ERROR(
        create_strip(PET_LED_A_GPIO, CONFIG_PET_LED_COUNT_A, &s_strip_a),
        TAG,
        "strip A creation failed"
    );
    ESP_RETURN_ON_ERROR(
        create_strip(PET_LED_B_GPIO, CONFIG_PET_LED_COUNT_B, &s_strip_b),
        TAG,
        "strip B creation failed"
    );
#endif

    led_strip_clear(s_strip_a);
#if !CONFIG_PET_LED_CHAINED
    led_strip_clear(s_strip_b);
#endif
    if (xTaskCreate(led_task, "pet_leds", 4096, NULL, 4, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
#if CONFIG_PET_LED_CHAINED
    ESP_LOGI(TAG, "LED engine started (chained)");
#else
    ESP_LOGI(TAG, "LED engine started (independent)");
#endif
    return ESP_OK;
}

void pet_leds_set_emotion(const char *emotion)
{
    emotion_t next = EMOTION_NEUTRAL;
    if (strcmp(emotion, "happy") == 0) next = EMOTION_HAPPY;
    else if (strcmp(emotion, "sad") == 0) next = EMOTION_SAD;
    else if (strcmp(emotion, "surprised") == 0) next = EMOTION_SURPRISED;
    else if (strcmp(emotion, "angry") == 0) next = EMOTION_ANGRY;
    else if (strcmp(emotion, "fearful") == 0) next = EMOTION_FEARFUL;
    else if (strcmp(emotion, "disgusted") == 0) next = EMOTION_DISGUSTED;

    portENTER_CRITICAL(&s_state_lock);
    s_emotion = next;
    portEXIT_CRITICAL(&s_state_lock);
    ESP_LOGI(TAG, "Base emotion: %s", emotion);
}

void pet_leds_trigger_touch(bool fast)
{
    portENTER_CRITICAL(&s_state_lock);
    s_overlay_fast = fast;
    s_overlay_until_us = esp_timer_get_time() + (fast ? 1500000 : 3000000);
    portEXIT_CRITICAL(&s_state_lock);
    ESP_LOGI(TAG, "%s touch effect", fast ? "Quick" : "Slow");
}
