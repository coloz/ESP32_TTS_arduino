# ESP32 TTS Arduino 库

[English](README_EN.md) | 简体中文

这是一个面向 ESP32-S3 的离线中文 TTS Arduino 封装。底层使用 Arduino-ESP32
随附的乐鑫 ESP-SR TTS 引擎，输入 UTF-8 中文，流式输出 16 kHz、16-bit、单声道
PCM。库支持直接写入 Arduino `Stream`（包括 `I2SClass`）、PCM 回调、拼音输入、
金额播报、0～5 档语速和跨任务停止。

## 环境要求

- ESP32-S3，Flash 至少 8 MB。
- Arduino-ESP32 3.3.8 或更新的 3.x 版本（已在 3.3.8 和 3.3.11-cn 上完整编译验证）。
- I2S DAC/功放（例如 MAX98357A）或能接收 PCM 的自定义音频输出。
- 当前 ESP-SR 设备端合成只支持中文。

使用 `BasicI2S_ES8311` 示例时，另行安装同级目录中的 `esp32_es8311` 库。该库只
管理 ES8311 控制面，TTS PCM 仍通过 Arduino `I2SClass` 输出。

Arduino-ESP32 3.3.x 已包含 `esp_tts_chinese`、`voice_set_xiaole` 以及对应头文件，
本库不会重复打包这些预编译库。音色数据使用官方推荐的独立 `voice_data` 分区，
避免占用应用分区。

## 首次使用

1. 在 Arduino IDE 中安装本库，选择 ESP32-S3 开发板，把 **Flash Size** 设为
   8 MB 或更大，并把 **Partition Scheme** 设为 **Default 8MB**。
2. 打开 `File > Examples > ESP32TTS > BasicI2S`，按硬件修改 BCLK、LRCLK 和
   DOUT 引脚。示例目录中的 `partitions.csv` 会被 Arduino 构建系统自动采用。
3. 正常上传一次示例，使自定义分区表生效。
4. 只在首次使用、完全擦除 Flash 或更换声音集后，烧录一次声音数据：

   ```powershell
   py -m pip install esptool
   py tools/flash_voice.py --port COM5
   ```

   Linux/macOS 示例：

   ```bash
   python3 -m pip install esptool
   python3 tools/flash_voice.py --port /dev/ttyUSB0
   ```

   如果库由 Arduino IDE 安装，`tools` 位于该库的安装目录。仓库中的默认分区把
   `voice_data` 放在 `0x410000`，容量为 3 MB。脚本会读取 CSV、检查容量和官方
   文件的 SHA-256 后再调用 esptool。

   使用自定义声音文件时必须显式提供预期摘要，防止损坏或误选的文件被烧录：

   ```bash
   python3 tools/flash_voice.py --port /dev/ttyUSB0 \
     --model path/to/voice.dat --sha256 <64位SHA-256>
   ```

   脚本会输出与该文件匹配的 `tts.begin()` 调用参数。

之后普通的 Arduino“上传”不会覆盖 `voice_data`；如果选择了“Erase All Flash”，
需要重新执行第 4 步。

## 最小示例

```cpp
#include <ESP_I2S.h>
#include <ESP32TTS.h>

I2SClass i2s;
ESP32TTS tts;

void setup() {
  i2s.setPins(5, 6, 7); // BCLK, LRCLK, DOUT
  i2s.begin(I2S_MODE_STD, ESP32TTS::sampleRate,
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO,
            I2S_STD_SLOT_LEFT);

  if (!tts.begin()) {
    Serial.println(tts.lastErrorMessage());
    return;
  }

  tts.setSpeed(3);
  tts.speak("欢迎使用离线语音合成", i2s);
}

void loop() {}
```

`speak()` 是阻塞调用。需要中止时，可从另一个 FreeRTOS 任务调用 `stop()`。
停止会在当前 PCM 输出回调返回后生效，因此回调不应无限阻塞。析构函数会请求停止并
等待活动合成退出；不要在 PCM 回调内部销毁 `ESP32TTS` 对象。

## API 摘要

- `begin("voice_data")`：校验随库声音文件的长度与 SHA-256，映射分区并创建合成器。
- `begin(label, size, sha256)`：使用显式长度和 SHA-256 校验自定义声音文件。
- `speak(text, stream/callback)`：合成 UTF-8 中文。
- `speakPinyin("da4 jia1 hao3", ...)`：直接合成带声调数字的拼音。
- `speakMoney(yuan, jiao, fen, mode, ...)`：合成金额，可选择纯数字、支付宝或微信前缀。
- `setSpeed(0..5)`：0 最慢，5 最快，默认 3。
- `lastError()` / `lastErrorMessage()`：获取最近错误。
- `end()`：释放 TTS、声音集和 Flash 映射；正在合成时返回 `false`/`Busy`。

回调签名如下。返回已消费的采样数；返回 0 会终止合成并报告 `OutputFailed`。

```cpp
size_t output(const int16_t *samples, size_t sampleCount, void *userData);
```

## 分区说明

示例分区表面向 8 MB 或更大 Flash：4 MB 应用、3 MB 声音数据和 960 KB SPIFFS。
示例目录的 CSV 决定实际分区，但 Arduino 的程序容量检查仍来自开发板菜单。在
Arduino-ESP32 3.3.11-cn 中，请选择 **Default 8MB**；只设置 Flash Size 会继续使用
1,310,720 字节的默认程序上限，而 **Custom** 菜单值会给出过大的 16 MB 上限。
Arduino CLI 对应参数为：

```text
--fqbn "esp32:esp32:esp32s3:FlashSize=8M,PartitionScheme=default_8MB"
```

若使用自定义分区表，分区名称必须与 `begin()` 参数相同，且声音数据文件必须能
完整放入分区。改变偏移后，烧录脚本必须使用同一份 CSV：

```bash
python3 tools/flash_voice.py --port /dev/ttyUSB0 \
  --partitions path/to/partitions.csv
```

## 依据与限制

实现遵循乐鑫 ESP-SR 最新 TTS 文档和 `esp-skainet/examples/chinese_tts` 的流程：
映射声音分区、调用 `esp_tts_voice_set_init()` / `esp_tts_create()`，随后循环读取
`esp_tts_stream_play()` 的 PCM 块。声音文件来自 ESP-SR 仓库中固定提交，来源与
许可见 `extras/voice_data/README.md`。

- [ESP-SR TTS 语音合成文档](https://docs.espressif.com/projects/esp-sr/zh_CN/latest/esp32s3/speech_synthesis/readme.html)
- [ESP-Skainet chinese_tts 官方示例](https://github.com/espressif/esp-skainet/tree/master/examples/chinese_tts)
- [Arduino-ESP32 自定义分区表文档](https://docs.espressif.com/projects/arduino-esp32/en/latest/tutorials/partition_table.html)
