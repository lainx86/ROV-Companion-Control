#pragma once

#include "config.hpp"
#include "controller.hpp"

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace rov {

class App {
public:
  explicit App(std::atomic<bool> &interrupted);
  ~App();
  int run();

private:
  enum class EventType {
    Log,
    Refresh,
    StartupTargetReady,
    ScanDone,
    ScanError
  };
  struct Event {
    EventType type;
    std::string text;
    std::vector<std::string> ips;
  };
  std::atomic<bool> &interrupted_;
  using Field = std::pair<std::string, std::string>;
  std::mutex event_lock_;
  std::deque<Event> events_;
  Config cfg_, last_valid_;
  Controller controller_;
  std::vector<std::string> logs_, scan_ips_;
  std::string local_ip_, interface_, message_{"Siap. Tekan H untuk bantuan."};
  bool running_{true}, scanning_{false};
  bool startup_start_cancelled_{false};
  bool startup_target_pending_{false};
  int mode_{0}, edit_index_{0}, scan_index_{0}, log_scroll_{0};
  std::vector<Field> edit_;
  std::thread scan_thread_;
  std::atomic<bool> scan_cancelled_{false};
  bool shutdown_done_{false};

  void event(EventType type, std::string text = {},
             std::vector<std::string> ips = {});
  void drain();
  void add(int y, int x, const std::string &value, int width = -1,
           int attr = 0);
  void box(int y, int x, int h, int w, const std::string &title = {});
  std::vector<std::string> wrapLogs(int width) const;
  void wordmark(int x, int width);
  void draw();
  void drawEdit(int rows, int cols);
  void drawScan(int rows, int cols);
  void drawHelp(int rows, int cols);
  void openEdit();
  bool saveEdit();
  void start();
  void scan(bool startup = false);
  void useScanTarget(const std::string &ip);
  void handle(int key);
  void shutdown();
};

} // namespace rov
