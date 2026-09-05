#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t pet_leds_start(void);
void pet_leds_set_emotion(const char *emotion);
void pet_leds_trigger_touch(bool fast);
void pet_leds_note_voice_activity(void);
