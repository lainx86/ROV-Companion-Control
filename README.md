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

Binary release tersedia untuk Linux x86-64 dan ARM64 (Raspberry Pi OS Bookworm
64-bit atau lebih baru). Binary dibangun pada Debian 12 dan membutuhkan glibc
2.36+, libstdc++ dari GCC 12+, serta ncurses 6. GStreamer, MAVProxy, `iproute2`,
dan `iputils-ping` tetap perlu dipasang pada perangkat. Pilih arsip yang sesuai
arsitektur, ekstrak, lalu jalankan `./rov_control` dari folder hasil ekstraksi.

Untuk membangun dari source:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Build kedua paket release dalam Debian 12 (ARM64 diuji melalui QEMU):

```bash
mkdir -p release-output
docker run --rm -v "$PWD:/src:ro" -v "$PWD/release-output:/out" \
  debian:bookworm-slim sh /src/scripts/build-release.sh
```

Jalankan dari terminal minimal 78×22:

```bash
./build/rov_control
```

Saat aplikasi dibuka, aplikasi lebih dahulu mencoba target IP yang tersimpan di
konfigurasi melalui Ethernet. Jika target merespons, IP tersebut langsung dipakai
dan semua stream dimulai tanpa scan subnet. Jika target tidak merespons, aplikasi
baru memindai Ethernet. IP lokal perangkat sendiri tidak dimasukkan ke hasil scan.
Satu-satunya host hasil scan langsung diterapkan dan disimpan; beberapa kandidat
ditampilkan untuk dipilih. Tombol `N` tetap membuka scan manual.

Setelah target IP startup diterapkan, CAM0, CAM1, dan MAVProxy langsung dimulai
dengan IP tersebut, tanpa perlu menekan `S`. Field `autostart` dari konfigurasi
lama diabaikan dan tidak lagi ditampilkan sebagai opsi di panel edit.
Scan gagal, hasil kosong, atau pembatalan pemilihan IP membatalkan autostart untuk
sesi tersebut; konfigurasi target sebelumnya tetap dipertahankan. Tombol `S`
tetap bisa dipakai setelahnya. Tombol `X` juga membatalkan autostart yang tertunda.
Scan manual melalui `N` tidak memulai ulang proses secara otomatis.

Scan startup dan tombol `N` hanya memakai Ethernet fisik dengan link aktif dan
alamat IPv4. Wi-Fi, loopback, serta interface virtual seperti Docker diabaikan.
Jika ada beberapa Ethernet yang memenuhi syarat, interface pertama dalam daftar
`ip link` digunakan. Probe ping dan TCP diikat ke interface tersebut, sehingga
tidak beralih ke rute default Wi-Fi. Tanpa Ethernet yang memenuhi syarat, aplikasi
menampilkan error dan membatalkan autostart untuk sesi itu. IP lokal di dashboard
juga mengikuti Ethernet. Aturan ini membatasi scan; rute stream tetap dikelola OS.

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

Jika interpreter Python 3 tersedia saat konfigurasi CMake, `startup_scan_tests`
juga menguji startup melalui terminal simulasi dengan perintah jaringan dan
stream tiruan, tanpa memindai jaringan nyata atau mengubah konfigurasi pengguna.
Skenario scan berhasil memerlukan metadata NIC Ethernet fisik di `/sys/class/net`
(kabel dan IP nyata tidak diperlukan), dan dilewati jika metadata itu tidak ada.
Klasifikasi Ethernet/Wi-Fi/interface virtual diuji terpisah dengan fixture sysfs
buatan pada `core_tests`.

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
