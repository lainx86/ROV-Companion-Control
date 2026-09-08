#include "app.hpp"

#include <algorithm>
#include <cstring>
#include <curses.h>

namespace rov {

namespace {
constexpr const char *kWordmark[5] = {
    "       _   _            _    _       ",
    "      / \\ | |_ ___ _ __| | _(_) __ _ ",
    "     / _ \\| __/ _ \\ '__| |/ / |/ _` |",
    "    / ___ \\ ||  __/ |  |   <| | (_| |",
    "   /_/   \\_\\__\\___|_|  |_|\\_\\_|\\__,_|"};
} // namespace

void App::add(int y, int x, const std::string &value, int width, int attr) {
  int rows, cols;
  getmaxyx(stdscr, rows, cols);
  if (y < 0 || y >= rows || x >= cols)
    return;
  attron(attr);
  mvaddnstr(y, x, value.c_str(),
            width < 0 ? cols - x : std::min(width, cols - x));
  attroff(attr);
}

void App::box(int y, int x, int h, int w, const std::string &title) {
  if (h < 2 || w < 2)
    return;
  mvaddch(y, x, ACS_ULCORNER);
  mvaddch(y, x + w - 1, ACS_URCORNER);
  mvaddch(y + h - 1, x, ACS_LLCORNER);
  mvaddch(y + h - 1, x + w - 1, ACS_LRCORNER);
  mvhline(y, x + 1, ACS_HLINE, w - 2);
  mvhline(y + h - 1, x + 1, ACS_HLINE, w - 2);
  mvvline(y + 1, x, ACS_VLINE, h - 2);
  mvvline(y + 1, x + w - 1, ACS_VLINE, h - 2);
  if (!title.empty())
    add(y, x + 2, " " + title + " ", w - 4, A_BOLD);
}

std::vector<std::string> App::wrapLogs(int width) const {
  std::vector<std::string> out;
  for (const auto &line : logs_)
    for (std::size_t pos = 0; pos < line.size() || (line.empty() && pos == 0);
         pos += std::max(1, width))
      out.push_back(line.substr(pos, width));
  return out;
}

void App::wordmark(int x, int width) {
  for (int i = 0; i < 5; ++i)
    add(4 + i,
        x + std::max(1,
                     (width - static_cast<int>(std::strlen(kWordmark[i]))) / 2),
        kWordmark[i], width - 2, COLOR_PAIR(1) | A_DIM);
}

void App::draw() {
  erase();
  int rows, cols;
  getmaxyx(stdscr, rows, cols);
  if (rows < 22 || cols < 78) {
    add(1, 2, "Terminal terlalu kecil. Perbesar minimal menjadi 78x22.", -1,
        A_BOLD);
    refresh();
    return;
  }
  add(0, 2, "ROV STREAM CONTROL", -1, A_BOLD | COLOR_PAIR(1));
  add(0, std::max(30, cols - 36),
      "Local: " + (local_ip_.empty() ? std::string("-") : local_ip_) +
          (interface_.empty() ? "" : " (" + interface_ + ")"),
      34);
  const bool brand = cols >= 145;
  int config_x = 1, config_w, status_x, status_w;
  if (brand) {
    int usable = cols - 4, brand_w = std::max(48, usable * 34 / 100);
    config_x = brand_w + 2;
    config_w = std::max(48, usable * 36 / 100);
    status_x = config_x + config_w + 1;
    status_w = usable - brand_w - config_w;
    box(2, 1, 10, brand_w);
    wordmark(1, brand_w);
  } else {
    config_w = cols * 3 / 5 - 2;
    status_x = config_x + config_w + 1;
    status_w = cols - status_x - 1;
  }
  box(2, config_x, 10, config_w, "Konfigurasi aktif");
  std::vector<std::string> config{
      "Target IP : " + cfg_.ip,
      "CAM0      : " + cfg_.cam0_device + " -> UDP " +
          std::to_string(cfg_.cam0_port),
      "CAM1      : " + cfg_.cam1_device + " -> UDP " +
          std::to_string(cfg_.cam1_port),
      "MAV       : " + cfg_.mav_device + " @ " + std::to_string(cfg_.baudrate) +
          " baud",
      "MAV UDP   : " + std::to_string(cfg_.mav_port0) + ", " +
          std::to_string(cfg_.mav_port1),
      "Auto start: " + std::string(cfg_.autostart ? "Ya" : "Tidak")};
  for (int i = 0; i < static_cast<int>(config.size()); ++i)
    add(3 + i, config_x + 2, config[i], config_w - 4);
  box(2, status_x, 10, status_w, "Status");
  const auto states = controller_.states();
  int i = 0;
  for (const auto &name :
       {std::string("CAM0"), std::string("CAM1"), std::string("MAVProxy")}) {
    const bool active = states.at(name);
    add(4 + i++ * 2, status_x + 2,
        name + "  " + (active ? "● RUNNING" : "○ STOPPED"), status_w - 4,
        (active ? COLOR_PAIR(2) : COLOR_PAIR(3)) | A_BOLD);
  }
  const int log_y = 13, log_h = rows - 17;
  box(log_y, 1, log_h, cols - 2, "Log aktivitas");
  auto visual = wrapLogs(cols - 6);
  const int available = std::max(1, log_h - 2);
  log_scroll_ =
      std::min(log_scroll_, std::max(0, static_cast<int>(visual.size()) - 1));
  const int end = std::max(0, static_cast<int>(visual.size()) - log_scroll_);
  for (int n = std::max(0, end - available), y = 0; n < end; ++n, ++y)
    add(log_y + 1 + y, 3, visual[n], cols - 6);
  add(rows - 3, 2, message_, cols - 4, A_BOLD);
  add(rows - 1, 2,
      "S Start  X Stop  E Edit  N Scan  W Simpan  C Clear  ↑↓ Log  H Help  Q "
      "Keluar",
      cols - 4, A_REVERSE);
  if (mode_ == 1)
    drawEdit(rows, cols);
  else if (mode_ == 2)
    drawScan(rows, cols);
  else if (mode_ == 3)
    drawHelp(rows, cols);
  refresh();
}

void App::drawEdit(int rows, int cols) {
  const int h = 16, w = 70, y = (rows - h) / 2, x = (cols - w) / 2;
  box(y, x, h, w, "Edit konfigurasi");
  for (int i = 0; i < static_cast<int>(edit_.size()); ++i) {
    int attr = i == edit_index_ ? A_REVERSE : 0;
    add(y + 1 + i, x + 2, edit_[i].first, 18, attr);
    add(y + 1 + i, x + 22, edit_[i].second, 44, attr);
  }
  add(y + 13, x + 2,
      "Tab/↑↓ pindah · Space Auto start · Enter terapkan · Esc batal", 66,
      A_BOLD);
}

void App::drawScan(int rows, int cols) {
  const int h = std::min(20, rows - 4), w = 42, y = (rows - h) / 2,
            x = (cols - w) / 2;
  for (int line = 0; line < h; ++line)
    mvhline(y + line, x, ' ', w);
  box(y, x, h, w, "Hasil scan Ethernet");
  for (int i = 0; i < std::min(h - 3, static_cast<int>(scan_ips_.size())); ++i)
    add(y + 1 + i, x + 3, scan_ips_[i], 35, i == scan_index_ ? A_REVERSE : 0);
  add(y + h - 2, x + 2, "↑↓ pilih · Enter gunakan IP · Esc tutup", 38, A_BOLD);
}

void App::drawHelp(int rows, int cols) {
  const int h = 11, w = 70, y = (rows - h) / 2, x = (cols - w) / 2;
  for (int line = 0; line < h; ++line)
    mvhline(y + line, x, ' ', w);
  box(y, x, h, w, "Bantuan");
  const std::vector<std::string> help{"S Start semua stream",
                                      "X Stop semua stream",
                                      "E Edit konfigurasi",
                                      "N Scan Ethernet",
                                      "W Simpan konfigurasi",
                                      "C Clear log · ↑/↓ gulir log",
                                      "Q/Ctrl+C stop, simpan, keluar",
                                      "Tekan tombol apa pun untuk kembali"};
  for (int i = 0; i < static_cast<int>(help.size()); ++i)
    add(y + 1 + i, x + 2, help[i], 66);
}

} // namespace rov
