# ESP32 TTS Arduino Library

English | [简体中文](README_CN.md)

An offline Chinese text-to-speech Arduino wrapper for ESP32-S3. It uses the
ESP-SR TTS engine bundled with Arduino-ESP32 and streams 16 kHz, signed 16-bit,
mono PCM to an Arduino `Stream` such as `I2SClass`, or to a custom callback.
Pinyin input, payment announcements, speeds from 0 to 5, and cross-task
cancellation are supported.

The voice model is compiled directly into the application firmware. A separate
`voice_data` partition and `.dat` upload are no longer required. The 2.78 MiB
small model is selected by default; a macro selects the 3.64 MiB standard
model. Only the selected model is linked into the final firmware.

## Requirements

- ESP32-S3 with at least 8 MB of flash.
- Arduino-ESP32 3.3.8 or a newer 3.x release.
- An I2S DAC/amplifier such as MAX98357A, or a custom PCM output.
- The `BasicI2S_ES8311` example also requires the sibling `esp32_es8311`
  library.

## Setup

1. Install this library, select an ESP32-S3 board, and set **Flash Size** to
   8 MB or more.
2. Open `File > Examples > ESP32TTS > BasicI2S` and adjust the BCLK, LRCLK, and
   DOUT pins.
3. Compile and upload normally. The example's `partitions.csv` reserves enough
   application space for either embedded model. There is no separate voice
   data flashing step.

## Selecting the voice model

No configuration is needed for the default small model:

```cpp
#include <ESP32TTS.h>
```

To select the standard model, define the macro before including the header:

```cpp
#define ESP32_TTS_USE_STANDARD_VOICE 1
#include <ESP32TTS.h>
```

The build system may define `ESP32_TTS_USE_STANDARD_VOICE=1` instead. The macro
accepts only `0` or `1`. The models are separate members of a static archive,
so the linker extracts only the selected member.

## Minimal example

```cpp
#include <ESP_I2S.h>
#include <ESP32TTS.h>

I2SClass i2s;
ESP32TTS tts;

void setup() {
  Serial.begin(115200);
  i2s.setPins(5, 6, 7); // BCLK, LRCLK, DOUT
  if (!i2s.begin(I2S_MODE_STD, ESP32TTS::sampleRate,
                 I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO,
                 I2S_STD_SLOT_LEFT)) {
    return;
  }

  if (!tts.begin()) {
    Serial.println(tts.lastErrorMessage());
    return;
  }

  tts.setSpeed(3);
  tts.speak("欢迎使用离线语音合成", i2s);
}

void loop() {}
```

`speak()` is blocking. Call `stop()` from another FreeRTOS task to cancel it.
Do not destroy an `ESP32TTS` object from inside its PCM callback.

## API overview

- `begin()`: create the synthesizer from the embedded small or standard model.
- `speak(text, stream/callback)`: synthesize UTF-8 Chinese text.
- `speakPinyin("da4 jia1 hao3", ...)`: synthesize numbered-tone pinyin.
- `speakMoney(yuan, jiao, fen, mode, ...)`: announce a payment amount.
- `setSpeed(0..5)`: set speed; 0 is slowest, 5 fastest, and 3 the default.
- `stop()`: request cancellation of active blocking synthesis.
- `lastError()` / `lastErrorMessage()`: retrieve the latest error.
- `end()`: release the engine and voice set; returns `false`/`Busy` while busy.

For compatibility, `begin(partitionLabel)` and
`begin(partitionLabel, size, sha256)` can still load external partition data.
New applications normally only need the parameterless `begin()`.

The PCM callback returns the number of consumed samples. Returning 0 aborts
synthesis with `OutputFailed`:

```cpp
size_t output(const int16_t *samples, size_t sampleCount, void *userData);
```

## Flash and partitions

The tested `BasicI2S` firmware is about 3.15 MiB with the small model and
4.03 MiB with the standard model, so common 1–3 MB application partitions are
too small. Each example includes an 8 MB `partitions.csv` with a 6.6875 MiB
application partition and 1.25 MiB SPIFFS. The same layout is available as
`extras/partitions/tts_8mb_embedded.csv`.

Changing the model only requires rebuilding and uploading the firmware.
Erasing all flash does not require a separate voice-data recovery step.

## Rebuilding the voice archive

`src/esp32s3/libESP32TTSVoice.a` is generated from the two official `.dat`
files in `extras/voice_data`. After replacing either model, rebuild it with an
ESP32-S3 GCC toolchain:

```bash
python3 tools/build_voice_archive.py --toolchain path/to/toolchain/bin
```

See `extras/voice_data/README.md` for sources, checksums, and licensing.

## Limitations

- The current on-device synthesizer supports Chinese only; sources must be
  UTF-8.
- Output is fixed at 16 kHz, signed 16-bit, mono PCM.
- Synthesis is streaming and blocking; use a separate FreeRTOS task when the
  application must remain responsive.
- The library supports ESP32-S3 and depends on ESP-SR TTS components bundled
  with Arduino-ESP32.
