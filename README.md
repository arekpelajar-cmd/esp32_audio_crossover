<div align="center">

# ESP32 Audio Crossover DSP

**Crossover audio DSP 4-channel berbasis ESP32 dengan input Bluetooth A2DP, PEQ 5-band, dan kontrol lokal melalui OLED + rotary encoder.**

[![PlatformIO](https://img.shields.io/badge/PlatformIO-ESP32-orange?logo=platformio)](https://platformio.org/)
[![Board](https://img.shields.io/badge/Board-ESP32%20Dev%20Module-blue)](https://docs.platformio.org/en/latest/boards/espressif32/esp32dev.html)
[![Framework](https://img.shields.io/badge/Framework-Arduino-00979D?logo=arduino)](https://www.arduino.cc/)
[![Audio](https://img.shields.io/badge/Audio-Bluetooth%20A2DP-0066CC)](https://github.com/pschatzmann/ESP32-A2DP)
[![Display](https://img.shields.io/badge/Display-SSD1306%20OLED-purple)](https://github.com/adafruit/Adafruit_SSD1306)
[![License](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

</div>

---

## 📌 Ringkasan

ESP32 Audio Crossover DSP adalah firmware audio multi-output untuk menerima stream Bluetooth A2DP stereo, memprosesnya melalui equalizer dan crossover, lalu mengirim audio ke empat channel melalui dua bus I2S.

Firmware menyediakan pengaturan crossover Linkwitz-Riley 24 dB, PEQ lima band, routing matrix, gain, mute, polarity, dan delay per channel. Pengoperasian dilakukan secara lokal dengan rotary encoder dan OLED SSD1306. Setting disimpan di NVS dengan pemeriksaan CRC dan recovery record.

> **Penting:** firmware saat ini hanya mendukung stream A2DP **44,1 kHz**. Stream dengan sample rate lain akan dimute; firmware tidak melakukan resampling. Selalu uji crossover dan routing pada volume rendah sebelum menghubungkan speaker.

## 📑 Daftar Isi

- [Fitur](#-fitur)
- [Batasan dan Spesifikasi](#-batasan-dan-spesifikasi)
- [Arsitektur Audio](#-arsitektur-audio)
- [Hardware dan Wiring](#-hardware-dan-wiring)
- [Persiapan, Build, dan Upload](#-persiapan-build-dan-upload)
- [Panduan Penggunaan](#-panduan-penggunaan)
- [Layar OLED](#-layar-oled)
- [Commissioning dan Tuning](#-commissioning-dan-tuning)
- [Troubleshooting](#-troubleshooting)
- [Verifikasi Firmware](#-verifikasi-firmware)
- [Struktur Project](#-struktur-project)
- [Lisensi](#-lisensi)

## ✨ Fitur

- Bluetooth A2DP Sink untuk input PCM stereo.
- Empat output matrix melalui dua bus I2S stereo.
- Crossover Linkwitz-Riley 24 dB untuk band Sub, Mid, High, atau Full/bypass.
- Parametric EQ lima band dengan filter Peaking, Low Shelf, dan High Shelf.
- Pilihan sumber Left, Right, dan Mono untuk tiap output.
- Gain output, mute, polarity invert, dan delay per channel.
- Limiter lookahead, soft clipping, DC blocker, dan headroom DSP.
- OLED SSD1306 128×64 dengan status Bluetooth, VU meter, menu, dan diagnostics.
- Rotary encoder dengan navigasi single-click, double-click, dan long-press.
- Autosave ke NVS, CRC validation, recovery record, heartbeat supervisor, dan Safe Mode.

## ⚙️ Batasan dan Spesifikasi

| Parameter | Implementasi |
| --- | --- |
| Board PlatformIO | `esp32dev` (environment `esp32wroom`) |
| Framework | Arduino pada platform Espressif32 |
| Input audio | Bluetooth A2DP Sink, stereo signed 16-bit PCM |
| Sample rate yang diterima | 44.100 Hz saja |
| Output | 4 channel melalui I2S0 stereo dan I2S1 stereo |
| Slot I2S | Stereo 32-bit, sampel audio 24-bit |
| Queue audio | 20 blok × maksimum 128 frame |
| PEQ | 5 band, 20–20.000 Hz, -12 sampai +12 dB, Q 0,1–10 |
| Gain output | -20 sampai +6 dB |
| Delay aktual | 0–2,8 ms/channel, langkah encoder 0,1 ms |
| OLED | SSD1306 128×64, I2C `0x3C` |
| Input analog/AUX | Tidak tersedia di firmware saat ini |
| Web UI / aplikasi kontrol | Tidak tersedia |

PSRAM bersifat opsional. Jika tersedia dan aktif pada konfigurasi board, firmware mencoba menggunakan PSRAM untuk buffer delay; jika tidak, firmware menggunakan RAM internal. Delay buffer saat ini hanya 128 sampel per channel. **Perbedaan UI:** slider Time Alignment masih digambar dengan skala 0–20 ms, tetapi nilai yang dapat diatur firmware dibatasi sampai 2,8 ms.

Bluetooth Info menampilkan status, nama sink, dan judul metadata. Firmware tidak menyediakan UI untuk memilih perangkat dari sebuah daftar. Nama sink Bluetooth yang disetel pada source adalah `DLMS BALAPs`.

## 🧠 Arsitektur Audio

```text
Bluetooth A2DP PCM stereo 16-bit @ 44,1 kHz
  -> FreeRTOS queue (20 blok x 128 frame)
  -> Task audio DSP (dipin ke Core 1)
  -> PEQ 5-band stereo
  -> Matrix source/polarity untuk tiap output
  -> Crossover LR24 per output
  -> Gain ramp dan fractional delay
  -> DC blocker
  -> Lookahead limiter, soft clip, dan output headroom
  -> I2S0 stereo (Channel A/B) + I2S1 stereo (Channel C/D)
```

### Pemrosesan DSP

- PEQ biquad berbasis tipe RBJ diterapkan ke input kiri dan kanan sebelum routing matrix.
- `Left` dan `Right` memilih kanal input masing-masing; `Mono` merupakan rata-rata `(L + R) / 2`.
- Sub memakai low-pass, Mid memakai high-pass lalu low-pass, High memakai high-pass, dan Full melewati crossover.
- Filter LR24 dibentuk dari pasangan biquad Butterworth orde dua yang dikaskadekan.
- Gain output di-slew agar perubahan nilai tidak terlalu mendadak; phase invert membalik polaritas output.
- Delay menggunakan interpolasi fractional linear pada buffer per channel.
- Limiter memiliki lookahead 16 sampel (sekitar 0,36 ms pada 44,1 kHz), threshold 0,95, dan ceiling 0,98.
- Hasil akhir dikonversi menjadi audio 24-bit di dalam slot I2S 32-bit dengan headroom tambahan.

### Task dan ketahanan sistem

- Task audio dibuat dengan `xTaskCreatePinnedToCore(..., 1)`, sehingga dipin ke Core 1 pada source saat ini.
- `loop()` Arduino menangani encoder, UI, autosave, dan pemeriksaan heartbeat.
- Setting yang dibagikan ke DSP disalin sebagai snapshot pada batas blok dan dilindungi critical section.
- Saat queue audio kosong, task DSP menulis blok hening ke kedua bus I2S. Queue penuh dapat menyebabkan drop/overflow.
- Partial write pada salah satu bus I2S langsung memicu Safe Mode. Kegagalan write penuh yang berulang juga dapat memicu Safe Mode.
- Supervisor memeriksa heartbeat setiap 500 ms dan memicu Safe Mode jika heartbeat yang telah berjalan berhenti lebih dari 1.200 ms.
- NVS memeriksa magic, versi, ukuran payload, CRC, dan nilai setting. Recovery record dicoba sebelum firmware kembali ke defaults.
- Perubahan setting disimpan setelah 3 detik tanpa edit. Notifikasi `[NVS SAVED]` menunjukkan save berhasil.

## 🔌 Hardware dan Wiring

### Pemetaan GPIO

| Sinyal | GPIO ESP32 | Sambungan |
| --- | ---: | --- |
| I2S0 BCK | 5 | DAC 1 BCK |
| I2S0 WS / LRCK | 18 | DAC 1 WS/LRCK |
| I2S0 DOUT | 19 | DAC 1 DIN |
| I2S1 BCK | 4 | DAC 2 BCK |
| I2S1 WS / LRCK | 13 | DAC 2 WS/LRCK |
| I2S1 DOUT | 14 | DAC 2 DIN |
| OLED SDA | 21 | SSD1306 SDA |
| OLED SCL | 22 | SSD1306 SCL |
| Encoder CLK | 32 | Rotary encoder CLK |
| Encoder DT | 33 | Rotary encoder DT |
| Encoder SW | 27 | Tombol encoder, aktif-low |

### Pemetaan output

| Bus I2S | Slot kiri | Slot kanan |
| --- | --- | --- |
| I2S0 / DAC 1 | Channel A | Channel B |
| I2S1 / DAC 2 | Channel C | Channel D |

Contoh routing default firmware: A = Left/Mid, B = Right/Mid, C = Mono/Sub, D = Mono/High. Setiap channel dapat diubah melalui menu Output Matrix.

### Diagram koneksi konseptual

```text
Sumber daya
  +--> Amplifier
  +--> Regulator/buck sesuai spesifikasi --> ESP32
                                  +--------> DAC 1, DAC 2, OLED

ESP32 I2S0 --> DAC 1 stereo --> input amplifier A/B
ESP32 I2S1 --> DAC 2 stereo --> input amplifier C/D

ESP32 GPIO 21/22 --> OLED SDA/SCL
ESP32 GPIO 32/33/27 --> Encoder CLK/DT/SW
```

ESP32 tidak menghasilkan sinyal analog audio secara langsung; DAC I2S eksternal diperlukan. Gunakan modul DAC yang kompatibel dengan slot stereo 32-bit dan clock 44,1 kHz.

> **Keselamatan wiring:** jangan hubungkan rail PSU amplifier (misalnya 19 V) langsung ke ESP32, DAC, atau OLED. Pastikan tegangan supply sesuai dengan spesifikasi modul yang digunakan. Jangan menyatukan terminal output speaker amplifier BTL atau menghubungkannya ke ground kecuali dokumentasi amplifier secara eksplisit mengizinkan. Periksa pinout, ground, dan tidak adanya short sebelum menyalakan sistem.

## 🛠️ Persiapan, Build, dan Upload

### Prasyarat

- VS Code dengan PlatformIO IDE atau PlatformIO Core.
- Board ESP32 yang kompatibel dengan konfigurasi `esp32dev`.
- Dua DAC I2S stereo untuk empat output analog.
- OLED SSD1306 dan rotary encoder, bila UI lokal digunakan.
- Amplifier dan speaker/driver yang sesuai dengan sistem crossover.

### Konfigurasi build

Konfigurasi ada di `platformio.ini`:

- Environment: `esp32wroom`
- Board: `esp32dev`
- Framework: Arduino
- Serial monitor: 115200 baud
- Build flag: `-DA2DP_I2S_AUDIOTOOLS=0`
- Library: ESP32-A2DP, Adafruit GFX, Adafruit SSD1306

### Perintah

Jalankan perintah dari direktori project:

```sh
pio run
pio run -t upload
pio device monitor
```

Pilih upload port yang sesuai jika PlatformIO tidak mendeteksi board secara otomatis. Setelah boot, periksa log serial untuk status I2S, DSP, OLED, Bluetooth, dan NVS.

## 🎛️ Panduan Penggunaan

### Kontrol rotary encoder

| Aksi | Fungsi |
| --- | --- |
| Putar di HOME | Mengubah master volume |
| Klik singkat di HOME | Membuka Main Menu |
| Putar pada menu | Memindahkan pilihan |
| Klik singkat | Membuka pilihan atau mulai/menyelesaikan edit |
| Double-click | Kembali satu tingkat; jika sedang edit, keluar dari mode edit |
| Tahan minimal 600 ms | Kembali ke HOME |
| Tahan sekitar 5 detik saat Safe Mode | Mencoba clear Safe Mode dan kembali ke HOME |

Single-click ditunda sekitar 300 ms agar dapat dibedakan dari double-click. Nilai yang telah diubah dengan putaran encoder langsung berlaku; keluar dari mode edit tidak mengembalikan nilai sebelumnya. Menu otomatis kembali ke HOME setelah kira-kira 10 detik tanpa aktivitas.

Saat Safe Mode, periksa fault dan log Serial sebelum mencoba clear. Clear Safe Mode bukan perbaikan penyebab fault.

### Menu utama

1. **Master Volume** — rentang 0–100%; juga dapat diubah langsung dari HOME.
2. **Presets** — Flat, Bass Boost, Vocal, Rock, Custom. Preset mengubah nilai gain PEQ.
3. **Output Matrix** — pilih Channel A-D untuk mengatur Source, Filter, Gain, Mute, dan Delay.
4. **Parametric EQ** — pilih band lalu edit Type, Frequency, Gain, atau Q.
5. **Time Alignment** — delay per channel, rentang aktual 0–2,8 ms.
6. **Phase Invert** — toggle polaritas 0°/180° untuk channel yang dipilih.
7. **LR24 Crossover** — mengatur empat frekuensi crossover.
8. **Bluetooth Info** — status koneksi, nama sink, dan judul track.
9. **Diagnostics** — beban DSP, kondisi queue, drop/underrun, error I2S, dan status PSRAM.

### Rentang parameter

| Pengaturan | Rentang / pilihan | Langkah atau catatan |
| --- | --- | --- |
| Master volume | 0–100% | 1% |
| Source matrix | Left, Right, Mono | Mono = rata-rata L/R |
| Filter output | Sub LR24, Mid LR24, High LR24, Full Bypass | Per output |
| Gain output | -20 sampai +6 dB | 0,5 dB |
| Mute | Mute / Unmute | Per output |
| Delay output | 0–2,8 ms | 0,1 ms; UI slider masih berskala 0–20 ms |
| Phase | Normal / Invert | 0° / 180° |
| PEQ type | Peaking, LowShelf, HighShelf | Per band |
| PEQ frequency | 20–20.000 Hz | Langkah encoder bertingkat |
| PEQ gain | -12 sampai +12 dB | 0,5 dB |
| PEQ Q | 0,1–10 | 0,05 |

### Crossover

Empat nilai crossover yang tersedia:

| Field | Rentang kontrol | Langkah |
| --- | ---: | ---: |
| Sub LPF | 20–500 Hz | 5 Hz |
| Mid HPF | 20–1.000 Hz | 5 Hz |
| Mid LPF | 500–15.000 Hz | 50 Hz |
| High HPF | 1.500–20.000 Hz | 50 Hz |

Validator menjaga hubungan `Sub LPF <= Mid HPF < Mid LPF <= High HPF`, dengan jarak minimal 50 Hz antara batas mid yang berdekatan dan batas bawah High HPF 1.500 Hz. Jika perlu, validator menyesuaikan nilai terkait agar urutannya tetap valid. Pastikan nilai akhir sesuai spesifikasi driver speaker yang digunakan.

### Autosave

Perubahan setting disimpan ke NVS setelah 3 detik tanpa edit. Tunggu toast `[NVS SAVED]` sebelum memutus daya jika perubahan terbaru perlu dipertahankan. CRC dan recovery record membantu mendeteksi atau memulihkan data konfigurasi yang tidak lengkap/rusak; mekanisme ini bukan pengganti backup konfigurasi eksternal.

## 🖥️ Layar OLED

OLED SSD1306 berukuran 128×64, alamat I2C `0x3C`, pada SDA GPIO 21/SCL GPIO 22. Firmware mengatur I2C 400 kHz. HOME diperbarui sekitar 30 FPS untuk VU meter; layar menu dibatasi sekitar 6,7 FPS dan dirender saat ada perubahan.

| Layar | Informasi |
| --- | --- |
| Boot | Animasi startup, judul firmware, label LR24/PEQ, dan status alokasi PSRAM/RAM |
| HOME | Status BT, master volume, `[LIMIT]`, judul track, label serta VU meter A-D |
| Main Menu | Sembilan pilihan pengaturan utama |
| Presets | Daftar Flat, Bass Boost, Vocal, Rock, Custom |
| Output Matrix | Ringkasan channel A-D dan status mute; klik channel untuk setup |
| Channel Setup | Source, Filter, Gain, Mute, Delay untuk channel terpilih |
| 5-Band PEQ | Ringkasan frekuensi/gain/type dan grafik gain |
| PEQ Band Edit | Type, Frequency, Gain, Q-factor |
| Time Alignment | Delay A-D dalam ms; rentang efektif 0–2,8 ms |
| Phase Polarity | Status Normal/Invert per channel |
| LR24 Crossover | Sub LPF, Mid HPF, Mid LPF, High HPF |
| Bluetooth Status | Connected/Pairing, nama `DLMS BALAPs`, judul track |
| System Diagnostics | DSP CPU, queue saat ini/depth/high-water, DROP, UF, I2S error, PSRAM |
| Safe Mode | Fault yang dikenali, CPU/drop, instruksi cek Serial/reboot; audio dimute |

`[LIMIT]` menunjukkan limiter sedang bekerja; VU meter dan Diagnostics adalah indikator relatif/telemetri, bukan alat kalibrasi. `PSRAM: OK` berarti setidaknya satu delay buffer dialokasikan di PSRAM; `NO` berarti firmware menggunakan RAM internal. Safe Mode menampilkan fault yang terpilih dari status fault aktif; log Serial dapat memberi detail tambahan.

## 🔊 Commissioning dan Tuning

1. Verifikasi wiring, polaritas catu daya, pin GPIO, dan tegangan kerja board/DAC sebelum menyalakan perangkat.
2. Nyalakan ESP32 tanpa menaikkan volume amplifier; pantau Serial Monitor pada 115200 baud.
3. Hubungkan sumber Bluetooth yang bernegosiasi pada 44,1 kHz. Firmware tidak me-resample.
4. Atur output matrix agar setiap channel menuju input amplifier/driver yang benar.
5. Mulai dengan master/gain rendah, PEQ gain 0 dB, dan crossover sesuai spesifikasi driver.
6. Uji satu channel pada satu waktu. Pastikan mute, routing, filter, fase, dan level bekerja sebelum sistem speaker lengkap dipasang.
7. Atur delay dengan pengukuran akustik bila tersedia. Secara kasar 1 ms setara sekitar 34,3 cm jarak rambat suara di udara pada suhu ruang.
8. Setelah mengubah setting, tunggu notifikasi `[NVS SAVED]` sebelum memutus daya.

Hindari boost PEQ besar karena meningkatkan kebutuhan headroom. Perubahan crossover/PEQ saat audio aktif dapat menimbulkan transien; lakukan commissioning pada volume rendah. Limiter tidak melindungi driver dari crossover salah, over-power, DC fault, atau kesalahan wiring.

## 🧰 Troubleshooting

| Gejala | Pemeriksaan |
| --- | --- |
| Bluetooth tersambung tetapi tidak ada audio | Pastikan sample rate A2DP 44,1 kHz; periksa master volume, mute channel, routing, dan layar Diagnostics. |
| Rate source bukan 44,1 kHz | Rate selain 44,1 kHz dicatat sebagai fault dan output dimute; gunakan source/rate yang didukung. |
| Satu atau beberapa output senyap | Periksa routing matrix, band crossover, kabel DIN/BCK/WS, supply DAC, dan input amplifier. Uji output satu per satu. |
| OLED tidak menyala | Periksa supply, ground, SDA/SCL GPIO 21/22, serta alamat I2C `0x3C`. OLED mati tidak selalu berarti audio DSP berhenti. |
| Safe Mode atau output mute | Catat fault dan log Serial; periksa I2S, DSP/queue, memori, dan supply. Jangan hanya clear Safe Mode tanpa mencari penyebab. |
| `CFG_E` atau setting kembali ke default | Periksa log NVS/CRC. Firmware mencoba recovery record, lalu defaults jika recovery tidak valid. |
| Setting tidak tersimpan | Tunggu 3 detik tanpa edit dan pastikan toast `[NVS SAVED]` muncul; periksa log Serial bila tidak berhasil. |
| Distorsi atau `[LIMIT]` sering aktif | Turunkan master/gain, kurangi boost PEQ, dan periksa level source serta headroom. |
| Delay tidak mencapai 20 ms | Batas firmware adalah 2,8 ms. Slider OLED masih menggunakan skala visual 20 ms. |

## ✅ Verifikasi Firmware

Tabel berikut adalah ringkasan inspeksi statis source; status ini **bukan** hasil build atau pengujian hardware.

| Area | Perilaku yang terlihat pada source | Uji perangkat yang disarankan |
| --- | --- | --- |
| Heartbeat DSP | Supervisor memeriksa setiap 500 ms; timeout lebih dari 1.200 ms memicu Safe Mode setelah heartbeat mulai berjalan. | Verifikasi fault dan mute output pada board uji. |
| Sample rate A2DP | Callback menerima 44,1 kHz; rate lain dicatat sebagai fault dan output dimute. Resampler tidak tersedia. | Uji source 44,1 kHz dan rate lain. |
| Queue | Depth 20 blok × 128 frame; timeout receive sekitar 2 ms mengirim blok hening, queue penuh meningkatkan drop/overflow. | Pause/lanjutkan stream dan pantau Diagnostics. |
| Dual I2S | Partial write memicu Safe Mode; kegagalan write penuh yang berulang juga memicu Safe Mode. | Uji kedua bus dengan prosedur fault-injection yang aman. |
| NVS | Header, versi, ukuran, CRC, dan validitas setting dicek; recovery record dicoba sebelum defaults. | Uji data korup dan power interruption pada board uji. |
| Snapshot DSP | Setting dilindungi critical section; snapshot invalid ditolak untuk memakai last-known-good. | Ubah parameter berulang sambil memantau error/drop. |
| Bluetooth metadata | Status koneksi atomic; judul track dibaca menggunakan snapshot terproteksi. | Uji ganti track dan putus-sambung Bluetooth. |
| Delay/UI | Batas aktual 2,8 ms; langkah kontrol 0,1 ms; slider OLED masih 0–20 ms. | Konfirmasi nilai efektif tidak melampaui batas. |
| Safe Mode reset | Tombol encoder ditahan sekitar 5 detik untuk mencoba clear. | Uji pada setup aman setelah penyebab fault diketahui. |

**Validasi sesi dokumentasi:** perubahan ini hanya menyatukan dokumentasi. Build PlatformIO, uji runtime, power-cut, fault injection, dan uji stabilitas jangka panjang tidak dijalankan. Sebelum rilis, lakukan build bersih dan uji empat output, sample rate, Safe Mode, serta recovery NVS pada hardware target.

## 📁 Struktur Project

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
├── platformio.ini
├── README.md
└── LICENSE
```

README ini menjadi dokumentasi utama dan menggabungkan panduan arsitektur, verifikasi, OLED, commissioning, penggunaan, serta wiring yang sebelumnya berada di dokumen terpisah.

## 📄 Lisensi

Proyek ini menggunakan **MIT License**. Lihat file [LICENSE](LICENSE) untuk teks lisensi lengkap.

---

<div align="center">

Dokumentasi ini dibuat untuk eksperimen dan pengembangan audio DSP berbasis ESP32.
Selalu verifikasi konfigurasi hardware dan keselamatan speaker sebelum penggunaan.

</div>
