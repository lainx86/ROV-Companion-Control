#include "config.hpp"
#include "controller.hpp"
#include "network.hpp"
#include "process.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <mutex>

using namespace rov;

int main() {
  Config config;
  std::string error;
  assert(validate(config, error));

  config.cam0_port = 0;
  assert(!validate(config, error));
  config.cam0_port = 5070;
  config.ip = "invalid";
  assert(!validate(config, error));
  config = Config{};
  config.mav_device.clear();
  assert(!validate(config, error));
  config = Config{};
  config.baudrate = 0;
  assert(!validate(config, error));

  const auto hosts = cidrHosts("192.168.1.0/30");
  assert((hosts == std::vector<std::string>{"192.168.1.1", "192.168.1.2"}));
  assert(cidrHosts("192.168.0.0/15").empty());
  assert(cidrHosts("invalid/24").empty());
  assert(cidrHosts("192.168.1.1").empty());
  std::atomic<bool> cancelled{true};
  const auto scan = scanNetwork(cancelled);
  assert(scan.ips.empty() && scan.error.empty());
  assert(routineOutput("Setting pipeline to PLAYING ..."));
  assert(!routineOutput("ERROR: camera unavailable"));

  // Exercise the extracted process module without cameras, MAVProxy, or a TTY.
  std::mutex log_lock;
  std::condition_variable output_ready;
  std::vector<std::string> logs;
  auto log = [&](const std::string &line) {
    std::lock_guard<std::mutex> guard(log_lock);
    logs.push_back(line);
    output_ready.notify_all();
  };
  ManagedProcess process(
      "TEST",
      {"/bin/sh", "-c",
       "printf 'Setting pipeline to PLAYING ...\n'; "
       "printf 'ERROR: test diagnostic\n' >&2; exec sleep 10"},
      log, [] {});
  assert(process.name() == "TEST");
  assert(process.start());
  bool received = false;
  {
    std::unique_lock<std::mutex> guard(log_lock);
    received = output_ready.wait_for(guard, std::chrono::seconds(3), [&] {
      return std::find(logs.begin(), logs.end(),
                       "[TEST] ERROR: test diagnostic") != logs.end();
    });
  }
  const bool was_running = process.running();
  process.stop();
  assert(received && was_running);
  assert(!process.running());
  assert(std::none_of(logs.begin(), logs.end(), [](const auto &line) {
    return line.find("Setting pipeline") != std::string::npos;
  }));

  Controller controller(log, [] {});
  const auto states = controller.states();
  assert(states.size() == 3);
  assert(!states.at("CAM0") && !states.at("CAM1") && !states.at("MAVProxy"));
  controller.stop();
  return 0;
}
