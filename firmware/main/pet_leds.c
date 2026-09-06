#include "pet_leds.h"

#include <stdint.h>
#include <stdlib.h>
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

typedef enum {
    EMOTION_WARM,
    EMOTION_HAPPY,
    EMOTION_CALM,
    EMOTION_MISS,
    EMOTION_SAD,
} emotion_t;

typedef enum {
    TOUCH_NONE,
    TOUCH_QUICK,
    TOUCH_SLOW,
} touch_effect_t;

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} rgb_t;

typedef struct {
    rgb_t color_a;
    rgb_t color_b;
    uint8_t brightness;
    uint16_t period_ms;
} emotion_preset_t;

static const char *TAG = "pet_leds";

#define CALM_SILENCE_MS 10000
#define LIGHT_FADE_MS     1000
#define INACTIVITY_TIMEOUT_MS 60000

// Fixed presets avoid runtime configuration and gateway/device mismatches.
static const emotion_preset_t PRESETS[] = {
    [EMOTION_WARM]  = {{242, 160, 123}, {255, 215, 181}, 72, 2600},
    [EMOTION_HAPPY] = {{255, 179,  71}, {255, 224, 138}, 96,  900},
    [EMOTION_CALM]  = {{255, 228, 181}, {217, 242, 230}, 52, 4200},
    [EMOTION_MISS]  = {{233, 149, 121}, {255, 208, 181}, 78, 3000},
    [EMOTION_SAD]   = {{ 37,  74, 135}, {122, 159, 209}, 42, 3600},
};

static led_strip_handle_t s_strip_a;
#if !CONFIG_PET_LED_CHAINED
static led_strip_handle_t s_strip_b;
#endif

static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;
static emotion_t s_emotion = EMOTION_CALM;
static touch_effect_t s_touch_effect = TOUCH_NONE;
static int64_t s_touch_started_ms;
static int64_t s_touch_until_ms;
static int64_t s_calm_deadline_ms;
static int64_t s_inactivity_deadline_ms;
static int64_t s_event_until_ms;
static emotion_t s_event_emotion = EMOTION_CALM;
static bool s_always_on;
static bool s_awake;

static emotion_t parse_emotion(const char *emotion)
{
    if (emotion && strcmp(emotion, "warm") == 0) {
        return EMOTION_WARM;
    }
    if (emotion && strcmp(emotion, "happy") == 0) {
        return EMOTION_HAPPY;
    }
    if (emotion && strcmp(emotion, "miss") == 0) {
        return EMOTION_MISS;
    }
    if (emotion && strcmp(emotion, "sad") == 0) {
        return EMOTION_SAD;
    }
    return EMOTION_CALM;
}

static uint8_t triangle8(uint16_t phase)
{
    uint8_t value = phase & 0xff;
    return value < 128 ? value * 2 : (255 - value) * 2;
}

static uint8_t mix8(uint8_t a, uint8_t b, uint8_t amount)
{
    return a + ((int16_t)b - a) * amount / 255;
}

static rgb_t mix_color(rgb_t a, rgb_t b, uint8_t amount)
{
    return (rgb_t){
        .r = mix8(a.r, b.r, amount),
        .g = mix8(a.g, b.g, amount),
        .b = mix8(a.b, b.b, amount),
    };
}

static uint8_t apply_brightness(uint8_t value, uint8_t preset_brightness, uint8_t visibility)
{
    uint16_t level = (uint16_t)preset_brightness * CONFIG_PET_LED_BRIGHTNESS / 128;
    uint16_t visible_value = (uint16_t)value * visibility / 255;
    return visible_value * level / 255;
}

static void write_pixel(int row, int index, rgb_t color, uint8_t brightness, uint8_t visibility)
{
    uint8_t r = apply_brightness(color.r, brightness, visibility);
    uint8_t g = apply_brightness(color.g, brightness, visibility);
    uint8_t b = apply_brightness(color.b, brightness, visibility);

    if (row == 0) {
        led_strip_set_pixel(s_strip_a, index, r, g, b);
        return;
    }

    // The second strip is installed in the opposite physical direction.
#if CONFIG_PET_LED_CHAINED
    int physical_index = CONFIG_PET_LED_COUNT_A + CONFIG_PET_LED_COUNT_B - 1 - index;
    led_strip_set_pixel(s_strip_a, physical_index, r, g, b);
#else
    int physical_index = CONFIG_PET_LED_COUNT_B - 1 - index;
    led_strip_set_pixel(s_strip_b, physical_index, r, g, b);
#endif
}

static uint8_t position8(int index, int count)
{
    return count > 1 ? (uint32_t)index * 255 / (count - 1) : 0;
}

static rgb_t render_emotion(
    emotion_t emotion,
    const emotion_preset_t *preset,
    uint8_t position,
    int64_t now_ms
)
{
    uint8_t time_phase = (now_ms % preset->period_ms) * 255 / preset->period_ms;
    uint8_t amount = triangle8(time_phase);

    switch (emotion) {
    case EMOTION_HAPPY:
        amount = 80 + triangle8(time_phase * 2) / 2;
        if (esp_random() % 100 < 12) {
            amount = 255;
        }
        break;
    case EMOTION_MISS: {
        int distance = abs(127 - position);
        int centre_value = 255 - distance * 2;
        uint8_t centre = centre_value > 0 ? centre_value : 0;
        amount = (uint16_t)centre * (100 + triangle8(time_phase)) / 255;
        break;
    }
    case EMOTION_SAD:
        amount = triangle8(time_phase + position);
        break;
    case EMOTION_WARM:
    case EMOTION_CALM:
    default:
        amount = 40 + triangle8(time_phase) * 215 / 255;
        break;
    }

    return mix_color(preset->color_a, preset->color_b, amount);
}

static rgb_t render_touch(
    touch_effect_t effect,
    uint8_t position,
    int64_t elapsed_ms,
    uint8_t *brightness
)
{
    if (effect == TOUCH_QUICK) {
        uint8_t head = (elapsed_ms * 512 / 1500) & 0xff;
        int distance = abs((int)position - head);
        if (distance > 127) {
            distance = 256 - distance;
        }
        uint8_t amount = distance < 48 ? 255 - distance * 5 : 15;
        *brightness = 120;
        return mix_color((rgb_t){255, 100, 30}, (rgb_t){255, 209, 102}, amount);
    }

    uint8_t pulse = 60 + triangle8((elapsed_ms * 255 / 1800) & 0xff) / 2;
    *brightness = 86;
    return mix_color((rgb_t){233, 162, 143}, (rgb_t){255, 224, 194}, pulse);
}

static void render_row(
    int row,
    int count,
    emotion_t emotion,
    touch_effect_t touch,
    int64_t touch_started_ms,
    int64_t now_ms,
    uint8_t visibility
)
{
    const emotion_preset_t *preset = &PRESETS[emotion];
    for (int index = 0; index < count; ++index) {
        uint8_t position = position8(index, count);
        uint8_t brightness = preset->brightness;
        rgb_t color;

        if (touch != TOUCH_NONE) {
            color = render_touch(touch, position, now_ms - touch_started_ms, &brightness);
        } else {
            color = render_emotion(emotion, preset, position, now_ms);
        }
        write_pixel(row, index, color, brightness, visibility);
    }
}

static void led_task(void *arg)
{
    while (true) {
        emotion_t emotion;
        touch_effect_t touch;
        int64_t touch_started_ms;
        uint8_t visibility = 255;
        int64_t now_ms = esp_timer_get_time() / 1000;

        portENTER_CRITICAL(&s_state_lock);
        if (s_touch_effect != TOUCH_NONE && now_ms >= s_touch_until_ms) {
            s_touch_effect = TOUCH_NONE;
        }

        if (s_event_until_ms > 0 && now_ms >= s_event_until_ms) {
            s_event_until_ms = 0;
            if (!s_always_on) {
                s_awake = false;
            }
        }

        if (!s_always_on && s_event_until_ms == 0 && s_awake &&
            s_inactivity_deadline_ms > 0 && now_ms >= s_inactivity_deadline_ms) {
            s_awake = false;
            s_touch_effect = TOUCH_NONE;
        }

        if (s_always_on) {
            emotion = EMOTION_CALM;
        } else if (s_event_until_ms > 0) {
            emotion = s_event_emotion;
        } else {
            emotion = s_emotion;
        }

        if (!s_awake && !s_always_on && s_event_until_ms == 0) {
            visibility = 0;
        } else if (!s_always_on && s_event_until_ms == 0 &&
                   s_touch_effect == TOUCH_NONE &&
                   s_emotion == EMOTION_CALM &&
                   s_calm_deadline_ms > 0 &&
                   now_ms >= s_calm_deadline_ms) {
            int64_t fade_elapsed_ms = now_ms - s_calm_deadline_ms;
            if (fade_elapsed_ms >= LIGHT_FADE_MS) {
                s_awake = false;
                visibility = 0;
            } else {
                visibility = 255 - (uint32_t)fade_elapsed_ms * 255 / LIGHT_FADE_MS;
            }
        }
        touch = s_touch_effect;
        touch_started_ms = s_touch_started_ms;
        portEXIT_CRITICAL(&s_state_lock);

        render_row(0, CONFIG_PET_LED_COUNT_A, emotion, touch, touch_started_ms, now_ms, visibility);
        render_row(1, CONFIG_PET_LED_COUNT_B, emotion, touch, touch_started_ms, now_ms, visibility);
        led_strip_refresh(s_strip_a);
#if !CONFIG_PET_LED_CHAINED
        led_strip_refresh(s_strip_b);
#endif
        vTaskDelay(pdMS_TO_TICKS(33));
    }
}

static esp_err_t create_strip(gpio_num_t gpio, int count, led_strip_handle_t *strip)
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
        .flags.with_dma = false,
    };
    return led_strip_new_rmt_device(&strip_config, &rmt_config, strip);
}

esp_err_t pet_leds_start(void)
{
#if CONFIG_PET_LED_CHAINED
    ESP_RETURN_ON_ERROR(
        create_strip(PET_LED_A_GPIO, CONFIG_PET_LED_COUNT_A + CONFIG_PET_LED_COUNT_B, &s_strip_a),
        TAG,
        "LED strip creation failed"
    );
#else
    ESP_RETURN_ON_ERROR(
        create_strip(PET_LED_A_GPIO, CONFIG_PET_LED_COUNT_A, &s_strip_a),
        TAG,
        "LED strip A creation failed"
    );
    ESP_RETURN_ON_ERROR(
        create_strip(PET_LED_B_GPIO, CONFIG_PET_LED_COUNT_B, &s_strip_b),
        TAG,
        "LED strip B creation failed"
    );
    led_strip_clear(s_strip_b);
#endif
    led_strip_clear(s_strip_a);

    if (xTaskCreate(led_task, "pet_leds", 4096, NULL, 4, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "WS2812 ready: %d + %d pixels", CONFIG_PET_LED_COUNT_A, CONFIG_PET_LED_COUNT_B);
    return ESP_OK;
}

bool pet_leds_set_emotion(const char *emotion)
{
    emotion_t next = parse_emotion(emotion);
    bool applied = false;

    int64_t now_ms = esp_timer_get_time() / 1000;
    portENTER_CRITICAL(&s_state_lock);
    // Voice results cannot light the product before the owner has touched it.
    if (s_awake && !s_always_on) {
        s_emotion = next;
        s_calm_deadline_ms = next == EMOTION_CALM ? now_ms + CALM_SILENCE_MS : 0;
        s_inactivity_deadline_ms = now_ms + INACTIVITY_TIMEOUT_MS;
        applied = true;
    }
    portEXIT_CRITICAL(&s_state_lock);
    return applied;
}

void pet_leds_set_always_on(bool enabled)
{
    portENTER_CRITICAL(&s_state_lock);
    s_always_on = enabled;
    s_event_until_ms = 0;
    s_touch_effect = TOUCH_NONE;
    s_awake = enabled;
    s_emotion = EMOTION_CALM;
    s_calm_deadline_ms = 0;
    s_inactivity_deadline_ms = 0;
    portEXIT_CRITICAL(&s_state_lock);
}

void pet_leds_show_event(const char *emotion, uint32_t hold_ms)
{
    int64_t now_ms = esp_timer_get_time() / 1000;
    portENTER_CRITICAL(&s_state_lock);
    if (!s_always_on) {
        s_event_emotion = parse_emotion(emotion);
        s_event_until_ms = now_ms + hold_ms;
        s_inactivity_deadline_ms = now_ms + INACTIVITY_TIMEOUT_MS;
        s_awake = true;
    }
    portEXIT_CRITICAL(&s_state_lock);
}

void pet_leds_trigger_touch(bool fast)
{
    int64_t now_ms = esp_timer_get_time() / 1000;
    int64_t until_ms = now_ms + (fast ? 1500 : 3000);
    portENTER_CRITICAL(&s_state_lock);
    if (!s_awake) {
        s_awake = true;
        s_emotion = EMOTION_CALM;
    }
    s_touch_effect = fast ? TOUCH_QUICK : TOUCH_SLOW;
    s_touch_started_ms = now_ms;
    s_touch_until_ms = until_ms;
    s_inactivity_deadline_ms = now_ms + INACTIVITY_TIMEOUT_MS;
    if (s_emotion == EMOTION_CALM) {
        // Give calm a full ten seconds after the immediate touch animation.
        s_calm_deadline_ms = until_ms + CALM_SILENCE_MS;
    }
    portEXIT_CRITICAL(&s_state_lock);
}

void pet_leds_note_voice_activity(void)
{
    int64_t now_ms = esp_timer_get_time() / 1000;
    int64_t deadline_ms = now_ms + CALM_SILENCE_MS;
    portENTER_CRITICAL(&s_state_lock);
    if (s_awake && !s_always_on) {
        s_inactivity_deadline_ms = now_ms + INACTIVITY_TIMEOUT_MS;
    }
    if (s_awake && s_emotion == EMOTION_CALM && deadline_ms > s_calm_deadline_ms) {
        s_calm_deadline_ms = deadline_ms;
    }
    portEXIT_CRITICAL(&s_state_lock);
}
