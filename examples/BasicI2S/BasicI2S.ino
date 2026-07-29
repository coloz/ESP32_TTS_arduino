#include <Arduino.h>
#include <ESP_I2S.h>
#include <ESP32TTS.h>

// Board settings: Flash Size = 8MB, Partition Scheme = Default 8MB.
// Change these pins to match your I2S DAC/amplifier (for example MAX98357A).
static constexpr int I2S_BCLK = 5;
static constexpr int I2S_LRCLK = 6;
static constexpr int I2S_DOUT = 7;

I2SClass i2s;
ESP32TTS tts;

void setup() {
  Serial.begin(115200);

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

  tts.setSpeed(3);
  if (!tts.speak("欢迎使用乐鑫离线语音合成", i2s)) {
    Serial.printf("TTS failed: %s\n", tts.lastErrorMessage());
  }
  if (!tts.speakPinyin("da4 jia1 hao3", i2s)) {
    Serial.printf("Pinyin TTS failed: %s\n", tts.lastErrorMessage());
  }
  if (!tts.speakMoney(72, 1, 0, ESP32TTSPayMode::Alipay, i2s)) {
    Serial.printf("Money TTS failed: %s\n", tts.lastErrorMessage());
  }
}

void loop() {
  delay(1000);
}
