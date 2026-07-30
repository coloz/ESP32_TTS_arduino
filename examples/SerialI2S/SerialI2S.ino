#include <Arduino.h>
#include <ESP_I2S.h>
#include <ESP32TTS.h>

// Board setting: Flash Size = 8MB. This example's partitions.csv reserves
// enough application space for either embedded voice model.
static constexpr int I2S_BCLK = 5;
static constexpr int I2S_LRCLK = 6;
static constexpr int I2S_DOUT = 7;

I2SClass i2s;
ESP32TTS tts;

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(30000);

  i2s.setPins(I2S_BCLK, I2S_LRCLK, I2S_DOUT);
  if (!i2s.begin(I2S_MODE_STD, ESP32TTS::sampleRate,
                 I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO,
                 I2S_STD_SLOT_LEFT)) {
    Serial.println("I2S initialization failed");
    return;
  }
  if (!tts.begin()) {
    Serial.printf("TTS initialization failed: %s\n", tts.lastErrorMessage());
    return;
  }

  Serial.println("请输入 UTF-8 中文并按回车：");
}

void loop() {
  if (!tts.isReady() || !Serial.available()) {
    delay(10);
    return;
  }

  String text = Serial.readStringUntil('\n');
  text.trim();
  if (text.isEmpty()) {
    return;
  }

  if (!tts.speak(text.c_str(), i2s)) {
    Serial.printf("TTS failed: %s\n", tts.lastErrorMessage());
  }
  Serial.println("请输入下一句：");
}
