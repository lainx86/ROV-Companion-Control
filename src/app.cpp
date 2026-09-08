#include "app.hpp"
#include "network.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <curses.h>
#include <utility>

namespace rov {

namespace {
constexpr std::size_t kMaxLogs = 500;

std::string timestamp() {
  std::time_t now = std::time(nullptr);
  std::tm local{};
  localtime_r(&now, &local);
  char buffer[12];
  std::strftime(buffer, sizeof(buffer), "[%H:%M:%S]", &local);
  return buffer;
}
} // namespace

App::App(std::atomic<bool> &interrupted)
    : interrupted_(interrupted), cfg_(loadConfig()), last_valid_(cfg_),
      controller_(
          [this](const std::string &text) {
            event(EventType::Log, timestamp() + " " + text);
          },
          [this] { event(EventType::Refresh); }) {
  auto local = localIpv4();
  local_ip_ = local.first;
  interface_ = local.second;
}

App::~App() { shutdown(); }

int App::run() {
  initscr();
  cbreak();
  noecho();
  keypad(stdscr, TRUE);
  timeout(100);
  curs_set(0);
  if (has_colors()) {
    start_color();
    use_default_colors();
    init_pair(1, COLOR_CYAN, -1);
    init_pair(2, COLOR_GREEN, -1);
    init_pair(3, COLOR_RED, -1);
  }
  const auto auto_start =
      cfg_.autostart
          ? std::chrono::steady_clock::now() + std::chrono::milliseconds(1500)
          : std::chrono::steady_clock::time_point::max();
  while (running_ && !interrupted_) {
    drain();
    if (cfg_.autostart && !auto_started_ &&
        std::chrono::steady_clock::now() >= auto_start) {
      auto_started_ = true;
      start();
    }
    draw();
    const int key = getch();
    if (key != ERR)
      handle(key);
  }
  endwin();
  shutdown();
  return 0;
}

void App::event(EventType type, std::string text,
                std::vector<std::string> ips) {
  std::lock_guard<std::mutex> guard(event_lock_);
  events_.push_back({type, std::move(text), std::move(ips)});
}

void App::drain() {
  std::deque<Event> pending;
  {
    std::lock_guard<std::mutex> guard(event_lock_);
    pending.swap(events_);
  }
  for (auto &e : pending) {
    if (e.type == EventType::Log) {
      logs_.push_back(std::move(e.text));
      if (logs_.size() > kMaxLogs)
        logs_.erase(logs_.begin());
    } else if (e.type == EventType::ScanDone) {
      scanning_ = false;
      scan_ips_ = std::move(e.ips);
      if (scan_ips_.empty())
        message_ = "Tidak ditemukan host";
      else {
        mode_ = 2;
        scan_index_ = 0;
        message_ = "Pilih target IP";
      }
    } else if (e.type == EventType::ScanError) {
      scanning_ = false;
      message_ = e.text;
      logs_.push_back(timestamp() + " [ERROR] " + e.text);
    }
  }
}

void App::openEdit() {
  edit_ = {{"Target IP", cfg_.ip},
           {"CAM0 device", cfg_.cam0_device},
           {"CAM0 UDP port", std::to_string(cfg_.cam0_port)},
           {"CAM1 device", cfg_.cam1_device},
           {"CAM1 UDP port", std::to_string(cfg_.cam1_port)},
           {"MAV serial device", cfg_.mav_device},
           {"MAV baudrate", std::to_string(cfg_.baudrate)},
           {"MAV UDP out 1", std::to_string(cfg_.mav_port0)},
           {"MAV UDP out 2", std::to_string(cfg_.mav_port1)},
           {"Auto start", cfg_.autostart ? "Ya" : "Tidak"}};
  edit_index_ = 0;
  mode_ = 1;
}

bool App::saveEdit() {
  Config next = cfg_;
  try {
    next.ip = edit_[0].second;
    next.cam0_device = edit_[1].second;
    next.cam0_port = std::stoi(edit_[2].second);
    next.cam1_device = edit_[3].second;
    next.cam1_port = std::stoi(edit_[4].second);
    next.mav_device = edit_[5].second;
    next.baudrate = std::stoi(edit_[6].second);
    next.mav_port0 = std::stoi(edit_[7].second);
    next.mav_port1 = std::stoi(edit_[8].second);
    next.autostart = edit_[9].second == "Ya";
  } catch (...) {
    message_ = "Nilai angka tidak valid";
    return false;
  }
  std::string error;
  if (!validate(next, error)) {
    message_ = error;
    return false;
  }
  cfg_ = last_valid_ = next;
  mode_ = 0;
  message_ = "Konfigurasi diterapkan. Tekan W untuk menyimpan.";
  return true;
}

void App::start() {
  std::string error;
  if (!validate(cfg_, error)) {
    message_ = error;
    return;
  }
  last_valid_ = cfg_;
  saveConfig(cfg_);
  controller_.start(cfg_);
}

void App::handle(int key) {
  if (mode_ == 3) {
    mode_ = 0;
    return;
  }
  if (mode_ == 2) {
    if (key == 27)
      mode_ = 0;
    else if (key == KEY_UP)
      scan_index_ = std::max(0, scan_index_ - 1);
    else if (key == KEY_DOWN)
      scan_index_ =
          std::min(static_cast<int>(scan_ips_.size()) - 1, scan_index_ + 1);
    else if (key == '\n' || key == KEY_ENTER) {
      cfg_.ip = scan_ips_[scan_index_];
      last_valid_ = cfg_;
      mode_ = 0;
      message_ = "Target IP diubah ke " + cfg_.ip;
    }
    return;
  }
  if (mode_ == 1) {
    if (key == 27) {
      mode_ = 0;
      return;
    }
    if (key == KEY_UP)
      edit_index_ = (edit_index_ + 9) % 10;
    else if (key == KEY_DOWN || key == '\t')
      edit_index_ = (edit_index_ + 1) % 10;
    else if (key == '\n' || key == KEY_ENTER)
      saveEdit();
    else if (edit_index_ == 9 && key == ' ')
      edit_[9].second = edit_[9].second == "Ya" ? "Tidak" : "Ya";
    else if (key == KEY_BACKSPACE || key == 127 || key == 8)
      edit_[edit_index_].second.pop_back();
    else if (key >= 32 && key <= 126 && edit_index_ != 9)
      edit_[edit_index_].second.push_back(static_cast<char>(key));
    return;
  }
  switch (key) {
  case 's':
  case 'S':
    start();
    break;
  case 'x':
  case 'X':
    controller_.stop();
    message_ = "Semua stream dihentikan";
    break;
  case 'e':
  case 'E':
    openEdit();
    break;
  case 'n':
  case 'N':
    scan();
    break;
  case 'w':
  case 'W':
    if (saveConfig(cfg_))
      message_ = "Konfigurasi disimpan";
    else
      message_ = "Gagal menyimpan konfigurasi";
    break;
  case 'c':
  case 'C':
    logs_.clear();
    log_scroll_ = 0;
    break;
  case 'h':
  case 'H':
  case '?':
    mode_ = 3;
    break;
  case 'q':
  case 'Q':
    running_ = false;
    break;
  case KEY_UP:
    ++log_scroll_;
    break;
  case KEY_DOWN:
    log_scroll_ = std::max(0, log_scroll_ - 1);
    break;
  case KEY_PPAGE:
    log_scroll_ += 10;
    break;
  case KEY_NPAGE:
    log_scroll_ = std::max(0, log_scroll_ - 10);
    break;
  }
}

void App::scan() {
  if (scanning_) {
    message_ = "Scan masih berjalan";
    return;
  }
  if (scan_thread_.joinable())
    scan_thread_.join();
  scanning_ = true;
  scan_cancelled_ = false;
  message_ = "Memindai Ethernet di latar belakang...";
  event(EventType::Log, timestamp() + " [INFO] Scanning Ethernet network...");
  scan_thread_ = std::thread([this] {
    auto result = scanNetwork(scan_cancelled_);
    if (scan_cancelled_)
      return;
    if (!result.error.empty())
      event(EventType::ScanError, std::move(result.error));
    else
      event(EventType::ScanDone, {}, std::move(result.ips));
  });
}

void App::shutdown() {
  if (shutdown_done_)
    return;
  shutdown_done_ = true;
  scan_cancelled_ = true;
  controller_.stop();
  if (scan_thread_.joinable())
    scan_thread_.join();
  saveConfig(last_valid_);
}

} // namespace rov
