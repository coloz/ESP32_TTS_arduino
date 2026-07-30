# ESP32 TTS Arduino 库

[English](README.md) | 简体中文

这是一个面向 ESP32-S3 的离线中文 TTS Arduino 封装。底层使用
Arduino-ESP32 附带的乐鑫 ESP-SR TTS 引擎，输入 UTF-8 中文，流式输出
16 kHz、16-bit、单声道 PCM。支持 Arduino `Stream`（包括 `I2SClass`）、
PCM 回调、拼音输入、金额播报、0～5 档语速和跨任务停止。

声音模型直接编译进应用固件，不再需要创建 `voice_data` 分区或单独上传
`.dat` 文件。默认使用约 2.78 MiB 的 small 模型；通过宏可改用约 3.64 MiB
的标准版。链接器只会把选中的一个模型加入最终固件。

## 环境要求

- ESP32-S3，Flash 至少 8 MB。
- Arduino-ESP32 3.3.8 或更新的 3.x 版本。
- I2S DAC/功放（例如 MAX98357A），或能接收 PCM 的自定义输出。
- 使用 `BasicI2S_ES8311` 示例时，另行安装同级目录中的 `esp32_es8311` 库。

## 首次使用

1. 安装本库并选择 ESP32-S3 开发板，把 **Flash Size** 设为 8 MB 或更大。
2. 打开 `File > Examples > ESP32TTS > BasicI2S`，按硬件修改 BCLK、LRCLK
   和 DOUT 引脚。
3. 正常编译并上传草图。示例自带的 `partitions.csv` 会为内嵌模型预留足够的
   应用空间，不再需要运行声音数据烧录脚本。

## 选择声音模型

默认无需任何配置，small 模型会编译进固件：

```cpp
#include <ESP32TTS.h>
```

若要使用标准版，必须在包含头文件之前定义宏：

```cpp
#define ESP32_TTS_USE_STANDARD_VOICE 1
#include <ESP32TTS.h>
```

也可以通过构建系统为草图定义
`ESP32_TTS_USE_STANDARD_VOICE=1`。宏只接受 `0` 或 `1`。两个模型分别位于
静态归档的不同成员中，因此即使归档包含两份数据，最终固件也只链接所选版本。

## 最小示例

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

`speak()` 是阻塞调用。需要中止时，可从另一个 FreeRTOS 任务调用 `stop()`。
不要在 PCM 回调内部销毁 `ESP32TTS` 对象。

## API 摘要

- `begin()`：使用编译进固件的 small 或标准声音模型创建合成器。
- `speak(text, stream/callback)`：合成 UTF-8 中文。
- `speakPinyin("da4 jia1 hao3", ...)`：合成带声调数字的拼音。
- `speakMoney(yuan, jiao, fen, mode, ...)`：合成金额播报。
- `setSpeed(0..5)`：0 最慢，5 最快，默认 3。
- `stop()`：请求停止当前的阻塞合成。
- `lastError()` / `lastErrorMessage()`：获取最近错误。
- `end()`：释放 TTS 和声音集；正在合成时返回 `false`/`Busy`。

为兼容旧项目，`begin(partitionLabel)` 和
`begin(partitionLabel, size, sha256)` 仍可从外部分区加载声音数据。新项目通常只需
调用无参数的 `begin()`。

PCM 回调返回已消费的采样数；返回 0 会终止合成并报告 `OutputFailed`：

```cpp
size_t output(const int16_t *samples, size_t sampleCount, void *userData);
```

## Flash 与分区

内嵌 small 模型的 `BasicI2S` 测试固件约为 3.15 MiB，标准版约为 4.03 MiB，
因此普通的 1～3 MB 应用分区无法容纳。三个示例都带有适用于 8 MB Flash 的
`partitions.csv`：应用分区为 6.6875 MiB，SPIFFS 为 1.25 MiB。相同布局也保存于
`extras/partitions/tts_8mb_embedded.csv`。

更换声音模型后只需重新编译并正常上传固件；选择“Erase All Flash”后也不需要
额外恢复声音数据。

## 维护声音数据归档

发布包中的 `src/esp32s3/libESP32TTSVoice.a` 由 `extras/voice_data` 中的两个官方
`.dat` 生成。替换模型文件后，使用 ESP32-S3 GCC 工具链重新生成归档：

```bash
python3 tools/build_voice_archive.py --toolchain path/to/toolchain/bin
```

来源、校验值和许可见 `extras/voice_data/README.md`。

## 限制

- 当前设备端合成器只支持中文，文本源码应使用 UTF-8。
- 输出固定为 16 kHz、16-bit、有符号、单声道 PCM。
- 合成为流式阻塞操作；应用需要保持响应时请放到独立 FreeRTOS 任务。
- 库只支持 ESP32-S3，并依赖 Arduino-ESP32 随附的 ESP-SR TTS 组件。
