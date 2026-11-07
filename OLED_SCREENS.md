# Referensi Layar OLED

Firmware menggunakan OLED SSD1306 128x64 pada alamat I2C `0x3C`, SDA GPIO 21 dan SCL GPIO 22. Bus diatur ke 400 kHz. HOME diperbarui sekitar 30 FPS untuk VU meter; layar menu dirender saat ada perubahan dengan batas refresh sekitar 6,7 FPS. Seluruh tampilan memakai satu transfer `display()` per frame.

| Layar | Isi dan kontrol |
| --- | --- |
| Boot | Animasi startup, judul `ESP32 AUDIO DSP PRO`, label LR24 + 5-band PEQ, dan status alokasi PSRAM/RAM. |
| HOME | Status BT, master volume, indikator `[LIMIT]`, judul track/metadata, label serta VU meter Channel A-D. Putar encoder mengubah volume; klik membuka Main Menu. |
| Main Menu | Master Volume, Presets, Output Matrix, Parametric EQ, Time Alignment, Phase Invert, LR24 Crossover, Bluetooth Info, dan Diagnostics. |
| Presets | Flat, Bass Boost, Vocal, Rock, Custom. Klik pilihan untuk menerapkan preset. |
| Output Matrix | Daftar output A-D; pilih channel untuk mengedit Source, Filter, Gain, Mute, dan Delay. |
| Channel Setup | Nilai Source/Filter/Gain/Mute/Delay untuk channel terpilih. Putar untuk pindah field; klik untuk edit; klik lagi untuk selesai. |
| 5-Band PEQ | Ringkasan frekuensi/gain/type setiap band beserta grafik gain; pilih band untuk membuka editor. |
| PEQ Band Edit | Type, Frequency, Gain, dan Q-factor. Putar untuk memilih field; klik untuk mengedit atau menyelesaikan edit. |
| Time Alignment | Delay Channel A-D dalam ms. Langkah encoder 0,1 ms; rentang efektif 0–2,8 ms. Slider yang digambar masih berskala 0–20 ms sehingga panjang slider tidak merepresentasikan batas nilai sebenarnya. |
| Phase Polarity | Status Normal (0°) atau Invert (180°) per channel; klik pada channel terpilih untuk toggle. |
| LR24 Crossover | Sub LPF, Mid HPF, Mid LPF, dan High HPF. Klik untuk edit field yang dipilih. |
| Bluetooth Status | Connected/Pairing, nama sink `DLMS BALAPs`, dan judul metadata track. Layar status ini tidak menyediakan daftar pemilihan perangkat. |
| System Diagnostics | DSP CPU, queue saat ini/depth/high-water, DROP dan underrun (UF), error I2S, dan indikator PSRAM. |
| Safe Mode | Mengganti tampilan normal, menunjukkan fault yang dikenali, CPU/drop, serta instruksi pemeriksaan Serial/reboot. Output dimute. |

## Navigasi dan notifikasi

- Klik pendek diproses setelah jendela deteksi double-click sekitar 300 ms.
- Double-click kembali ke layar sebelumnya; saat sedang edit, double-click mengakhiri mode edit tetapi tidak meng-undo perubahan yang sudah dilakukan.
- Tahan tombol encoder minimal 600 ms untuk kembali ke HOME.
- Menu otomatis kembali ke HOME setelah sekitar 10 detik tanpa aktivitas.
- Toast koneksi Bluetooth dan `[NVS SAVED]` tampil sementara di atas layar aktif.
- Saat Safe Mode, tahan tombol encoder sekitar 5 detik untuk mencoba clear fault dan kembali ke HOME. Periksa penyebab fault sebelum melanjutkan.

## Membaca indikator

- `[LIMIT]` menandakan limiter sedang melakukan reduksi/penanganan level.
- VU meter menunjukkan level relatif per channel; bukan alat ukur kalibrasi.
- Queue dan DROP/UF adalah counter/telemetri firmware, bukan pengukuran kualitas audio.
- `PSRAM: OK` menunjukkan setidaknya satu delay buffer dialokasikan di PSRAM; `NO` berarti firmware menggunakan RAM internal.
- Safe Mode menampilkan fault yang dipilih dari bit fault aktif; log Serial dapat memberi detail tambahan.
