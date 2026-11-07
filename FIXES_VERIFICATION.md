# Verifikasi Perbaikan Firmware

**Diperbarui:** 2026-11-08  
**Metode:** peninjauan statis source pada `src/`. Status di bawah menjelaskan alur yang tampak pada kode; status tersebut bukan bukti build sukses atau validasi perangkat keras.

| Area | Perilaku yang terlihat pada source | Status dokumentasi | Verifikasi lanjutan |
| --- | --- | --- | --- |
| Heartbeat DSP | Supervisor memeriksa setiap 500 ms dan memicu Safe Mode bila heartbeat yang sudah mulai tidak berubah lebih dari 1.200 ms. | Sesuai source | Hentikan task audio secara terkontrol dan verifikasi fault serta mute output. |
| Batas sample rate A2DP | Callback menerima 44,1 kHz; rate lain dicatat sebagai fault dan audio dimute. Tidak ada resampler. | Sesuai source | Uji perangkat yang menegosiasikan 44,1 kHz dan rate lain; pastikan output rate lain tetap mute. |
| Queue underrun/overflow | Queue depth 20 blok, masing-masing maksimal 128 frame. Timeout receive sekitar 2 ms menulis hening; queue penuh menambah counter overflow/drop. | Sesuai source | Pause stream, lanjutkan stream, dan uji beban tanpa memodifikasi perangkat saat berdaya tinggi. |
| Dual-I2S write | Partial write pada salah satu bus memicu Safe Mode; lima kegagalan penuh berturut-turut juga memicu Safe Mode. | Sesuai source | Lakukan fault injection terkontrol untuk tiap bus dan pastikan output dimute. |
| Validasi konfigurasi NVS | Load memeriksa header, versi, ukuran dan CRC sebelum menerapkan snapshot; data invalid mencoba recovery record lalu defaults. | Sesuai source | Uji key/blob korup serta versi/ukuran yang tidak sesuai pada board uji. |
| Recovery saat save | Recovery record berisi setting dan CRC ditulis sebelum field utama; header/CRC utama menjadi marker akhir setelah payload berhasil. | Sesuai source | Lakukan power-cut test pada board uji dan pastikan recovery/default dapat dimuat. |
| Snapshot setting DSP | Setting bersama dilindungi critical section dan disalin ke snapshot di jalur pemrosesan blok. Snapshot invalid ditolak untuk memakai last-known-good. | Sesuai source | Ubah parameter berulang saat audio aktif; pantau error/drop dan dengarkan transien. |
| Bluetooth metadata | Status koneksi memakai atomic, judul track diakses melalui snapshot yang dilindungi critical section, dan hanya metadata judul diminta. | Sesuai source | Ganti track dan putus-sambung koneksi berulang sambil melihat OLED. |
| Delay time alignment | Batas aktual `MAX_DELAY_MS` adalah 2,8 ms dan langkah encoder 0,1 ms. Slider OLED masih memakai skala 0–20 ms. | Perbedaan UI/source dicatat | Pastikan nilai efektif tidak melebihi 2,8 ms; koreksi skala UI sebagai pekerjaan firmware terpisah. |
| Clear Safe Mode | Safe Mode dapat dicoba di-clear dengan menahan tombol encoder sekitar 5 detik. | Sesuai source | Uji hanya pada setup aman; jangan anggap clear sebagai perbaikan akar masalah. |

## Batas verifikasi

- Ini adalah inspeksi source, bukan laporan hasil build atau pengukuran runtime.
- Build PlatformIO tidak dijalankan untuk pembaruan dokumentasi ini.
- Tidak ada pengujian dengan board ESP32, DAC, amplifier, speaker, atau source Bluetooth pada sesi ini.
- Pengujian power-cut, fault injection, dan uji stabilitas jangka panjang memerlukan perangkat keras serta prosedur aman.

## Kriteria sebelum rilis

1. Build bersih untuk environment PlatformIO yang tercantum di `platformio.ini`.
2. Uji operasi 44,1 kHz pada perangkat target; verifikasi rate selain 44,1 kHz tetap mute.
3. Uji empat output, mute, crossover, PEQ, phase, dan delay pada volume rendah sebelum memasang driver/speaker.
4. Uji queue, Safe Mode, dan pemulihan NVS pada board uji; catat hasil dan versi firmware.
5. Konfirmasi konfigurasi crossover dan keselamatan driver fisik sesuai spesifikasi sistem speaker.
