# ROV Control C++ TUI

Penulisan ulang C++17 dari ROV Stream Control. Aplikasi menjalankan dua pipeline GStreamer dan MAVProxy, memindai Ethernet, menyimpan konfigurasi, menangkap output proses ke panel log, serta menghentikan seluruh stream saat keluar.

## Dependensi Raspberry Pi

```bash
sudo apt update
sudo apt install -y build-essential cmake libncurses-dev \
  gstreamer1.0-tools gstreamer1.0-plugins-base \
  gstreamer1.0-plugins-good gstreamer1.0-plugins-bad \
  gstreamer1.0-plugins-ugly v4l-utils
python3 -m pip install --user MAVProxy
```

## Build dan test

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Jalankan dari terminal minimal 78×22:

```bash
./build/rov_control
```

## Struktur kode

```text
src/
├── main.cpp          # Entry point dan penanganan SIGINT
├── config.hpp/.cpp   # Nilai default, validasi, baca/tulis konfigurasi JSON
├── process.hpp/.cpp  # Lifecycle proses anak dan penangkapan output
├── controller.hpp/.cpp # Pipeline kamera dan perintah MAVProxy
├── network.hpp/.cpp  # IP lokal, subnet, dan pemindaian host
├── app.hpp/.cpp      # Event loop, input keyboard, state, dan koordinasi modul
└── app_view.cpp      # Rendering dashboard serta panel edit, scan, dan bantuan
```

`rov_core` berisi modul konfigurasi, proses, controller, dan jaringan, tanpa
dependensi ncurses. Executable `rov_control` menambahkan lapisan TUI di atasnya.
Semua API modul berada di namespace `rov`; helper internal tetap privat di file
implementasi. Perubahan perintah GStreamer/MAVProxy berada di `controller.cpp`,
sedangkan perubahan tampilan berada di `app_view.cpp`.

Pemindaian jaringan mengembalikan `ScanResult` berisi daftar IP atau pesan error.
`App` menjalankannya dalam thread dan meneruskan hasilnya melalui antrean event.
Saat keluar, aplikasi membatalkan probe berikutnya dan menunggu probe aktif
selesai sebelum melepas thread scan.

`core_tests` memakai header dan library `rov_core` secara langsung. Test mencakup
validasi konfigurasi, subnet, pembatalan scan, filter log, status awal controller,
serta start/stop dan penangkapan output proses lokal; tidak memerlukan kamera,
MAVProxy, atau terminal interaktif.

## Shortcut

| Tombol | Fungsi |
| --- | --- |
| `S` | Simpan konfigurasi lalu mulai CAM0, CAM1, dan MAVProxy. |
| `X` | Hentikan semua stream. |
| `E` | Edit konfigurasi. |
| `N` | Scan Ethernet di latar belakang. |
| `W` | Simpan konfigurasi. |
| `C` | Bersihkan log. |
| `H` | Bantuan. |
| `Q` / `Ctrl+C` | Stop semua stream, simpan konfigurasi valid, dan keluar. |

Konfigurasi kompatibel dengan aplikasi Python sebelumnya dan tetap berada di `~/.rov_control.json`.

## Catatan

`mavproxy.py` harus tersedia dalam `PATH`, terutama saat dijalankan dari service. Output GStreamer dan MAVProxy tidak menulis langsung ke terminal; seluruh error dan diagnostic masuk ke panel log TUI.
