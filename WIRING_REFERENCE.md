# Referensi Wiring

Dokumen ini mencatat pin yang dikonfigurasi pada source. Periksa label pin pada board ESP32 dan modul DAC yang sebenarnya sebelum menyambungkan daya.

## Pemetaan pin ESP32

| Perangkat/fungsi | GPIO ESP32 | Catatan |
| --- | ---: | --- |
| I2S0 BCK | 5 | Clock bit untuk DAC/bus output A-B |
| I2S0 WS/LRCK | 18 | Word select / left-right clock |
| I2S0 DOUT | 19 | Data keluar dari ESP32 menuju DAC |
| I2S1 BCK | 4 | Clock bit untuk DAC/bus output C-D |
| I2S1 WS/LRCK | 13 | Word select / left-right clock |
| I2S1 DOUT | 14 | Data keluar dari ESP32 menuju DAC |
| OLED SDA | 21 | I2C data |
| OLED SCL | 22 | I2C clock |
| Encoder CLK | 32 | Encoder A/CLK |
| Encoder DT | 33 | Encoder B/DT |
| Encoder SW | 27 | Tombol encoder, aktif-low |

## DAC I2S dan pemetaan channel

| Bus I2S | DAC stereo | Slot kiri | Slot kanan |
| --- | --- | --- | --- |
| I2S0 (BCK 5, WS 18, DOUT 19) | DAC 1 | Channel A | Channel B |
| I2S1 (BCK 4, WS 13, DOUT 14) | DAC 2 | Channel C | Channel D |

Firmware mengatur kedua bus pada 44,1 kHz, slot stereo 32-bit yang membawa resolusi sampel 24-bit. Gunakan DAC I2S yang mendukung format/clock tersebut. ESP32 tidak menghasilkan sinyal analog audio langsung; keluaran analog berasal dari DAC eksternal.

Contoh routing defaults: A = Left/Mid, B = Right/Mid, C = Mono/Sub, D = Mono/High. Konfigurasi ini dapat diubah lewat Output Matrix.

## OLED dan encoder

- OLED SSD1306: SDA GPIO 21, SCL GPIO 22, alamat I2C `0x3C`.
- Encoder: CLK GPIO 32, DT GPIO 33, SW GPIO 27.
- Firmware mengaktifkan internal pull-up untuk ketiga pin encoder; switch menghubungkan pin SW ke ground saat ditekan.
- Pastikan modul OLED menggunakan level logika yang kompatibel dengan board ESP32.

## Catu daya dan koneksi amplifier

Diagram konseptual (tegangan harus disesuaikan dengan spesifikasi perangkat yang digunakan):

```text
PSU amplifier
  +--> amplifier
  +--> regulator/buck sesuai spesifikasi --> ESP32
                                  +--------> DAC 1, DAC 2, OLED

ESP32 I2S0 --> DAC 1 --> input amplifier channel A/B
ESP32 I2S1 --> DAC 2 --> input amplifier channel C/D
```

- Jangan hubungkan rail PSU amplifier (misalnya 19 V) langsung ke ESP32, DAC, atau OLED.
- Pilih regulator dengan output, arus, dan noise yang sesuai untuk board/modul. Jangan menganggap semua breakout DAC menerima tegangan supply yang sama.
- Gunakan ground signal yang baik dan wiring BCK/WS/DOUT pendek. Pastikan ground ESP32, DAC, dan sisi input amplifier direferensikan dengan benar.
- Jangan menggabungkan terminal speaker amplifier BTL atau menghubungkannya ke ground kecuali dokumentasi amplifier secara eksplisit mengizinkan.
- Sebelum memasang speaker, verifikasi pinout, tidak ada short, level output, konfigurasi crossover, dan perilaku mute pada volume rendah. Gunakan datasheet DAC/amplifier dan driver speaker untuk keputusan keselamatan.

Lihat [TIPS_TRICKS.md](TIPS_TRICKS.md) untuk urutan commissioning dan troubleshooting.
