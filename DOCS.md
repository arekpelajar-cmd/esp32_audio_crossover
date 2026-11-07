# Arsitektur Teknis

Dokumen ini mencatat perilaku firmware pada source saat ini. Konfigurasi hardware dan parameter dapat berubah; cek `src/esp32_audio_crossover_config.h` dan `platformio.ini` jika melakukan modifikasi.

## Jalur audio

```text
A2DP PCM stereo signed 16-bit, 44.1 kHz
  -> FreeRTOS queue (20 blok x 128 frame)
  -> task DSP (dipin ke Core 1)
  -> 5-band PEQ stereo
  -> matrix input dan polarity per output
  -> crossover LR24 per output
  -> gain ramp dan delay fractional per output
  -> DC blocker
  -> lookahead limiter, soft clip, dan output headroom
  -> I2S0 stereo (Channel A/B) dan I2S1 stereo (Channel C/D)
```

PCM Bluetooth dibagi ke blok hingga 128 frame dan dikirim melalui queue. Saat queue kosong, task DSP menulis blok hening ke kedua bus I2S. Saat queue penuh, blok yang tidak tertampung dihitung sebagai drop/overflow.

Kedua bus I2S menggunakan 44.100 Hz, slot stereo 32-bit yang membawa sampel audio 24-bit. I2S0 menyediakan output A/B; I2S1 menyediakan C/D. Keduanya menggunakan clock masing-masing, sehingga firmware memicu Safe Mode jika terjadi partial write yang berpotensi membuat aliran antarkanal tidak sinkron.

## Sinkronisasi setting dan task

- Arduino `setup()` memuat default, membaca konfigurasi NVS, lalu menginisialisasi I2S, DSP, OLED, encoder, dan Bluetooth.
- Task audio dibuat dengan `xTaskCreatePinnedToCore(..., 1)`, yaitu dipin ke Core 1 pada source saat ini.
- Fungsi `loop()` juga menjalankan encoder, UI, autosave, dan pemeriksaan heartbeat.
- Setting UI disalin ke snapshot DSP pada batas blok. Akses ke struktur setting bersama dilindungi critical section.
- Jangan mengandalkan komentar lama yang menyebut DSP berjalan di Core 0; konfigurasi task aktual di source menentukan pinning.

## Filter dan output

- Crossover Linkwitz-Riley 24 dB menggunakan pasangan biquad low-pass/high-pass Butterworth orde dua yang dikaskadekan.
- Band Sub memakai LPF; Mid memakai HPF lalu LPF; High memakai HPF; Full melewati crossover.
- PEQ lima band memakai filter biquad RBJ: Peaking, Low Shelf, dan High Shelf. PEQ diterapkan pada input kiri dan kanan sebelum matrix routing.
- Matrix tiap output memilih Left, Right, atau Mono. Mono adalah rata-rata `(L + R) / 2`.
- Gain output dislew untuk mengurangi perubahan mendadak. Phase Invert membalik polaritas output.
- DC blocker adalah high-pass IIR orde satu.
- Limiter memiliki lookahead 16 sampel (sekitar 0,36 ms pada 44,1 kHz), threshold 0,95, ceiling 0,98, serta soft clipping.
- Konversi output memakai headroom tambahan sebelum dikirim sebagai sampel 24-bit di slot 32-bit.

## Delay dan memori

Delay per channel dibatasi firmware pada 0–2,8 ms dengan langkah kontrol 0,1 ms. Buffer berukuran 128 sampel per channel dan menggunakan interpolasi fractional linear. Bila build mengaktifkan PSRAM dan PSRAM terdeteksi, buffer dicoba dialokasikan di sana; jika tidak, firmware menggunakan heap internal. PSRAM bukan syarat untuk fitur delay pada ukuran buffer saat ini.

Catatan: layar Time Alignment saat ini menggambar slider dengan skala sampai 20 ms, tetapi nilai yang dapat disetel dan diterima validator dibatasi `MAX_DELAY_MS = 2.8`. Nilai efektif maksimum adalah 2,8 ms; skala visual slider belum sama dengan batas firmware.

## Ketahanan sistem

- Supervisor memeriksa heartbeat setiap 500 ms. Jika heartbeat DSP sudah pernah berjalan lalu tidak berubah lebih dari 1.200 ms, firmware memicu Safe Mode.
- Timeout pembacaan queue DSP sekitar 2 ms; pada timeout, keluaran I2S diberi blok hening.
- Partial write pada salah satu bus I2S langsung memicu Safe Mode. Kegagalan write tanpa partial memicu Safe Mode setelah lima kegagalan berturut-turut.
- Safe Mode memute audio. Pengguna dapat mencoba clear manual dengan menahan tombol encoder sekitar 5 detik; setelah itu periksa penyebab melalui layar/Serial sebelum melanjutkan pemakaian.
- Sample rate A2DP diperiksa melalui callback. Hanya 44,1 kHz yang diterima; rate lain dicatat sebagai fault konfigurasi dan output dimute. Firmware belum melakukan resampling.
- NVS memeriksa magic, versi, ukuran payload, CRC, dan nilai setting. Recovery record ditulis sebelum field konfigurasi diperbarui; jika data utama tidak valid, firmware mencoba recovery record lalu defaults.
- Perubahan parameter disimpan setelah 3 detik tanpa edit. Konfirmasi berhasil muncul sebagai toast `[NVS SAVED]`.

## Batas desain saat ini

- Input audio aktif hanya Bluetooth A2DP stereo; tidak ada input AUX/analog.
- Tidak ada resampler. Gunakan source Bluetooth yang bernegosiasi pada 44,1 kHz.
- Bluetooth Info menunjukkan status, nama sink, dan judul metadata; tidak menyediakan daftar/perpindahan perangkat Bluetooth dari UI.
- OLED adalah SSD1306 128x64 melalui I2C. Nama sink yang dikonfigurasi: `DLMS BALAPs`.
- Build PlatformIO memakai `board = esp32dev`; konfigurasi PSRAM bergantung pada modul/build yang digunakan dan bukan keharusan untuk buffer delay 128 sampel.

Dokumen pemakaian dan koneksi: [USER_GUIDE.md](USER_GUIDE.md), [OLED_SCREENS.md](OLED_SCREENS.md), [WIRING_REFERENCE.md](WIRING_REFERENCE.md), [TIPS_TRICKS.md](TIPS_TRICKS.md), dan [FIXES_VERIFICATION.md](FIXES_VERIFICATION.md).
