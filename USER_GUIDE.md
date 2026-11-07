# Panduan Pengguna

## Memulai

1. Nyalakan board dan pastikan inisialisasi berhasil melalui Serial Monitor 115200 baud.
2. Pada perangkat sumber, hubungkan Bluetooth ke sink bernama `DLMS BALAPs`.
3. Gunakan source audio dengan sample rate A2DP 44,1 kHz. Rate lain tidak di-resample dan akan membuat output mute.
4. Pastikan amplifier/driver tersambung ke output yang benar dan mulai pada volume rendah.

## Kontrol encoder

| Aksi | Hasil |
| --- | --- |
| Putar di HOME | Mengubah master volume. |
| Klik singkat di HOME | Membuka Main Menu. |
| Putar di menu | Memindahkan pilihan atau mengubah nilai saat mode edit aktif. |
| Klik singkat | Membuka pilihan, mulai edit, atau menyelesaikan edit sesuai layar. |
| Double-click | Kembali satu tingkat; jika sedang mengedit, keluar dari mode edit. Perubahan yang sudah dilakukan dengan memutar encoder langsung berlaku dan tidak di-undo. |
| Tahan minimal 600 ms | Kembali ke HOME dari layar mana pun. |
| Tahan sekitar 5 detik saat Safe Mode | Mencoba clear Safe Mode dan kembali ke HOME. Gunakan hanya setelah memeriksa fault. |

Single-click ditunda sekitar 300 ms untuk membedakan double-click. Menu akan kembali ke HOME setelah kurang lebih 10 detik tanpa aktivitas.

## Menu utama

1. **Master Volume** — rentang 0–100%. Bisa diubah langsung dari HOME atau melalui item ini.
2. **Presets** — Flat, Bass Boost, Vocal, Rock, dan Custom. Pemilihan preset mengatur gain PEQ; lakukan pengecekan level setelah menerapkan preset.
3. **Output Matrix** — untuk Channel A-D, mengatur source Left/Right/Mono, filter Sub/Mid/High/Full, gain -20 sampai +6 dB, mute, dan delay.
4. **Parametric EQ** — lima band. Setiap band menyediakan type Peaking/LowShelf/HighShelf, frekuensi 20–20.000 Hz, gain -12 sampai +12 dB, dan Q 0,1–10.
5. **Time Alignment** — delay terpisah untuk A-D, langkah 0,1 ms, rentang aktual 0–2,8 ms. Walaupun slider layar menampilkan skala sampai 20 ms, firmware tetap membatasi nilai ke 2,8 ms. Layar menampilkan ms, bukan jarak cm.
6. **Phase Invert** — toggle polaritas normal/invert (0°/180°) per channel.
7. **LR24 Crossover** — mengatur Sub LPF, Mid HPF, Mid LPF, dan High HPF.
8. **Bluetooth Info** — status koneksi, nama sink, serta judul metadata track. Ini bukan menu untuk memilih perangkat dari daftar.
9. **Diagnostics** — melihat perkiraan beban DSP, kondisi queue, drop/underrun, error I2S, dan status PSRAM.

## Mengedit parameter

Pada layar Channel Setup, PEQ Band Edit, Crossover, dan Time Alignment:

1. Putar encoder untuk memilih channel/field.
2. Klik untuk masuk mode edit.
3. Putar untuk mengubah nilai; perubahan berlaku pada setting firmware.
4. Klik untuk menyelesaikan edit. Double-click keluar dari mode edit atau kembali ke layar sebelumnya; nilai yang sudah diubah sebelumnya tetap berlaku.

Di menu Phase, klik channel terpilih untuk langsung toggle. Pada menu Matrix, pilih channel lalu klik untuk membuka setup channel.

## Batas crossover

Validator menjaga hubungan frekuensi agar `Sub LPF <= Mid HPF < Mid LPF <= High HPF`, dengan jarak minimal 50 Hz antara batas mid yang berdekatan serta lantai High HPF 1.500 Hz. Jika perlu, validator akan menyesuaikan field terkait agar urutan tetap valid.

Rentang/langkah pada encoder:

| Parameter | Rentang kontrol | Langkah |
| --- | ---: | ---: |
| Sub LPF | 20–500 Hz | 5 Hz |
| Mid HPF | 20–1.000 Hz | 5 Hz |
| Mid LPF | 500–15.000 Hz | 50 Hz |
| High HPF | 1.500–20.000 Hz | 50 Hz |

Batas antarparameter dapat menyebabkan nilai yang ditampilkan disesuaikan oleh validator setelah perubahan.

## Autosave dan audio

- Perubahan setting dijadwalkan untuk disimpan setelah 3 detik tanpa edit. Tunggu notifikasi `[NVS SAVED]` sebelum memutus daya bila perubahan terbaru perlu dipertahankan.
- NVS menggunakan pemeriksaan CRC dan recovery record. Jika setting tersimpan rusak dan recovery tidak tersedia/valid, firmware dapat kembali ke defaults.
- `Left`, `Right`, dan `Mono` mengacu pada kanal stream Bluetooth; Mono merupakan rata-rata kanal L/R. Tidak ada input AUX/analog aktif.
- Sample rate yang didukung saat ini adalah 44,1 kHz. Tidak ada konversi/resampling untuk 48 kHz atau rate lain.
- Turunkan volume saat menguji routing, crossover, atau perubahan PEQ; limiter bukan pengganti perlindungan driver.

Lihat [OLED_SCREENS.md](OLED_SCREENS.md) untuk penjelasan tampilan, [DOCS.md](DOCS.md) untuk arsitektur, [WIRING_REFERENCE.md](WIRING_REFERENCE.md) untuk wiring, dan [TIPS_TRICKS.md](TIPS_TRICKS.md) untuk commissioning.
