# Tips, Commissioning, dan Troubleshooting

## Sebelum menyalakan sistem

- Periksa pinout board, tegangan kerja modul DAC, polaritas power, dan sambungan ground sebelum menghubungkan daya.
- Jangan hubungkan rail 19 V langsung ke ESP32, OLED, encoder, atau DAC. Gunakan regulator/buck yang sesuai dengan spesifikasi tiap modul dan sediakan arus yang cukup.
- Pastikan semua perangkat audio memakai referensi ground yang sesuai. Hindari loop ground; jangan menyatukan terminal output speaker amplifier BTL karena terminal tersebut dapat aktif.
- Mulai pengujian dengan amplifier dan master volume rendah, output speaker terlepas bila memungkinkan, lalu uji channel satu per satu.
- Pastikan crossover sesuai dengan batas driver. High HPF firmware memiliki batas bawah 1.500 Hz, tetapi nilai aman sebenarnya ditentukan spesifikasi tweeter dan enclosure.

## Commissioning audio

1. Nyalakan ESP32 dan periksa Serial pada 115200 baud untuk log inisialisasi I2S, DSP, Bluetooth, serta NVS.
2. Hubungkan source Bluetooth yang beroperasi pada 44,1 kHz. Firmware tidak mengubah sample rate; stream rate lain akan dimute.
3. Atur output matrix agar routing setiap output sesuai kanal amplifier/driver.
4. Mulai dengan semua PEQ pada gain 0 dB dan gain channel rendah. Hindari boost besar; boost PEQ meningkatkan kebutuhan headroom.
5. Set crossover berdasarkan respons dan spesifikasi driver. Periksa hasil pengaturan ulang validator setelah mengedit frekuensi.
6. Uji mute, phase, level, dan delay di setiap output secara terpisah sebelum menghubungkan seluruh sistem.
7. Tunggu toast `[NVS SAVED]` setelah perubahan konfigurasi sebelum mematikan daya.

## Tuning

- Gunakan `Left` dan `Right` untuk routing kanal Bluetooth; `Mono` adalah rata-rata kanal kiri dan kanan, bukan input analog.
- LR24 menggunakan frekuensi crossover bersama untuk pasangan band yang bersebelahan. Sesuaikan Sub LPF/Mid HPF dan Mid LPF/High HPF dengan kebutuhan akustik sistem.
- Delay efektif firmware saat ini hanya sampai 2,8 ms (sekitar 123 sampel pada 44,1 kHz), bukan 20 ms. UI masih menggambar skala slider sampai 20 ms.
- Secara kasar 1 ms setara sekitar 34,3 cm jarak rambat suara di udara pada suhu ruang. Gunakan pengukuran akustik untuk alignment akhir.
- Perubahan PEQ/crossover saat audio aktif dapat menimbulkan transien; lakukan perubahan pada volume rendah.

## Troubleshooting

| Gejala | Pemeriksaan |
| --- | --- |
| Bluetooth tersambung tetapi tidak ada audio | Pastikan source bernegosiasi pada 44,1 kHz, periksa mute/master volume dan mute tiap channel, lalu lihat Diagnostics dan Serial. |
| Audio tidak muncul pada sample rate tertentu | Firmware hanya menerima 44,1 kHz dan tidak memiliki resampler. Uji dengan source yang dapat mengeluarkan 44,1 kHz. |
| Satu atau beberapa output senyap | Periksa routing matrix, pemilihan band, crossover, kabel DIN/BCK/WS, supply DAC, dan amplifier. Uji channel secara terpisah pada volume rendah. |
| OLED tidak menyala | Periksa SDA/SCL GPIO 21/22, ground, supply, dan alamat I2C `0x3C`. OLED tidak merespons tidak otomatis berarti audio DSP berhenti. |
| Safe Mode / output mute | Catat log Serial dan fault pada layar; periksa bus I2S, queue/DSP, memori, dan supply. Tahan tombol 5 detik hanya untuk mencoba clear setelah penyebab dipahami. |
| `CFG_E` atau konfigurasi kembali default | Periksa log NVS/CRC. Firmware mencoba recovery record; backup konfigurasi eksternal tidak tersedia otomatis. |
| Pengaturan tidak tersimpan | Tunggu 3 detik tanpa perubahan lalu pastikan toast `[NVS SAVED]` muncul. Periksa Serial bila save gagal. |
| Level pecah atau indikator `[LIMIT]` aktif | Turunkan master/gain output, kurangi boost PEQ, dan periksa level source serta headroom. Limiter bukan pengganti pengaturan gain yang benar. |
| Delay tidak mencapai 20 ms | Batas firmware aktual 2,8 ms. Slider UI saat ini memiliki skala visual 20 ms; nilai edit tetap dibatasi firmware. |

## Batas dan keselamatan

Jangan gunakan proteksi limiter sebagai perlindungan driver terhadap crossover yang salah, over-power, DC fault, atau kesalahan wiring. Konfirmasi filter dan batas daya pada hardware nyata. Dokumentasi arsitektur tersedia di [DOCS.md](DOCS.md), kontrol di [USER_GUIDE.md](USER_GUIDE.md), dan pin di [WIRING_REFERENCE.md](WIRING_REFERENCE.md).
