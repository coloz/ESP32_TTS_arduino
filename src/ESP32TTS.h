#pragma once

#include <Arduino.h>
#include <atomic>
#include <stdint.h>

enum class ESP32TTSError : uint8_t {
  None = 0,
  UnsupportedTarget,
  EngineUnavailable,
  NotInitialized,
  Busy,
  InvalidArgument,
  PartitionNotFound,
  PartitionMapFailed,
  VoiceDataMissing,
  VoiceDataInvalid,
  VoiceInitFailed,
  EngineCreateFailed,
  ParseFailed,
  OutputFailed,
  Stopped,
  OutOfMemory,
};

enum class ESP32TTSPayMode : uint8_t {
  NumberOnly = 0,
  Alipay = 1,
  WeChat = 2,
};

class ESP32TTS {
public:
  using AudioOutput = size_t (*)(const int16_t *samples, size_t sampleCount, void *userData);

  static constexpr uint32_t sampleRate = 16000;
  static constexpr uint8_t bitsPerSample = 16;
  static constexpr uint8_t channels = 1;
  static constexpr uint8_t minSpeed = 0;
  static constexpr uint8_t maxSpeed = 5;
  static constexpr size_t smallVoiceDataSize = 2913777;
  static const char smallVoiceDataSha256[];
  static constexpr size_t standardVoiceDataSize = 3821311;
  static const char standardVoiceDataSha256[];

  ESP32TTS();
  ~ESP32TTS();

  ESP32TTS(const ESP32TTS &) = delete;
  ESP32TTS &operator=(const ESP32TTS &) = delete;

  // Detects and verifies either bundled voice model, then creates a TTS instance.
  bool begin(const char *partitionLabel = "voice_data");

  // Custom voice data must be protected by its exact byte length and SHA-256.
  bool begin(const char *partitionLabel, size_t voiceDataSize,
             const char *expectedSha256);
  bool end();

  bool isReady() const;
  bool isSpeaking() const;

  bool setSpeed(uint8_t speed);
  uint8_t speed() const;

  // Blocking synthesis. Output is 16 kHz, signed 16-bit, mono PCM.
  bool speak(const char *utf8Text, AudioOutput output, void *userData = nullptr);
  bool speak(const char *utf8Text, Stream &output);

  bool speakPinyin(const char *pinyin, AudioOutput output, void *userData = nullptr);
  bool speakPinyin(const char *pinyin, Stream &output);

  bool speakMoney(int yuan, int jiao, int fen, ESP32TTSPayMode mode,
                  AudioOutput output, void *userData = nullptr);
  bool speakMoney(int yuan, int jiao, int fen, ESP32TTSPayMode mode,
                  Stream &output);

  // May be called from another FreeRTOS task. The blocking speak call returns false.
  void stop();

  ESP32TTSError lastError() const;
  const char *lastErrorMessage() const;
  static const char *errorMessage(ESP32TTSError error);

private:
  struct Impl;

  bool speakText(const char *text, bool pinyin, AudioOutput output, void *userData);
  bool outputParsed(AudioOutput output, void *userData);
  bool beginOperation(AudioOutput output);
  void finishOperation();
  void releaseResources();
  void setError(ESP32TTSError error);

  static size_t streamOutput(const int16_t *samples, size_t sampleCount, void *userData);

  Impl *_impl;
  std::atomic<ESP32TTSError> _lastError;
  std::atomic<uint8_t> _speed;
  std::atomic<bool> _ready;
  std::atomic<bool> _busy;
  std::atomic<bool> _speaking;
  std::atomic<bool> _stopRequested;
};
