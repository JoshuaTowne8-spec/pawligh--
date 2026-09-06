#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t pet_leds_start(void);
bool pet_leds_set_emotion(const char *emotion);
void pet_leds_set_always_on(bool enabled);
void pet_leds_show_event(const char *emotion, uint32_t hold_ms);
void pet_leds_trigger_touch(bool fast);
void pet_leds_note_voice_activity(void);
