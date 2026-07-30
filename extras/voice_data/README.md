# Voice data

The directory contains both the Xiaoxin small voice set
(`esp_tts_voice_data_xiaoxin_small.dat`) and the full Xiaoxin voice set
(`esp_tts_voice_data_xiaoxin.dat`) from
Espressif's `esp-sr` repository, commit
`2f8c4b0459db5bbb39abd77adae27962d6d94bcb`.

Source:
https://github.com/espressif/esp-sr/blob/2f8c4b0459db5bbb39abd77adae27962d6d94bcb/esp-tts/esp_tts_chinese/esp_tts_voice_data_xiaoxin_small.dat

https://github.com/espressif/esp-sr/blob/2f8c4b0459db5bbb39abd77adae27962d6d94bcb/esp-tts/esp_tts_chinese/esp_tts_voice_data_xiaoxin.dat

The file is distributed under the license in `ESPRESSIF_LICENSE`.

Exact validation values used by the library and flashing tool:

- Xiaoxin small: `2913777` bytes, SHA-256
  `cc9a81fd716b3c07fae3ca2f802dc026081896f2e34db9b9db117d4de5a85c01`
- Xiaoxin full: `3821311` bytes, SHA-256
  `b0b9ad9fdaa4a560ee839ce6a4659f08af3fded7c72d0784d83186859a081e55`

The Arduino library links one of these files directly into the application
firmware through `src/esp32s3/libESP32TTSVoice.a`. Run
`tools/build_voice_archive.py` after changing either `.dat` file.
