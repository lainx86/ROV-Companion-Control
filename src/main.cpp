#include <curses.h>
#include <arpa/inet.h>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <netdb.h>
#include <regex>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cerrno>
#include <ctime>

namespace {

constexpr std::size_t kMaxLogs = 500;
std::atomic<bool> interrupted{false};

struct Config {
  std::string ip{"192.168.1.198"};
  std::string cam0_device{"/dev/video0"};
  int cam0_port{5070};
  std::string cam1_device{"/dev/video2"};
  int cam1_port{5090};
  int mav_port0{14550};
  int mav_port1{14551};
  std::string mav_device{"/dev/ttyACM0"};
  int baudrate{57600};
  bool autostart{false};
};

std::string homeConfigPath() {
  const char* home = std::getenv("HOME");
  return std::string(home ? home : ".") + "/.rov_control.json";
}

std::string timestamp() {
  std::time_t now = std::time(nullptr);
  std::tm local{};
  localtime_r(&now, &local);
  char buffer[12];
  std::strftime(buffer, sizeof(buffer), "[%H:%M:%S]", &local);
  return buffer;
}

bool validPort(int value) { return value >= 1 && value <= 65535; }

bool validate(Config& cfg, std::string& error) {
  in_addr address{};
  if (inet_pton(AF_INET, cfg.ip.c_str(), &address) != 1) {
    error = "Target IP tidak valid";
    return false;
  }
  if (cfg.cam0_device.empty() || cfg.cam1_device.empty() || cfg.mav_device.empty()) {
    error = "Device kamera dan MAV tidak boleh kosong";
    return false;
  }
  if (!validPort(cfg.cam0_port) || !validPort(cfg.cam1_port) ||
      !validPort(cfg.mav_port0) || !validPort(cfg.mav_port1)) {
    error = "Port harus berada di antara 1 dan 65535";
    return false;
  }
  if (cfg.baudrate <= 0) {
    error = "Baudrate harus lebih besar dari 0";
    return false;
  }
  return true;
}

std::string jsonEscape(const std::string& value) {
  std::string out;
  for (char c : value) {
    if (c == '\\' || c == '"') out.push_back('\\');
    out.push_back(c);
  }
  return out;
}

bool saveConfig(const Config& cfg) {
  std::ofstream file(homeConfigPath());
  if (!file) return false;
  file << "{\n"
       << "  \"ip\": \"" << jsonEscape(cfg.ip) << "\",\n"
       << "  \"cam0_device\": \"" << jsonEscape(cfg.cam0_device) << "\",\n"
       << "  \"cam0_port\": " << cfg.cam0_port << ",\n"
       << "  \"cam1_device\": \"" << jsonEscape(cfg.cam1_device) << "\",\n"
       << "  \"cam1_port\": " << cfg.cam1_port << ",\n"
       << "  \"mav_port0\": " << cfg.mav_port0 << ",\n"
       << "  \"mav_port1\": " << cfg.mav_port1 << ",\n"
       << "  \"mav_device\": \"" << jsonEscape(cfg.mav_device) << "\",\n"
       << "  \"baudrate\": " << cfg.baudrate << ",\n"
       << "  \"autostart\": " << (cfg.autostart ? "true" : "false") << "\n}\n";
  return static_cast<bool>(file);
}

std::string readFile(const std::string& path) {
  std::ifstream file(path);
  return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

std::string jsonValue(const std::string& source, const std::string& key) {
  const std::regex pattern("\\\"" + key + "\\\"\\s*:\\s*(\\\"([^\\\"]*)\\\"|true|false|-?[0-9]+)");
  std::smatch match;
  if (!std::regex_search(source, match, pattern)) return {};
  return match[2].matched ? match[2].str() : match[1].str();
}

Config loadConfig() {
  Config cfg;
  const std::string source = readFile(homeConfigPath());
  if (source.empty()) return cfg;
  auto stringField = [&](const char* key, std::string& target) {
    const auto value = jsonValue(source, key);
    if (!value.empty()) target = value;
  };
  auto intField = [&](const char* key, int& target) {
    const auto value = jsonValue(source, key);
    try { if (!value.empty()) target = std::stoi(value); } catch (...) {}
  };
  stringField("ip", cfg.ip); stringField("cam0_device", cfg.cam0_device);
  stringField("cam1_device", cfg.cam1_device); stringField("mav_device", cfg.mav_device);
  intField("cam0_port", cfg.cam0_port); intField("cam1_port", cfg.cam1_port);
  intField("mav_port0", cfg.mav_port0); intField("mav_port1", cfg.mav_port1);
  intField("baudrate", cfg.baudrate);
  cfg.autostart = jsonValue(source, "autostart") == "true";
  std::string error;
  return validate(cfg, error) ? cfg : Config{};
}

std::string commandOutput(const std::string& command) {
  std::string result;
  FILE* pipe = popen(command.c_str(), "r");
  if (!pipe) return result;
  char buffer[256];
  while (fgets(buffer, sizeof(buffer), pipe)) result += buffer;
  pclose(pipe);
  return result;
}

std::pair<std::string, std::string> localIpv4() {
  const auto route = commandOutput("ip -4 route get 1.1.1.1 2>/dev/null");
  std::smatch match;
  std::string ip, iface;
  if (std::regex_search(route, match, std::regex("\\bsrc\\s+([0-9.]+)"))) ip = match[1];
  if (std::regex_search(route, match, std::regex("\\bdev\\s+(\\S+)"))) iface = match[1];
  return {ip, iface};
}

std::string interfaceCidr(const std::string& iface) {
  if (iface.empty()) return {};
  const auto out = commandOutput("ip -4 -o addr show dev " + iface + " 2>/dev/null");
  std::smatch match;
  return std::regex_search(out, match, std::regex("inet\\s+([0-9.]+/[0-9]+)")) ? match[1].str() : "";
}

std::vector<std::string> cidrHosts(const std::string& cidr) {
  const auto slash = cidr.find('/');
  if (slash == std::string::npos) return {};
  int prefix = 0;
  try { prefix = std::stoi(cidr.substr(slash + 1)); } catch (...) { return {}; }
  if (prefix < 16 || prefix > 30) return {};
  in_addr address{};
  if (inet_pton(AF_INET, cidr.substr(0, slash).c_str(), &address) != 1) return {};
  const uint32_t raw = ntohl(address.s_addr);
  const uint32_t mask = 0xffffffffu << (32 - prefix);
  const uint32_t first = (raw & mask) + 1;
  const uint32_t last = (raw | ~mask) - 1;
  if (last < first || last - first + 1 > 1022) return {};
  std::vector<std::string> hosts;
  for (uint32_t value = first; value <= last; ++value) {
    in_addr host{htonl(value)};
    char text[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &host, text, sizeof(text));
    hosts.emplace_back(text);
  }
  return hosts;
}

bool tcpAlive(const std::string& ip, int port) {
  int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (socket_fd < 0) return false;
  timeval timeout{0, 500000};
  setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
  sockaddr_in address{};
  address.sin_family = AF_INET; address.sin_port = htons(port);
  inet_pton(AF_INET, ip.c_str(), &address.sin_addr);
  const bool alive = connect(socket_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0;
  close(socket_fd);
  return alive;
}

bool hostAlive(const std::string& ip) {
  const std::string ping = "ping -c 1 -W 1 " + ip + " >/dev/null 2>&1";
  if (std::system(ping.c_str()) == 0) return true;
  return tcpAlive(ip, 22) || tcpAlive(ip, 80) || tcpAlive(ip, 443);
}

bool routineOutput(const std::string& line) {
  static const std::vector<std::string> ignored{"Setting pipeline to ", "Pipeline is PREROLL", "New clock: ", "Redistribute latency"};
  return std::any_of(ignored.begin(), ignored.end(), [&](const auto& prefix) { return line.rfind(prefix, 0) == 0; });
}

class ManagedProcess {
 public:
  using Log = std::function<void(const std::string&)>;
  ManagedProcess(std::string name, std::vector<std::string> command, Log log, std::function<void()> changed)
      : name_(std::move(name)), command_(std::move(command)), log_(std::move(log)), changed_(std::move(changed)) {}
  ~ManagedProcess() { stop(); }

  bool start() {
    if (running()) { log_("[INFO] " + name_ + " sudah berjalan"); return true; }
    int pipefd[2];
    if (pipe(pipefd) != 0) { log_("[ERROR] Pipe " + name_ + ": " + std::strerror(errno)); return false; }
    const pid_t child = fork();
    if (child == 0) {
      setsid();
      dup2(pipefd[1], STDOUT_FILENO); dup2(pipefd[1], STDERR_FILENO);
      close(pipefd[0]); close(pipefd[1]);
      std::vector<char*> argv;
      for (auto& part : command_) argv.push_back(const_cast<char*>(part.c_str()));
      argv.push_back(nullptr);
      execvp(argv.front(), argv.data());
      dprintf(STDERR_FILENO, "exec %s gagal: %s\n", argv.front(), std::strerror(errno));
      _exit(127);
    }
    if (child < 0) { close(pipefd[0]); close(pipefd[1]); log_("[ERROR] Fork " + name_ + ": " + std::strerror(errno)); return false; }
    close(pipefd[1]);
    { std::lock_guard<std::mutex> guard(lock_); pid_ = child; }
    reader_ = std::thread([this, fd = pipefd[0]] { readOutput(fd); });
    log_("[INFO] Starting " + name_ + "..."); changed_(); return true;
  }

  bool running() {
    std::lock_guard<std::mutex> guard(lock_);
    if (pid_ <= 0) return false;
    int status = 0;
    const pid_t result = waitpid(pid_, &status, WNOHANG);
    if (result == pid_) { pid_ = -1; changed_(); return false; }
    return result == 0;
  }

  void stop() {
    pid_t pid = -1;
    { std::lock_guard<std::mutex> guard(lock_); pid = pid_; }
    if (pid <= 0) { if (reader_.joinable()) reader_.join(); return; }
    log_("[INFO] Stopping " + name_ + "...");
    kill(-pid, SIGTERM);
    int status = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline && waitpid(pid, &status, WNOHANG) == 0) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    if (waitpid(pid, &status, WNOHANG) == 0) { log_("[WARNING] " + name_ + " force kill"); kill(-pid, SIGKILL); waitpid(pid, &status, 0); }
    { std::lock_guard<std::mutex> guard(lock_); pid_ = -1; }
    if (reader_.joinable()) reader_.join();
    changed_();
  }

  const std::string& name() const { return name_; }

 private:
  void readOutput(int fd) {
    FILE* stream = fdopen(fd, "r");
    if (!stream) { close(fd); return; }
    char* line = nullptr; size_t length = 0;
    while (getline(&line, &length, stream) != -1) {
      std::string text(line); while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
      if (!text.empty() && !routineOutput(text)) log_("[" + name_ + "] " + text);
    }
    free(line); fclose(stream);
  }
  std::string name_; std::vector<std::string> command_; Log log_; std::function<void()> changed_;
  pid_t pid_{-1}; std::mutex lock_; std::thread reader_;
};

class Controller {
 public:
  Controller(ManagedProcess::Log log, std::function<void()> changed) : log_(std::move(log)), changed_(std::move(changed)) {}
  void start(const Config& cfg) {
    if (std::any_of(processes_.begin(), processes_.end(), [](const auto& process) { return process->running(); })) { log_("[WARNING] Ada proses yang masih berjalan"); return; }
    stop(); processes_.clear();
    auto camera = [&](const std::string& device, int port) {
      return std::vector<std::string>{"gst-launch-1.0", "-q", "v4l2src", "device=" + device, "!", "image/jpeg,width=1280,height=720,framerate=30/1", "!", "queue", "max-size-buffers=2", "leaky=downstream", "!", "jpegdec", "!", "videoconvert", "!", "video/x-raw,format=I420", "!", "x264enc", "tune=zerolatency", "bitrate=2000", "speed-preset=ultrafast", "!", "rtph264pay", "config-interval=1", "pt=96", "!", "udpsink", "host=" + cfg.ip, "port=" + std::to_string(port), "sync=false", "async=false"};
    };
    processes_.push_back(std::make_unique<ManagedProcess>("CAM0", camera(cfg.cam0_device, cfg.cam0_port), log_, changed_));
    processes_.push_back(std::make_unique<ManagedProcess>("CAM1", camera(cfg.cam1_device, cfg.cam1_port), log_, changed_));
    processes_.push_back(std::make_unique<ManagedProcess>("MAVProxy", std::vector<std::string>{"mavproxy.py", "--master=" + cfg.mav_device, "--baudrate=" + std::to_string(cfg.baudrate), "--out=udp:" + cfg.ip + ":" + std::to_string(cfg.mav_port0), "--out=udp:" + cfg.ip + ":" + std::to_string(cfg.mav_port1)}, log_, changed_));
    log_("[INFO] Memulai semua stream..."); for (auto& process : processes_) process->start(); changed_();
  }
  void stop() { for (auto it = processes_.rbegin(); it != processes_.rend(); ++it) (*it)->stop(); changed_(); }
  std::map<std::string, bool> states() { std::map<std::string, bool> out{{"CAM0", false}, {"CAM1", false}, {"MAVProxy", false}}; for (const auto& process : processes_) out[process->name()] = process->running(); return out; }
 private:
  ManagedProcess::Log log_; std::function<void()> changed_; std::vector<std::unique_ptr<ManagedProcess>> processes_;
};

enum class EventType { Log, Refresh, ScanDone, ScanError };
struct Event { EventType type; std::string text; std::vector<std::string> ips; };

class App {
 public:
  App() : cfg_(loadConfig()), last_valid_(cfg_), controller_([this](const std::string& text) { event(EventType::Log, timestamp() + " " + text); }, [this] { event(EventType::Refresh); }) {
    auto local = localIpv4(); local_ip_ = local.first; interface_ = local.second;
  }
  ~App() { shutdown(); }
  int run() {
    initscr(); cbreak(); noecho(); keypad(stdscr, TRUE); timeout(100); curs_set(0);
    if (has_colors()) { start_color(); use_default_colors(); init_pair(1, COLOR_CYAN, -1); init_pair(2, COLOR_GREEN, -1); init_pair(3, COLOR_RED, -1); }
    const auto auto_start = cfg_.autostart ? std::chrono::steady_clock::now() + std::chrono::milliseconds(1500) : std::chrono::steady_clock::time_point::max();
    while (running_ && !interrupted) {
      drain(); if (cfg_.autostart && !auto_started_ && std::chrono::steady_clock::now() >= auto_start) { auto_started_ = true; start(); }
      draw(); const int key = getch(); if (key != ERR) handle(key);
    }
    endwin(); shutdown(); return 0;
  }

 private:
  using Field = std::pair<std::string, std::string>;
  static constexpr const char* kWordmark[5] = {"       _   _            _    _       ", "      / \\ | |_ ___ _ __| | _(_) __ _ ", "     / _ \\| __/ _ \\ '__| |/ / |/ _` |", "    / ___ \\ ||  __/ |  |   <| | (_| |", "   /_/   \\_\\__\\___|_|  |_|\\_\\_|\\__,_|"};
  std::mutex event_lock_; std::deque<Event> events_; Config cfg_, last_valid_; Controller controller_;
  std::vector<std::string> logs_, scan_ips_; std::string local_ip_, interface_, message_{"Siap. Tekan H untuk bantuan."};
  bool running_{true}, auto_started_{false}, scanning_{false}; int mode_{0}, edit_index_{0}, scan_index_{0}, log_scroll_{0}; std::vector<Field> edit_;

  void event(EventType type, std::string text = {}, std::vector<std::string> ips = {}) { std::lock_guard<std::mutex> guard(event_lock_); events_.push_back({type, std::move(text), std::move(ips)}); }
  void drain() { std::deque<Event> pending; { std::lock_guard<std::mutex> guard(event_lock_); pending.swap(events_); } for (auto& e : pending) { if (e.type == EventType::Log) { logs_.push_back(std::move(e.text)); if (logs_.size() > kMaxLogs) logs_.erase(logs_.begin()); } else if (e.type == EventType::ScanDone) { scanning_ = false; scan_ips_ = std::move(e.ips); if (scan_ips_.empty()) message_ = "Tidak ditemukan host"; else { mode_ = 2; scan_index_ = 0; message_ = "Pilih target IP"; } } else if (e.type == EventType::ScanError) { scanning_ = false; message_ = e.text; logs_.push_back(timestamp() + " [ERROR] " + e.text); } } }
  void add(int y, int x, const std::string& value, int width = -1, int attr = 0) { int rows, cols; getmaxyx(stdscr, rows, cols); if (y < 0 || y >= rows || x >= cols) return; attron(attr); mvaddnstr(y, x, value.c_str(), width < 0 ? cols - x : std::min(width, cols - x)); attroff(attr); }
  void box(int y, int x, int h, int w, const std::string& title = {}) { if (h < 2 || w < 2) return; mvaddch(y, x, ACS_ULCORNER); mvaddch(y, x + w - 1, ACS_URCORNER); mvaddch(y + h - 1, x, ACS_LLCORNER); mvaddch(y + h - 1, x + w - 1, ACS_LRCORNER); mvhline(y, x + 1, ACS_HLINE, w - 2); mvhline(y + h - 1, x + 1, ACS_HLINE, w - 2); mvvline(y + 1, x, ACS_VLINE, h - 2); mvvline(y + 1, x + w - 1, ACS_VLINE, h - 2); if (!title.empty()) add(y, x + 2, " " + title + " ", w - 4, A_BOLD); }
  std::vector<std::string> wrapLogs(int width) const { std::vector<std::string> out; for (const auto& line : logs_) for (std::size_t pos = 0; pos < line.size() || (line.empty() && pos == 0); pos += std::max(1, width)) out.push_back(line.substr(pos, width)); return out; }
  void wordmark(int x, int width) { for (int i = 0; i < 5; ++i) add(4 + i, x + std::max(1, (width - static_cast<int>(std::strlen(kWordmark[i]))) / 2), kWordmark[i], width - 2, COLOR_PAIR(1) | A_DIM); }
  void draw() {
    erase(); int rows, cols; getmaxyx(stdscr, rows, cols); if (rows < 22 || cols < 78) { add(1, 2, "Terminal terlalu kecil. Perbesar minimal menjadi 78x22.", -1, A_BOLD); refresh(); return; }
    add(0, 2, "ROV STREAM CONTROL", -1, A_BOLD | COLOR_PAIR(1)); add(0, std::max(30, cols - 36), "Local: " + (local_ip_.empty() ? std::string("-") : local_ip_) + (interface_.empty() ? "" : " (" + interface_ + ")"), 34);
    const bool brand = cols >= 145; int config_x = 1, config_w, status_x, status_w;
    if (brand) { int usable = cols - 4, brand_w = std::max(48, usable * 34 / 100); config_x = brand_w + 2; config_w = std::max(48, usable * 36 / 100); status_x = config_x + config_w + 1; status_w = usable - brand_w - config_w; box(2, 1, 10, brand_w); wordmark(1, brand_w); }
    else { config_w = cols * 3 / 5 - 2; status_x = config_x + config_w + 1; status_w = cols - status_x - 1; }
    box(2, config_x, 10, config_w, "Konfigurasi aktif");
    std::vector<std::string> config{"Target IP : " + cfg_.ip, "CAM0      : " + cfg_.cam0_device + " -> UDP " + std::to_string(cfg_.cam0_port), "CAM1      : " + cfg_.cam1_device + " -> UDP " + std::to_string(cfg_.cam1_port), "MAV       : " + cfg_.mav_device + " @ " + std::to_string(cfg_.baudrate) + " baud", "MAV UDP   : " + std::to_string(cfg_.mav_port0) + ", " + std::to_string(cfg_.mav_port1), "Auto start: " + std::string(cfg_.autostart ? "Ya" : "Tidak")};
    for (int i = 0; i < static_cast<int>(config.size()); ++i) add(3 + i, config_x + 2, config[i], config_w - 4);
    box(2, status_x, 10, status_w, "Status"); const auto states = controller_.states();
    int i = 0; for (const auto& name : {std::string("CAM0"), std::string("CAM1"), std::string("MAVProxy")}) { const bool active = states.at(name); add(4 + i++ * 2, status_x + 2, name + "  " + (active ? "● RUNNING" : "○ STOPPED"), status_w - 4, (active ? COLOR_PAIR(2) : COLOR_PAIR(3)) | A_BOLD); }
    const int log_y = 13, log_h = rows - 17; box(log_y, 1, log_h, cols - 2, "Log aktivitas"); auto visual = wrapLogs(cols - 6); const int available = std::max(1, log_h - 2); log_scroll_ = std::min(log_scroll_, std::max(0, static_cast<int>(visual.size()) - 1)); const int end = std::max(0, static_cast<int>(visual.size()) - log_scroll_); for (int n = std::max(0, end - available), y = 0; n < end; ++n, ++y) add(log_y + 1 + y, 3, visual[n], cols - 6);
    add(rows - 3, 2, message_, cols - 4, A_BOLD); add(rows - 1, 2, "S Start  X Stop  E Edit  N Scan  W Simpan  C Clear  ↑↓ Log  H Help  Q Keluar", cols - 4, A_REVERSE);
    if (mode_ == 1) drawEdit(rows, cols); else if (mode_ == 2) drawScan(rows, cols); else if (mode_ == 3) drawHelp(rows, cols); refresh();
  }
  void drawEdit(int rows, int cols) { const int h = 16, w = 70, y = (rows - h) / 2, x = (cols - w) / 2; box(y, x, h, w, "Edit konfigurasi"); for (int i = 0; i < static_cast<int>(edit_.size()); ++i) { int attr = i == edit_index_ ? A_REVERSE : 0; add(y + 1 + i, x + 2, edit_[i].first, 18, attr); add(y + 1 + i, x + 22, edit_[i].second, 44, attr); } add(y + 13, x + 2, "Tab/↑↓ pindah · Space Auto start · Enter terapkan · Esc batal", 66, A_BOLD); }
  void drawScan(int rows, int cols) { const int h = std::min(20, rows - 4), w = 42, y = (rows - h) / 2, x = (cols - w) / 2; box(y, x, h, w, "Hasil scan Ethernet"); for (int i = 0; i < std::min(h - 3, static_cast<int>(scan_ips_.size())); ++i) add(y + 1 + i, x + 3, scan_ips_[i], 35, i == scan_index_ ? A_REVERSE : 0); add(y + h - 2, x + 2, "↑↓ pilih · Enter gunakan IP · Esc tutup", 38, A_BOLD); }
  void drawHelp(int rows, int cols) { const int h = 11, w = 70, y = (rows - h) / 2, x = (cols - w) / 2; box(y, x, h, w, "Bantuan"); const std::vector<std::string> help{"S Start semua stream", "X Stop semua stream", "E Edit konfigurasi", "N Scan Ethernet", "W Simpan konfigurasi", "C Clear log · ↑/↓ gulir log", "Q/Ctrl+C stop, simpan, keluar", "Tekan tombol apa pun untuk kembali"}; for (int i = 0; i < static_cast<int>(help.size()); ++i) add(y + 1 + i, x + 2, help[i], 66); }
  void openEdit() { edit_ = {{"Target IP", cfg_.ip}, {"CAM0 device", cfg_.cam0_device}, {"CAM0 UDP port", std::to_string(cfg_.cam0_port)}, {"CAM1 device", cfg_.cam1_device}, {"CAM1 UDP port", std::to_string(cfg_.cam1_port)}, {"MAV serial device", cfg_.mav_device}, {"MAV baudrate", std::to_string(cfg_.baudrate)}, {"MAV UDP out 1", std::to_string(cfg_.mav_port0)}, {"MAV UDP out 2", std::to_string(cfg_.mav_port1)}, {"Auto start", cfg_.autostart ? "Ya" : "Tidak"}}; edit_index_ = 0; mode_ = 1; }
  bool saveEdit() { Config next = cfg_; try { next.ip=edit_[0].second; next.cam0_device=edit_[1].second; next.cam0_port=std::stoi(edit_[2].second); next.cam1_device=edit_[3].second; next.cam1_port=std::stoi(edit_[4].second); next.mav_device=edit_[5].second; next.baudrate=std::stoi(edit_[6].second); next.mav_port0=std::stoi(edit_[7].second); next.mav_port1=std::stoi(edit_[8].second); next.autostart=edit_[9].second=="Ya"; } catch (...) { message_="Nilai angka tidak valid"; return false; } std::string error; if (!validate(next,error)) { message_=error; return false; } cfg_=last_valid_=next; mode_=0; message_="Konfigurasi diterapkan. Tekan W untuk menyimpan."; return true; }
  void start() { std::string error; if (!validate(cfg_, error)) { message_=error; return; } last_valid_=cfg_; saveConfig(cfg_); controller_.start(cfg_); }
  void scan() { if (scanning_) { message_="Scan masih berjalan"; return; } scanning_=true; message_="Memindai Ethernet di latar belakang..."; event(EventType::Log, timestamp()+" [INFO] Scanning Ethernet network..."); std::thread([this] { auto local=localIpv4(); std::string cidr=interfaceCidr(local.second); if(cidr.empty()&&!local.first.empty()) cidr=local.first.substr(0,local.first.rfind('.'))+".0/24"; auto hosts=cidrHosts(cidr); if(hosts.empty()) { event(EventType::ScanError, "Tidak bisa menentukan subnet Ethernet"); return; } std::atomic<std::size_t> next{0}; std::vector<std::string> alive; std::mutex alive_lock; std::vector<std::thread> workers; for(int n=0;n<32;++n) workers.emplace_back([&] { while(true) { auto index=next++; if(index>=hosts.size()) break; if(hostAlive(hosts[index])) { std::lock_guard<std::mutex> lock(alive_lock); alive.push_back(hosts[index]); } } }); for(auto& worker:workers) worker.join(); std::sort(alive.begin(),alive.end()); event(EventType::ScanDone, {}, alive); }).detach(); }
  void handle(int key) { if (mode_ == 3) { mode_=0; return; } if (mode_ == 2) { if(key==27) mode_=0; else if(key==KEY_UP) scan_index_=std::max(0,scan_index_-1); else if(key==KEY_DOWN) scan_index_=std::min(static_cast<int>(scan_ips_.size())-1,scan_index_+1); else if(key=='\n'||key==KEY_ENTER) { cfg_.ip=scan_ips_[scan_index_]; last_valid_=cfg_; mode_=0; message_="Target IP diubah ke "+cfg_.ip; } return; } if (mode_ == 1) { if(key==27) { mode_=0; return; } if(key==KEY_UP) edit_index_=(edit_index_+9)%10; else if(key==KEY_DOWN||key=='\t') edit_index_=(edit_index_+1)%10; else if(key=='\n'||key==KEY_ENTER) saveEdit(); else if(edit_index_==9&&key==' ') edit_[9].second=edit_[9].second=="Ya"?"Tidak":"Ya"; else if(key==KEY_BACKSPACE||key==127||key==8) edit_[edit_index_].second.pop_back(); else if(key>=32&&key<=126&&edit_index_!=9) edit_[edit_index_].second.push_back(static_cast<char>(key)); return; } switch(key) { case 's': case 'S': start(); break; case 'x': case 'X': controller_.stop(); message_="Semua stream dihentikan"; break; case 'e': case 'E': openEdit(); break; case 'n': case 'N': scan(); break; case 'w': case 'W': if(saveConfig(cfg_)) message_="Konfigurasi disimpan"; else message_="Gagal menyimpan konfigurasi"; break; case 'c': case 'C': logs_.clear(); log_scroll_=0; break; case 'h': case 'H': case '?': mode_=3; break; case 'q': case 'Q': running_=false; break; case KEY_UP: ++log_scroll_; break; case KEY_DOWN: log_scroll_=std::max(0,log_scroll_-1); break; case KEY_PPAGE: log_scroll_+=10; break; case KEY_NPAGE: log_scroll_=std::max(0,log_scroll_-10); break; } }
  void shutdown() { static bool done=false; if(done) return; done=true; controller_.stop(); saveConfig(last_valid_); }
};

void signalHandler(int) { interrupted = true; }

}  // namespace

int main() {
  std::signal(SIGINT, signalHandler);
  App app;
  return app.run();
}
