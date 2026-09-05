#pragma once

// Copy this file to config.h, then fill in your local Wi-Fi and device token.
// config.h is intentionally ignored by Git and must never be uploaded.
// The Alibaba Cloud API key stays only in gateway/.env on the gateway computer.

#define PET_WIFI_SSID       "YOUR_WIFI_NAME"
#define PET_WIFI_PASSWORD   "YOUR_WIFI_PASSWORD"

#define PET_GATEWAY_HOST    "192.168.18.49"
#define PET_GATEWAY_PORT    8080
#define PET_GATEWAY_PATH    "/v1/device/audio"
#define PET_DEVICE_TOKEN    "PASTE_THE_DEVICE_TOKEN_FROM_GATEWAY_ENV"

#define PET_LED_A_PIN       4
#define PET_LED_B_PIN       5
#define PET_LED_COUNT_A     30
#define PET_LED_COUNT_B     60
#define PET_LED_BRIGHTNESS  64

#define PET_I2C_SDA_PIN     8
#define PET_I2C_SCL_PIN     9
#define PET_MPR121_IRQ_PIN  10

#define PET_I2S_SD_PIN      16
#define PET_I2S_BCLK_PIN    17
#define PET_I2S_WS_PIN      18
#define PET_MIC_GAIN        4
