#pragma once

#include <stdbool.h>

#include "esp_err.h"

typedef struct cJSON cJSON;

esp_err_t pet_leds_start(void);
void pet_leds_set_emotion(const char *emotion);
void pet_leds_apply_config(const char *preset, const cJSON *config);
void pet_leds_trigger_touch(bool fast);
