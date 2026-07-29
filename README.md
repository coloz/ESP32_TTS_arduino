# ESP32 TTS Arduino Library

English | [简体中文](README.md)

An offline Chinese text-to-speech Arduino wrapper for the ESP32-S3. It uses
the Espressif ESP-SR TTS engine bundled with Arduino-ESP32, accepts UTF-8
Chinese text, and streams 16 kHz, signed 16-bit, mono PCM. The library can
write directly to an Arduino `Stream` such as `I2SClass`, or to a custom PCM
callback. It also supports pinyin input, payment announcements, speech speeds
from 0 to 5, and cancellation from another task.

> **Language note:** this is the English documentation for the library. The
> current on-device ESP-SR speech synthesizer generates Chinese speech only.

## Requirements

- An ESP32-S3 with at least 8 MB of flash.
- Arduino-ESP32 3.3.8 or a newer 3.x release. The library has been fully
  compiled and linked against 3.3.8 and 3.3.11-cn.
- An I2S DAC/amplifier such as the MAX98357A, or a custom audio output that
  accepts PCM samples.
- UTF-8 source files when passing Chinese text literals.

The `BasicI2S_ES8311` example additionally uses the standalone
`esp32_es8311` sibling library. That library controls the codec while TTS PCM
continues to flow through Arduino `I2SClass`.

Arduino-ESP32 3.3.x already provides `esp_tts_chinese`, `voice_set_xiaole`,
and their headers, so this library does not duplicate those precompiled
components. Following Espressif's recommended layout, the voice set is stored
in a separate `voice_data` partition instead of consuming application space.

## First-time setup

1. Install this library in Arduino IDE, select an ESP32-S3 board, set
   **Flash Size** to 8 MB or more, and select **Default 8MB** for
   **Partition Scheme**.
2. Open `File > Examples > ESP32TTS > BasicI2S`. Change the BCLK, LRCLK, and
   DOUT pins to match your hardware. Arduino automatically uses the
   `partitions.csv` included in the example directory.
3. Upload the example once so the custom partition table is written.
4. Flash the voice data once. Repeat this step only after erasing the entire
   flash or changing the voice set.

   Windows PowerShell:

   ```powershell
   py -m pip install esptool
   py tools/flash_voice.py --port COM5
   ```

   Linux/macOS:

   ```bash
   python3 -m pip install esptool
   python3 tools/flash_voice.py --port /dev/ttyUSB0
   ```

   When the library is installed through Arduino IDE, `tools` is inside the
   installed library directory. The default layout places the 3 MB
   `voice_data` partition at `0x410000`. Before invoking esptool, the script
   reads the partition CSV, checks that the model fits, and verifies the
   SHA-256 of the bundled official voice file.

   A custom voice file must include its expected digest so a damaged or
   accidentally selected file cannot be flashed:

   ```bash
   python3 tools/flash_voice.py --port /dev/ttyUSB0 \
     --model path/to/voice.dat --sha256 <64-character-SHA-256>
   ```

   The tool prints the matching `tts.begin()` validation arguments.

Normal Arduino uploads do not overwrite `voice_data`. If **Erase All Flash**
is enabled, flash the voice data again afterward.

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
    Serial.println("I2S initialization failed");
    return;
  }

  if (!tts.begin()) {
    Serial.println(tts.lastErrorMessage());
    return;
  }

  tts.setSpeed(3);
  if (!tts.speak("欢迎使用离线语音合成", i2s)) {
    Serial.println(tts.lastErrorMessage());
  }
}

void loop() {}
```

`speak()` is blocking. To cancel it, call `stop()` from another FreeRTOS task.
Cancellation takes effect between generated PCM blocks or after the current
output callback returns, so callbacks must not block indefinitely. The
destructor requests cancellation and waits for active synthesis to exit. Do
not destroy an `ESP32TTS` object from inside its PCM callback.

## API overview

- `begin("voice_data")`: verify the bundled voice file's exact length and
  SHA-256, map the partition, and create the synthesizer.
- `begin(label, size, sha256)`: validate a custom voice file using its exact
  length and SHA-256.
- `speak(text, stream/callback)`: synthesize UTF-8 Chinese text.
- `speakPinyin("da4 jia1 hao3", ...)`: synthesize numbered-tone pinyin.
- `speakMoney(yuan, jiao, fen, mode, ...)`: announce an amount, optionally
  prefixed with Alipay or WeChat payment wording.
- `setSpeed(0..5)`: set the speech speed; 0 is slowest, 5 is fastest, and 3 is
  the default.
- `stop()`: request cancellation of an active blocking synthesis operation.
- `lastError()` / `lastErrorMessage()`: retrieve the most recent error.
- `end()`: destroy the engine and voice set, then release the flash mapping;
  it returns `false`/`Busy` while synthesis is active.

The PCM callback has the following signature. Return the number of samples
consumed. Returning 0 aborts synthesis with `OutputFailed`.

```cpp
size_t output(const int16_t *samples, size_t sampleCount, void *userData);
```

The callback may consume only part of a block. The library calls it again with
the unconsumed samples until the complete block has been handled.

## Partition layout

The example partition table targets devices with 8 MB or more of flash. It
allocates 4 MB to the application, 3 MB to voice data, and 960 KB to SPIFFS.
The CSV in the example controls the actual layout, but Arduino's application
size check still comes from the board menu. With Arduino-ESP32 3.3.11-cn,
select **Default 8MB**: changing only Flash Size leaves the default 1,310,720
byte application limit, while the **Custom** menu entry exposes an unsafe
16 MB limit. The equivalent Arduino CLI option is:

```text
--fqbn "esp32:esp32:esp32s3:FlashSize=8M,PartitionScheme=default_8MB"
```

If you use a custom table, its partition label must match the value passed to
`begin()`, and the voice model must fit in the partition.

When changing the partition offset, pass the same CSV to the flashing tool:

```bash
python3 tools/flash_voice.py --port /dev/ttyUSB0 \
  --partitions path/to/partitions.csv
```

You may override the detected offset explicitly with `--offset`, but using the
same CSV as the sketch is safer.

## Design basis and limitations

The implementation follows the current ESP-SR TTS documentation and the
`esp-skainet/examples/chinese_tts` workflow: map the voice partition, call
`esp_tts_voice_set_init()` and `esp_tts_create()`, then repeatedly consume PCM
blocks from `esp_tts_stream_play()`. The bundled voice file is pinned to a
specific ESP-SR commit; see `extras/voice_data/README.md` for its source,
checksum context, and license.

- The synthesizer currently supports Chinese only.
- Text must be encoded as UTF-8.
- Output is always 16 kHz, signed 16-bit, mono PCM.
- Synthesis is streaming and blocking; use a separate FreeRTOS task if the
  application must remain responsive.
- Hardware audio playback has to be configured by the sketch or implemented
  in the PCM callback.

References:

- [ESP-SR TTS documentation](https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/speech_synthesis/readme.html)
- [Official ESP-Skainet chinese_tts example](https://github.com/espressif/esp-skainet/tree/master/examples/chinese_tts)
- [Arduino-ESP32 custom partition documentation](https://docs.espressif.com/projects/arduino-esp32/en/latest/tutorials/partition_table.html)
