#include "ESP32TTS.h"

#include <cstring>
#include <new>

#if defined(ARDUINO_ARCH_ESP32) && defined(CONFIG_IDF_TARGET_ESP32S3) && \
    defined(__has_include)
#if __has_include("esp_tts.h") && __has_include("esp_tts_voice_template.h")
#define ESP32_TTS_ENGINE_AVAILABLE 1
#endif
#endif

#ifndef ESP32_TTS_ENGINE_AVAILABLE
#define ESP32_TTS_ENGINE_AVAILABLE 0
#endif

#if ESP32_TTS_ENGINE_AVAILABLE
#include "esp_partition.h"
#include "esp_tts.h"
#include "esp_tts_voice_template.h"
#include "mbedtls/sha256.h"
#endif

constexpr uint32_t ESP32TTS::sampleRate;
constexpr uint8_t ESP32TTS::bitsPerSample;
constexpr uint8_t ESP32TTS::channels;
constexpr uint8_t ESP32TTS::minSpeed;
constexpr uint8_t ESP32TTS::maxSpeed;
constexpr size_t ESP32TTS::bundledVoiceDataSize;
const char ESP32TTS::bundledVoiceDataSha256[] =
    "cc9a81fd716b3c07fae3ca2f802dc026081896f2e34db9b9db117d4de5a85c01";

namespace {

int hexNibble(char value) {
  if (value >= '0' && value <= '9') {
    return value - '0';
  }
  if (value >= 'a' && value <= 'f') {
    return value - 'a' + 10;
  }
  if (value >= 'A' && value <= 'F') {
    return value - 'A' + 10;
  }
  return -1;
}

bool parseSha256(const char *text, uint8_t output[32]) {
  if (text == nullptr || std::strlen(text) != 64) {
    return false;
  }
  for (size_t i = 0; i < 32; ++i) {
    const int high = hexNibble(text[i * 2]);
    const int low = hexNibble(text[i * 2 + 1]);
    if (high < 0 || low < 0) {
      return false;
    }
    output[i] = static_cast<uint8_t>((high << 4) | low);
  }
  return true;
}

#if ESP32_TTS_ENGINE_AVAILABLE
bool voiceDataMatches(const uint8_t *data, size_t size,
                      const uint8_t expected[32]) {
  uint8_t actual[32];
  if (mbedtls_sha256(data, size, actual, 0) != 0) {
    return false;
  }

  uint8_t difference = 0;
  for (size_t i = 0; i < sizeof(actual); ++i) {
    difference |= actual[i] ^ expected[i];
  }
  return difference == 0;
}
#endif

} // namespace

struct ESP32TTS::Impl {
#if ESP32_TTS_ENGINE_AVAILABLE
  esp_tts_voice_t *voice = nullptr;
  esp_tts_handle_t handle = nullptr;
  esp_partition_mmap_handle_t mmapHandle = 0;
  bool mapped = false;
#endif
};

ESP32TTS::ESP32TTS()
    : _impl(nullptr), _lastError(ESP32TTSError::None), _speed(3),
      _ready(false), _busy(false), _speaking(false), _stopRequested(false) {}

ESP32TTS::~ESP32TTS() {
  // Take exclusive ownership before releasing resources. An active synthesis
  // observes the stop request and clears _busy after its output callback
  // returns. Keeping the lock set prevents a new operation from starting.
  for (;;) {
    _stopRequested.store(true);
    bool expected = false;
    if (_busy.compare_exchange_weak(expected, true)) {
      break;
    }
    delay(1);
  }
  releaseResources();
}

bool ESP32TTS::begin(const char *partitionLabel) {
  return begin(partitionLabel, bundledVoiceDataSize, bundledVoiceDataSha256);
}

bool ESP32TTS::begin(const char *partitionLabel, size_t voiceDataSize,
                     const char *expectedSha256) {
  uint8_t expectedDigest[32];
  if (partitionLabel == nullptr || partitionLabel[0] == '\0' ||
      voiceDataSize == 0 || !parseSha256(expectedSha256, expectedDigest)) {
    setError(ESP32TTSError::InvalidArgument);
    return false;
  }

  bool expectedIdle = false;
  if (!_busy.compare_exchange_strong(expectedIdle, true)) {
    setError(ESP32TTSError::Busy);
    return false;
  }
  _stopRequested.store(false);
  releaseResources();

#if !defined(ARDUINO_ARCH_ESP32) || !defined(CONFIG_IDF_TARGET_ESP32S3)
  setError(ESP32TTSError::UnsupportedTarget);
  finishOperation();
  return false;
#elif !ESP32_TTS_ENGINE_AVAILABLE
  setError(ESP32TTSError::EngineUnavailable);
  finishOperation();
  return false;
#else
  Impl *impl = new (std::nothrow) Impl();
  if (impl == nullptr) {
    setError(ESP32TTSError::OutOfMemory);
    finishOperation();
    return false;
  }

  const esp_partition_t *partition = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, partitionLabel);
  if (partition == nullptr) {
    delete impl;
    setError(ESP32TTSError::PartitionNotFound);
    finishOperation();
    return false;
  }

  if (voiceDataSize > partition->size) {
    delete impl;
    setError(ESP32TTSError::VoiceDataInvalid);
    finishOperation();
    return false;
  }

  const void *voiceData = nullptr;
  const esp_err_t mapResult = esp_partition_mmap(
      partition, 0, partition->size, ESP_PARTITION_MMAP_DATA, &voiceData,
      &impl->mmapHandle);
  if (mapResult != ESP_OK || voiceData == nullptr) {
    delete impl;
    setError(ESP32TTSError::PartitionMapFailed);
    finishOperation();
    return false;
  }
  impl->mapped = true;

  // An erased data partition is all 0xff (and some test images are all zero).
  // Reject both before passing an invalid image to the binary TTS engine.
  const uint8_t *voiceBytes = static_cast<const uint8_t *>(voiceData);
  bool allErased = true;
  bool allZero = true;
  const size_t probeSize = partition->size < 64 ? partition->size : 64;
  for (size_t i = 0; i < probeSize; ++i) {
    allErased = allErased && voiceBytes[i] == 0xff;
    allZero = allZero && voiceBytes[i] == 0x00;
  }
  if (probeSize < 64 || allErased || allZero) {
    esp_partition_munmap(impl->mmapHandle);
    delete impl;
    setError(ESP32TTSError::VoiceDataMissing);
    finishOperation();
    return false;
  }

  if (!voiceDataMatches(voiceBytes, voiceDataSize, expectedDigest)) {
    esp_partition_munmap(impl->mmapHandle);
    delete impl;
    setError(ESP32TTSError::VoiceDataInvalid);
    finishOperation();
    return false;
  }

  impl->voice = esp_tts_voice_set_init(
      &esp_tts_voice_template, const_cast<void *>(voiceData));
  if (impl->voice == nullptr) {
    esp_partition_munmap(impl->mmapHandle);
    delete impl;
    setError(ESP32TTSError::VoiceInitFailed);
    finishOperation();
    return false;
  }

  impl->handle = esp_tts_create(impl->voice);
  if (impl->handle == nullptr) {
    esp_tts_voice_set_free(impl->voice);
    esp_partition_munmap(impl->mmapHandle);
    delete impl;
    setError(ESP32TTSError::EngineCreateFailed);
    finishOperation();
    return false;
  }

  _impl = impl;
  _ready.store(true);
  setError(ESP32TTSError::None);
  finishOperation();
  return true;
#endif
}

bool ESP32TTS::end() {
  bool expectedIdle = false;
  if (!_busy.compare_exchange_strong(expectedIdle, true)) {
    setError(ESP32TTSError::Busy);
    return false;
  }
  _stopRequested.store(false);
  releaseResources();
  setError(ESP32TTSError::None);
  finishOperation();
  return true;
}

void ESP32TTS::releaseResources() {
  _ready.store(false);

#if ESP32_TTS_ENGINE_AVAILABLE
  if (_impl != nullptr) {
    if (_impl->handle != nullptr) {
      esp_tts_destroy(_impl->handle);
      _impl->handle = nullptr;
    }
    if (_impl->voice != nullptr) {
      esp_tts_voice_set_free(_impl->voice);
      _impl->voice = nullptr;
    }
    if (_impl->mapped) {
      esp_partition_munmap(_impl->mmapHandle);
      _impl->mapped = false;
    }
  }
#endif

  delete _impl;
  _impl = nullptr;
}

bool ESP32TTS::isReady() const { return _ready.load(); }

bool ESP32TTS::isSpeaking() const { return _speaking.load(); }

bool ESP32TTS::setSpeed(uint8_t requestedSpeed) {
  if (requestedSpeed < minSpeed || requestedSpeed > maxSpeed) {
    setError(ESP32TTSError::InvalidArgument);
    return false;
  }
  _speed.store(requestedSpeed);
  setError(ESP32TTSError::None);
  return true;
}

uint8_t ESP32TTS::speed() const { return _speed.load(); }

bool ESP32TTS::speak(const char *utf8Text, AudioOutput output, void *userData) {
  return speakText(utf8Text, false, output, userData);
}

bool ESP32TTS::speak(const char *utf8Text, Stream &output) {
  return speak(utf8Text, streamOutput, &output);
}

bool ESP32TTS::speakPinyin(const char *pinyin, AudioOutput output,
                           void *userData) {
  if (pinyin == nullptr || pinyin[0] == '\0') {
    setError(ESP32TTSError::InvalidArgument);
    return false;
  }
  if (!beginOperation(output)) {
    return false;
  }

#if ESP32_TTS_ENGINE_AVAILABLE
  const char *cursor = pinyin;
  bool hasSyllable = false;
  while (*cursor != '\0') {
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
           *cursor == '\n') {
      ++cursor;
    }
    if (*cursor == '\0') {
      break;
    }

    // The binary ESP-SR parser in some Arduino-ESP32 releases treats a
    // whitespace-separated phrase as one dictionary item and asserts when it
    // cannot find that item. Feed it one documented tone-number syllable at a
    // time instead. "zhuang4" and "chuang2" are the longest standard forms.
    char syllable[8];
    size_t length = 0;
    while (*cursor != '\0' && *cursor != ' ' && *cursor != '\t' &&
           *cursor != '\r' && *cursor != '\n') {
      if (length + 1 >= sizeof(syllable) ||
          !((*cursor >= 'a' && *cursor <= 'z') ||
            (*cursor >= '0' && *cursor <= '5'))) {
        esp_tts_stream_reset(_impl->handle);
        setError(ESP32TTSError::InvalidArgument);
        finishOperation();
        return false;
      }
      syllable[length++] = *cursor++;
    }
    if (length < 2 || syllable[length - 1] < '0' ||
        syllable[length - 1] > '5') {
      esp_tts_stream_reset(_impl->handle);
      setError(ESP32TTSError::InvalidArgument);
      finishOperation();
      return false;
    }
    syllable[length] = '\0';

    if (!esp_tts_parse_pinyin(_impl->handle, syllable)) {
      esp_tts_stream_reset(_impl->handle);
      setError(ESP32TTSError::ParseFailed);
      finishOperation();
      return false;
    }
    hasSyllable = true;
    const bool result = outputParsed(output, userData);
    esp_tts_stream_reset(_impl->handle);
    if (!result) {
      finishOperation();
      return false;
    }
  }

  if (!hasSyllable) {
    setError(ESP32TTSError::InvalidArgument);
    finishOperation();
    return false;
  }
  setError(ESP32TTSError::None);
  finishOperation();
  return true;
#else
  (void)userData;
  finishOperation();
  return false;
#endif
}

bool ESP32TTS::speakPinyin(const char *pinyin, Stream &output) {
  return speakPinyin(pinyin, streamOutput, &output);
}

bool ESP32TTS::speakMoney(int yuan, int jiao, int fen, ESP32TTSPayMode mode,
                          AudioOutput output, void *userData) {
  if (yuan < 0 || jiao < 0 || jiao > 9 || fen < 0 || fen > 9 ||
      static_cast<uint8_t>(mode) > static_cast<uint8_t>(ESP32TTSPayMode::WeChat)) {
    setError(ESP32TTSError::InvalidArgument);
    return false;
  }
  if (!beginOperation(output)) {
    return false;
  }

#if ESP32_TTS_ENGINE_AVAILABLE
  const int parsed = esp_tts_parse_money(
      _impl->handle, yuan, jiao, fen,
      static_cast<pay_mode_t>(static_cast<uint8_t>(mode)));
  if (!parsed) {
    esp_tts_stream_reset(_impl->handle);
    setError(ESP32TTSError::ParseFailed);
    finishOperation();
    return false;
  }

  const bool result = outputParsed(output, userData);
  esp_tts_stream_reset(_impl->handle);
  finishOperation();
  return result;
#else
  (void)yuan;
  (void)jiao;
  (void)fen;
  (void)mode;
  (void)userData;
  finishOperation();
  return false;
#endif
}

bool ESP32TTS::speakMoney(int yuan, int jiao, int fen, ESP32TTSPayMode mode,
                          Stream &output) {
  return speakMoney(yuan, jiao, fen, mode, streamOutput, &output);
}

void ESP32TTS::stop() {
  if (_speaking.load()) {
    _stopRequested.store(true);
  }
}

ESP32TTSError ESP32TTS::lastError() const { return _lastError.load(); }

const char *ESP32TTS::lastErrorMessage() const {
  return errorMessage(lastError());
}

const char *ESP32TTS::errorMessage(ESP32TTSError error) {
  switch (error) {
    case ESP32TTSError::None:
      return "no error";
    case ESP32TTSError::UnsupportedTarget:
      return "ESP32 TTS currently supports ESP32-S3 only";
    case ESP32TTSError::EngineUnavailable:
      return "ESP-SR TTS is unavailable; install a current Arduino-ESP32 3.x core";
    case ESP32TTSError::NotInitialized:
      return "TTS is not initialized";
    case ESP32TTSError::Busy:
      return "TTS is already speaking";
    case ESP32TTSError::InvalidArgument:
      return "invalid argument";
    case ESP32TTSError::PartitionNotFound:
      return "voice_data partition was not found";
    case ESP32TTSError::PartitionMapFailed:
      return "voice_data partition could not be mapped";
    case ESP32TTSError::VoiceDataMissing:
      return "voice_data partition is empty; flash the bundled voice model";
    case ESP32TTSError::VoiceDataInvalid:
      return "voice data size or SHA-256 does not match the expected model";
    case ESP32TTSError::VoiceInitFailed:
      return "voice data could not be initialized";
    case ESP32TTSError::EngineCreateFailed:
      return "TTS engine could not be created";
    case ESP32TTSError::ParseFailed:
      return "text could not be parsed by the Chinese TTS engine";
    case ESP32TTSError::OutputFailed:
      return "PCM output stopped accepting samples";
    case ESP32TTSError::Stopped:
      return "speech synthesis was stopped";
    case ESP32TTSError::OutOfMemory:
      return "out of memory";
    default:
      return "unknown error";
  }
}

bool ESP32TTS::speakText(const char *text, bool pinyin, AudioOutput output,
                         void *userData) {
  if (text == nullptr || text[0] == '\0') {
    setError(ESP32TTSError::InvalidArgument);
    return false;
  }
  if (!beginOperation(output)) {
    return false;
  }

#if ESP32_TTS_ENGINE_AVAILABLE
  const int parsed = pinyin ? esp_tts_parse_pinyin(_impl->handle, text)
                            : esp_tts_parse_chinese(_impl->handle, text);
  if (!parsed) {
    esp_tts_stream_reset(_impl->handle);
    setError(ESP32TTSError::ParseFailed);
    finishOperation();
    return false;
  }

  const bool result = outputParsed(output, userData);
  esp_tts_stream_reset(_impl->handle);
  finishOperation();
  return result;
#else
  (void)pinyin;
  (void)userData;
  finishOperation();
  return false;
#endif
}

bool ESP32TTS::outputParsed(AudioOutput output, void *userData) {
#if ESP32_TTS_ENGINE_AVAILABLE
  while (!_stopRequested.load()) {
    int sampleCount = 0;
    short *pcm = esp_tts_stream_play(_impl->handle, &sampleCount, _speed.load());
    if (sampleCount <= 0) {
      setError(ESP32TTSError::None);
      return true;
    }
    if (pcm == nullptr) {
      setError(ESP32TTSError::OutputFailed);
      return false;
    }

    size_t consumed = 0;
    const size_t total = static_cast<size_t>(sampleCount);
    while (consumed < total && !_stopRequested.load()) {
      const size_t accepted = output(pcm + consumed, total - consumed, userData);
      if (accepted == 0 || accepted > total - consumed) {
        setError(ESP32TTSError::OutputFailed);
        return false;
      }
      consumed += accepted;
    }
  }
  setError(ESP32TTSError::Stopped);
  return false;
#else
  (void)output;
  (void)userData;
  return false;
#endif
}

bool ESP32TTS::beginOperation(AudioOutput output) {
  if (output == nullptr) {
    setError(ESP32TTSError::InvalidArgument);
    return false;
  }

  bool expected = false;
  if (!_busy.compare_exchange_strong(expected, true)) {
    setError(ESP32TTSError::Busy);
    return false;
  }
  _stopRequested.store(false);
  _speaking.store(true);

  if (!_ready.load()) {
#if !defined(ARDUINO_ARCH_ESP32) || !defined(CONFIG_IDF_TARGET_ESP32S3)
    setError(ESP32TTSError::UnsupportedTarget);
#elif !ESP32_TTS_ENGINE_AVAILABLE
    setError(ESP32TTSError::EngineUnavailable);
#else
    setError(ESP32TTSError::NotInitialized);
#endif
    finishOperation();
    return false;
  }
  setError(ESP32TTSError::None);
  return true;
}

void ESP32TTS::finishOperation() {
  _speaking.store(false);
  _stopRequested.store(false);
  _busy.store(false);
}

void ESP32TTS::setError(ESP32TTSError error) { _lastError.store(error); }

size_t ESP32TTS::streamOutput(const int16_t *samples, size_t sampleCount,
                              void *userData) {
  if (samples == nullptr || userData == nullptr || sampleCount == 0) {
    return 0;
  }

  Stream *stream = static_cast<Stream *>(userData);
  const uint8_t *bytes = reinterpret_cast<const uint8_t *>(samples);
  const size_t byteCount = sampleCount * sizeof(int16_t);
  size_t written = 0;
  while (written < byteCount) {
    const size_t count = stream->write(bytes + written, byteCount - written);
    if (count == 0 || count > byteCount - written) {
      return 0;
    }
    written += count;
  }
  return sampleCount;
}
