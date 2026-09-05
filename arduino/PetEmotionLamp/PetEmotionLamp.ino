#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP_I2S.h>
#include <FastLED.h>
#include <Adafruit_MPR121.h>
#include <WebSocketsClient.h>
#include <WiFi.h>
#include <Wire.h>

#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include "config.h"

// ------------------------------ Audio ---------------------------------

constexpr uint32_t AUDIO_SAMPLE_RATE = 16000;
constexpr uint32_t AUDIO_FRAME_MS = 40;
constexpr size_t AUDIO_SAMPLES = AUDIO_SAMPLE_RATE * AUDIO_FRAME_MS / 1000;
constexpr size_t AUDIO_FRAME_BYTES = AUDIO_SAMPLES * sizeof(int16_t);

I2SClass microphone;
StreamBufferHandle_t audioStream = nullptr;
volatile bool streamAudioToCloud = false;

static int16_t convertMicSample(int32_t raw) {
  // INMP441: signed 24-bit sample, left aligned in a 32-bit I2S slot.
  int32_t sample24 = raw >> 8;
  int32_t sample16 = (sample24 * PET_MIC_GAIN) >> 8;
  return static_cast<int16_t>(constrain(sample16, -32768, 32767));
}

static void audioCaptureTask(void *parameter) {
  static int32_t raw[AUDIO_SAMPLES];
  static int16_t pcm[AUDIO_SAMPLES];

  while (true) {
    size_t bytesRead = microphone.readBytes(
      reinterpret_cast<char *>(raw), sizeof(raw)
    );
    if (bytesRead == 0) {
      vTaskDelay(pdMS_TO_TICKS(1));
      continue;
    }

    size_t samplesRead = bytesRead / sizeof(raw[0]);
    for (size_t i = 0; i < samplesRead; ++i) {
      pcm[i] = convertMicSample(raw[i]);
    }

    if (streamAudioToCloud) {
      size_t pcmBytes = samplesRead * sizeof(pcm[0]);
      size_t queued = xStreamBufferSend(audioStream, pcm, pcmBytes, 0);
      if (queued != pcmBytes) {
        Serial.println("[audio] queue full; frame dropped");
      }
    }
  }
}

static bool startMicrophone() {
  microphone.setPins(
    PET_I2S_BCLK_PIN,
    PET_I2S_WS_PIN,
    -1,
    PET_I2S_SD_PIN
  );
  if (!microphone.begin(
        I2S_MODE_STD,
        AUDIO_SAMPLE_RATE,
        I2S_DATA_BIT_WIDTH_32BIT,
        I2S_SLOT_MODE_MONO,
        I2S_STD_SLOT_LEFT)) {
    Serial.println("[audio] INMP441 initialization failed");
    return false;
  }

  audioStream = xStreamBufferCreate(AUDIO_FRAME_BYTES * 25, AUDIO_FRAME_BYTES);
  if (audioStream == nullptr) {
    Serial.println("[audio] stream buffer allocation failed");
    return false;
  }
  if (xTaskCreatePinnedToCore(
        audioCaptureTask,
        "audio_capture",
        4096,
        nullptr,
        3,
        nullptr,
        0) != pdPASS) {
    Serial.println("[audio] task creation failed");
    return false;
  }
  Serial.println("[audio] INMP441 ready: PCM16 mono, 16 kHz");
  return true;
}

// ------------------------------- LEDs ---------------------------------

enum class Emotion : uint8_t {
  Neutral,
  Happy,
  Sad,
  Surprised,
  Angry,
  Fearful,
  Disgusted,
};

CRGB ledsA[PET_LED_COUNT_A];
CRGB ledsB[PET_LED_COUNT_B];
Emotion baseEmotion = Emotion::Neutral;
bool touchOverlayActive = false;
bool touchOverlayFast = false;
uint32_t touchOverlayEndsAt = 0;
uint32_t lastLedFrameAt = 0;
uint8_t animationPhase = 0;

static uint8_t triangle8(uint8_t phase) {
  return phase < 128 ? phase * 2 : (255 - phase) * 2;
}

static bool timeBefore(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(deadline - now) > 0;
}

static CRGB emotionPixel(
  Emotion emotion,
  bool overlay,
  bool fastOverlay,
  uint16_t index,
  uint16_t count,
  uint8_t phase
) {
  uint8_t wave = triangle8(phase + static_cast<uint8_t>((index * 255UL) / count));

  if (overlay && fastOverlay) {
    // Quick tap/swipe: bright golden chase.
    return CRGB(120 + wave / 2, 35 + wave / 3, wave / 12);
  }
  if (overlay) {
    // Slow touch: quiet rose-gold breathing.
    uint8_t breath = 36 + triangle8(phase / 2) / 3;
    return CRGB(breath * 2, breath, breath / 2);
  }

  switch (emotion) {
    case Emotion::Happy:
      if (random8() < 2) return CRGB(255, 150, 60);
      return CRGB(90 + wave / 2, 32 + wave / 3, 8 + wave / 10);

    case Emotion::Sad: {
      uint8_t breath = 18 + triangle8(phase / 2) / 5;
      return CRGB(breath / 4, breath / 2, breath * 2);
    }

    case Emotion::Surprised: {
      uint8_t bloom = triangle8(phase);
      return CRGB(80 + bloom / 2, 60 + bloom / 3, 30 + bloom / 4);
    }

    case Emotion::Angry: {
      // Soothing amber instead of an alarming pure red.
      uint8_t heartbeat = triangle8(phase * 2);
      return CRGB(65 + heartbeat / 2, 18 + heartbeat / 8, 2);
    }

    case Emotion::Fearful: {
      int center2 = count - 1;
      int distance2 = abs(static_cast<int>(index) * 2 - center2);
      uint8_t inward = triangle8(phase + distance2 * 18);
      return CRGB(18 + inward / 5, 8 + inward / 10, 42 + inward / 3);
    }

    case Emotion::Disgusted:
      return CRGB(18 + wave / 5, 28 + wave / 6, 6);

    case Emotion::Neutral:
    default: {
      uint8_t breath = 22 + triangle8(phase / 2) / 6;
      return CRGB(breath * 2, breath, breath / 4);
    }
  }
}

static void startLEDs() {
  FastLED.addLeds<WS2812B, PET_LED_A_PIN, GRB>(ledsA, PET_LED_COUNT_A);
  FastLED.addLeds<WS2812B, PET_LED_B_PIN, GRB>(ledsB, PET_LED_COUNT_B);
  FastLED.setBrightness(PET_LED_BRIGHTNESS);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, 3000);
  fill_solid(ledsA, PET_LED_COUNT_A, CRGB::Black);
  fill_solid(ledsB, PET_LED_COUNT_B, CRGB::Black);
  FastLED.show();
  Serial.printf("[led] two independent strips ready: %d + %d LEDs\n",
                PET_LED_COUNT_A, PET_LED_COUNT_B);
}

static void updateLEDs() {
  uint32_t now = millis();
  if (now - lastLedFrameAt < 33) return;
  lastLedFrameAt = now;

  if (touchOverlayActive && !timeBefore(now, touchOverlayEndsAt)) {
    touchOverlayActive = false;
  }
  for (uint16_t i = 0; i < PET_LED_COUNT_A; ++i) {
    ledsA[i] = emotionPixel(baseEmotion, touchOverlayActive,
                            touchOverlayFast, i, PET_LED_COUNT_A, animationPhase);
  }
  for (uint16_t i = 0; i < PET_LED_COUNT_B; ++i) {
    // Reverse B logically so both rows animate in the same visual direction.
    ledsB[PET_LED_COUNT_B - 1 - i] = emotionPixel(
      baseEmotion, touchOverlayActive,
      touchOverlayFast, i, PET_LED_COUNT_B, animationPhase
    );
  }
  FastLED.show();
  animationPhase += touchOverlayActive && touchOverlayFast ? 10 : 3;
}

static void setEmotion(const char *name) {
  if (!name) return;
  if (!strcmp(name, "happy")) baseEmotion = Emotion::Happy;
  else if (!strcmp(name, "sad")) baseEmotion = Emotion::Sad;
  else if (!strcmp(name, "surprised")) baseEmotion = Emotion::Surprised;
  else if (!strcmp(name, "angry")) baseEmotion = Emotion::Angry;
  else if (!strcmp(name, "fearful")) baseEmotion = Emotion::Fearful;
  else if (!strcmp(name, "disgusted")) baseEmotion = Emotion::Disgusted;
  else baseEmotion = Emotion::Neutral;
  Serial.printf("[led] emotion: %s\n", name);
}

static void triggerTouchEffect(bool fast) {
  touchOverlayFast = fast;
  touchOverlayActive = true;
  touchOverlayEndsAt = millis() + (fast ? 1500 : 3000);
  Serial.printf("[touch] %s effect\n", fast ? "quick" : "slow");
}

// ------------------------------ Touch ---------------------------------

Adafruit_MPR121 touchController;
bool touchAvailable = false;
uint16_t previousTouchMask = 0;
uint32_t gestureStartedAt = 0;
uint8_t gestureTransitions = 0;
bool slowTouchFired = false;
uint32_t lastTouchPollAt = 0;

static void startTouch() {
  Wire.begin(PET_I2C_SDA_PIN, PET_I2C_SCL_PIN, 400000);
  pinMode(PET_MPR121_IRQ_PIN, INPUT_PULLUP);
  touchAvailable = touchController.begin(0x5A, &Wire, 12, 6);
  if (!touchAvailable) {
    Serial.println("[touch] MPR121 not found at address 0x5A; touch disabled");
    return;
  }
  Serial.println("[touch] MPR121 ready: electrodes E0-E8");
}

static void updateTouch() {
  if (!touchAvailable || millis() - lastTouchPollAt < 20) return;
  lastTouchPollAt = millis();

  uint16_t mask = touchController.touched() & 0x01FF;
  uint32_t now = millis();

  if (previousTouchMask == 0 && mask != 0) {
    gestureStartedAt = now;
    gestureTransitions = 1;
    slowTouchFired = false;
  } else if (mask != 0 && mask != previousTouchMask) {
    ++gestureTransitions;
  }

  if (mask != 0 && !slowTouchFired && now - gestureStartedAt >= 800) {
    triggerTouchEffect(false);
    slowTouchFired = true;
  }

  if (previousTouchMask != 0 && mask == 0) {
    uint32_t duration = now - gestureStartedAt;
    if (!slowTouchFired && (duration <= 450 ||
                            (gestureTransitions >= 3 && duration <= 1200))) {
      triggerTouchEffect(true);
    } else if (!slowTouchFired && duration >= 500) {
      triggerTouchEffect(false);
    }
  }
  previousTouchMask = mask;
}

// ----------------------- Wi-Fi and WebSocket --------------------------

WebSocketsClient webSocket;
String authorizationHeader;
bool gatewayReady = false;
uint32_t lastWifiAttemptAt = 0;

static void handleGatewayMessage(uint8_t *payload, size_t length) {
  JsonDocument document;
  DeserializationError error = deserializeJson(document, payload, length);
  if (error) {
    Serial.printf("[cloud] invalid JSON: %s\n", error.c_str());
    return;
  }

  const char *type = document["type"] | "";
  if (!strcmp(type, "ready")) {
    gatewayReady = true;
    streamAudioToCloud = true;
    Serial.println("[cloud] gateway ready; audio upload enabled");
  } else if (!strcmp(type, "emotion")) {
    setEmotion(document["emotion"] | "neutral");
  } else if (strstr(type, "error") != nullptr) {
    const char *message = document["message"] | "unknown";
    Serial.printf("[cloud] error: %s\n", message);
  }
}

static void webSocketEvent(WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      Serial.println("[cloud] WebSocket connected");
      break;
    case WStype_DISCONNECTED:
      gatewayReady = false;
      streamAudioToCloud = false;
      if (audioStream) {
        uint8_t discarded[128];
        while (xStreamBufferReceive(audioStream, discarded, sizeof(discarded), 0) > 0) {}
      }
      Serial.println("[cloud] disconnected; automatic reconnect active");
      break;
    case WStype_TEXT:
      handleGatewayMessage(payload, length);
      break;
    case WStype_ERROR:
      Serial.println("[cloud] WebSocket error");
      break;
    default:
      break;
  }
}

static void startWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(PET_WIFI_SSID, PET_WIFI_PASSWORD);
  Serial.printf("[wifi] connecting to %s", PET_WIFI_SSID);

  uint32_t startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < 20000) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[wifi] IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("[wifi] initial connection timed out; retrying in loop");
  }
}

static void maintainWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  uint32_t now = millis();
  if (now - lastWifiAttemptAt >= 5000) {
    lastWifiAttemptAt = now;
    WiFi.disconnect();
    WiFi.begin(PET_WIFI_SSID, PET_WIFI_PASSWORD);
    Serial.println("[wifi] reconnecting");
  }
}

static void startGateway() {
  authorizationHeader = "Authorization: Bearer ";
  authorizationHeader += PET_DEVICE_TOKEN;
  webSocket.setExtraHeaders(authorizationHeader.c_str());
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(3000);
  webSocket.enableHeartbeat(15000, 3000, 2);
  webSocket.begin(PET_GATEWAY_HOST, PET_GATEWAY_PORT, PET_GATEWAY_PATH, "");
  Serial.printf("[cloud] connecting to ws://%s:%d%s\n",
                PET_GATEWAY_HOST, PET_GATEWAY_PORT, PET_GATEWAY_PATH);
}

static void uploadQueuedAudio() {
  if (!gatewayReady || !webSocket.isConnected() || audioStream == nullptr) return;

  static uint8_t frame[AUDIO_FRAME_BYTES];
  // Limit each loop pass so LED/touch/WebSocket housekeeping stays responsive.
  for (uint8_t sentFrames = 0; sentFrames < 3; ++sentFrames) {
    size_t available = xStreamBufferBytesAvailable(audioStream);
    if (available < AUDIO_FRAME_BYTES) return;
    size_t received = xStreamBufferReceive(audioStream, frame, sizeof(frame), 0);
    if (received && !webSocket.sendBIN(frame, received)) {
      Serial.println("[cloud] audio send failed");
      return;
    }
  }
}

// ------------------------------- Main ---------------------------------

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\nPet Emotion Lamp - Arduino edition");

  startLEDs();
  startTouch();
  if (!startMicrophone()) {
    Serial.println("[fatal] microphone could not start");
    while (true) {
      updateLEDs();
      updateTouch();
      delay(1);
    }
  }
  startWiFi();
  startGateway();
}

void loop() {
  maintainWiFi();
  webSocket.loop();
  uploadQueuedAudio();
  updateTouch();
  updateLEDs();
  delay(1);
}
