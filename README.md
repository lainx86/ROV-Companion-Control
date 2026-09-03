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
