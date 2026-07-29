#include <Arduino.h>
#include <ESP_I2S.h>
#include <ESP32ES8311.h>
#include <ESP32TTS.h>

// ESP32-S3 + ES8311 wiring used by this example.
static constexpr int I2S_MCLK = 46;
static constexpr int I2S_DOUT = 38;  // ESP32 output -> ES8311 DI
static constexpr int I2S_LRCLK = 2;
static constexpr int I2S_DIN = 40;   // ES8311 DO -> ESP32 input (unused here)
static constexpr int I2C_SDA = 41;
static constexpr int I2C_SCL = 42;
static constexpr int I2S_BCLK = 39;

I2SClass i2s;
// Only the board-specific I2C pins are required. The codec defaults are
// suitable for TTS: DAC, 16 kHz, 16-bit, mono, unmuted output.
ESP32ES8311 codec(I2C_SDA, I2C_SCL);
ESP32TTS tts;

static void playTestTone() {
  static const int16_t oneKhzSine[] = {
      0, 1531, 2828, 3696, 4000, 3696, 2828, 1531,
      0, -1531, -2828, -3696, -4000, -3696, -2828, -1531,
  };
  for (int i = 0; i < 250; ++i) {
    i2s.write(reinterpret_cast<const uint8_t *>(oneKhzSine),
              sizeof(oneKhzSine));
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\nESP32TTS ES8311 hardware test");
  Serial.printf("Pins: MCLK=%d BCLK=%d LRCLK=%d DOUT=%d DIN=%d SDA=%d SCL=%d\n",
                I2S_MCLK, I2S_BCLK, I2S_LRCLK, I2S_DOUT, I2S_DIN,
                I2C_SDA, I2C_SCL);
  Serial.printf("PSRAM: %lu bytes\n",
                static_cast<unsigned long>(ESP.getPsramSize()));

  // RX is intentionally disabled: TTS only sends audio to ES8311 DI.
  i2s.setPins(I2S_BCLK, I2S_LRCLK, I2S_DOUT, -1, I2S_MCLK);
  if (!i2s.begin(I2S_MODE_STD, ESP32TTS::sampleRate,
                 I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO,
                 I2S_STD_SLOT_LEFT)) {
    Serial.printf("I2S initialization failed: %d\n", i2s.lastError());
    return;
  }
  Serial.println("I2S initialized: 16 kHz / 16-bit / mono / 4.096 MHz MCLK");

  if (!codec.begin()) {
    Serial.printf("ES8311 initialization failed: %s (driver=%d)\n",
                  codec.lastErrorMessage(), codec.lastDriverError());
    return;
  }
  Serial.println("ES8311 initialized; playing a 500 ms test tone");
  playTestTone();

  if (!tts.begin()) {
    Serial.printf("TTS initialization failed: %s\n", tts.lastErrorMessage());
    return;
  }

  tts.setSpeed(3);
  Serial.println("Speaking Chinese text...");
  if (!tts.speak("这是离线语音合成功能的Arduino版本", i2s)) {
    Serial.printf("TTS failed: %s\n", tts.lastErrorMessage());
    return;
  }
  Serial.println("Speaking pinyin...");
  if (!tts.speakPinyin("da4 jia1 hao3", i2s)) {
    Serial.printf("Pinyin TTS failed: %s\n", tts.lastErrorMessage());
    return;
  }
  Serial.println("Speaking payment amount...");
  if (!tts.speakMoney(72, 1, 0, ESP32TTSPayMode::NumberOnly, i2s)) {
    Serial.printf("Money TTS failed: %s\n", tts.lastErrorMessage());
    return;
  }
  Serial.println("All ES8311 and TTS tests completed successfully");
}

void loop() {
  delay(1000);
}
