# ESP32 Audio Crossover DSP

Firmware DSP audio berbasis ESP32 untuk crossover 4-channel, processing audio sumber Bluetooth A2DP, pengaturan EQ parametris, delay, fase, dan kontrol perangkat keras via encoder + OLED. Proyek ini ditargetkan untuk board ESP32 Dev Module (`esp32dev`) dan aplikasi speaker aktif / crossover multiway. PSRAM dapat digunakan bila tersedia dan diaktifkan oleh konfigurasi board, tetapi bukan syarat untuk buffer delay saat ini.

## Ringkasan proyek

Project ini mengubah ESP32 menjadi sistem audio DSP mandiri yang mampu:

- menerima stream stereo Bluetooth A2DP
- memproses audio dengan crossover Linkwitz-Riley 24 dB
- mengatur 5-band PEQ (parametric EQ)
- mengatur gain, mute, delay, dan phase per channel
- mengirim output ke dua bus I2S yang terpisah untuk 4 channel output
- menampilkan status dan pengaturan di OLED SSD1306
- menyimpan konfigurasi ke NVS untuk auto-save
- memantau kesehatan DSP dan mengaktifkan safe mode bila diperlukan

## Fitur utama

- Input: Bluetooth A2DP Sink stereo
- Sample rate target: 44,1 kHz
- Output: 4 channel matrix audio
- Crossover: LR24 (crossover 4-band dengan sub/mid/high)
- Filter: 5-band PEQ (peaking, low shelf, high shelf)
- Time alignment/delay: 0–2,8 ms per channel (langkah 0,1 ms)
- Limiter dan safety processing: brick-wall limiter, DC blocker, mute failure-safe
- UI: encoder rotary + OLED SSD1306
- Penyimpanan: NVS + CRC validation + recovery record
- Sistem: watchdog heartbeat supervisor, safe mode, diagnostics

## Batasan penting

Firmware ini hanya memproses stream A2DP 44,1 kHz dan belum memiliki resampler. Callback sample rate memeriksa rate yang dinegosiasikan; rate selain 44,1 kHz dicatat sebagai fault dan output dimute.

Selain itu:

- tidak ada input analog/AUX aktif di firmware saat ini
- tidak ada web UI / app control
- perangkat ini hanya memproses stream Bluetooth stereo yang diterima oleh A2DP sink
- tidak ada kebutuhan PSRAM untuk buffer delay 128 sampel; firmware memakai PSRAM bila tersedia dan fallback ke RAM internal

## Arsitektur perangkat keras

### Pin yang digunakan

| Fungsi | GPIO |
| --- | --- |
| I2S0 BCK | 5 |
| I2S0 WS | 18 |
| I2S0 DOUT | 19 |
| I2S1 BCK | 4 |
| I2S1 WS | 13 |
| I2S1 DOUT | 14 |
| OLED SDA | 21 |
| OLED SCL | 22 |
| Encoder CLK | 32 |
| Encoder DT | 33 |
| Encoder SW | 27 |

### Konfigurasi utama

- Board: ESP32 Dev Module (`esp32dev`)
- Framework: Arduino
- PlatformIO environment: `esp32wroom`
- Build flags: `-DA2DP_I2S_AUDIOTOOLS=0`; konfigurasi PSRAM mengikuti board yang dipakai
- Dependency: ESP32-A2DP, Adafruit GFX, Adafruit SSD1306

## Struktur project

```text
.
├── src/
│   ├── main.cpp
│   ├── esp32_audio_crossover_config.h
│   ├── esp32_audio_dsp.h
│   ├── esp32_audio_i2s_handler.h
│   ├── esp32_audio_bluetooth.h
│   ├── esp32_audio_oled_ui.h
│   ├── esp32_audio_encoder.h
│   ├── esp32_audio_nvs.h
│   └── esp32_audio_bt_select.h
├── README.md
├── USER_GUIDE.md
├── DOCS.md
├── OLED_SCREENS.md
├── WIRING_REFERENCE.md
├── TIPS_TRICKS.md
├── FIXES_VERIFICATION.md
├── platformio.ini
├── .gitignore
├── LICENSE
└── compile_commands.json
```

## Prasyarat

- PlatformIO Core / VS Code + PlatformIO IDE
- Board ESP32 yang kompatibel dengan `esp32dev`
- Power supply yang cukup stabil untuk ESP32 dan DAC/I2S output
- Speaker / amplifier yang sesuai dengan kanal output
- Smartphone atau perangkat sumber audio yang mendukung Bluetooth A2DP

## Setup dan build

1. Pastikan PlatformIO sudah terinstall.
2. Buka project folder ini di VS Code / PlatformIO.
3. Pastikan board yang dipakai kompatibel dengan ESP32 Dev Module; PSRAM bersifat opsional.
4. Jalankan perintah build:

```bash
pio run
```

5. Upload ke board:

```bash
pio run -t upload
```

6. Buka monitor serial:

```bash
pio device monitor
```

## Penggunaan dasar

### Kontrol encoder

- Putar encoder: navigasi / mengubah nilai parameter
- Klik pendek: masuk menu / mulai atau konfirmasi edit
- Double-click: kembali satu tingkat
- Tahan ≥600 ms: kembali ke HOME
- Tahan sekitar 5 detik saat Safe Mode: coba clear Safe Mode setelah fault diperiksa

### Menu utama

- Master volume
- Preset audio
- Output matrix
- PEQ 5-band
- Time alignment / delay
- Phase inversion
- Crossover LR24
- Informasi Bluetooth
- Diagnostics

### Auto-save

Setiap perubahan parameter akan otomatis disimpan ke NVS setelah periode idle tertentu. Sistem juga menyediakan validasi CRC dan recovery record agar konfigurasi tidak rusak pada saat write interupsi atau power loss.

## Tipe audio dan pipeline DSP

Pada level pipeline, sistem bekerja seperti ini:

```text
A2DP PCM stereo 16-bit @ 44,1 kHz
  → Queue audio (20 × 128 frame)
  → DSP task (dipin ke Core 1)
  → PEQ
  → matrix source / polarity
  → LR24 crossover
  → gain, delay, limiter
  → DAC I2S output
```

Komponen utama yang diproses di pipeline:

- low-pass filter / high-pass filter crossover
- gain staging per channel
- phase inversion control
- delay fractional 0–2,8 ms (PSRAM bila tersedia, fallback RAM internal)
- DC blocker
- limiter dan soft clipping

## Dokumentasi terkait

- [USER_GUIDE.md](USER_GUIDE.md): panduan penggunaan dan batas parameter UI
- [DOCS.md](DOCS.md): arsitektur dan jalur DSP
- [OLED_SCREENS.md](OLED_SCREENS.md): detail tampilan OLED per layar
- [WIRING_REFERENCE.md](WIRING_REFERENCE.md): referensi wiring hardware
- [TIPS_TRICKS.md](TIPS_TRICKS.md): tips commissioning dan troubleshooting
- [FIXES_VERIFICATION.md](FIXES_VERIFICATION.md): status yang sudah diperbaiki

## Troubleshooting

Beberapa kondisi umum yang perlu diperhatikan:

- Bluetooth tersambung tanpa audio: cek sample rate; rate selain 44,1 kHz dimute
- OLED tidak tampil: cek wiring SDA/SCL dan alamat 0x3C
- speaker output tidak muncul: cek I2S pin dan output amplifier
- Bluetooth tidak terhubung: pastikan board mendapat power stabil dan library A2DP tersedia
- delay tidak mencapai 20 ms: rentang aktual firmware 0–2,8 ms; skala slider OLED masih 0–20 ms
- safe mode aktif: cek log serial untuk diagnosis fault seperti DSP timeout, I2S error, atau queue overflow

## Lisensi

Proyek ini dilisensikan di bawah MIT License. Lihat file [LICENSE](LICENSE) untuk teks lengkap.

## Catatan pengembang

Project ini masih merupakan firmware DSP yang ditargetkan untuk kebutuhan spesifik audio crossover dan tuning. Untuk penggunaan produksi atau aplikasi komersial, disarankan untuk melakukan review hardware dan konfigurasi audio secara menyeluruh sebelum deployment ke perangkat nyata.
