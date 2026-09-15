#include "config.hpp"
#include "controller.hpp"
#include "network.hpp"
#include "process.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>

using namespace rov;

int main() {
  // Synthetic sysfs entries distinguish wired NICs from renamed Wi-Fi and
  // virtual Ethernet devices without depending on the test machine's hardware.
  char sysfs_template[] = "/tmp/rov-network-test-XXXXXX";
  const char *sysfs_dir = mkdtemp(sysfs_template);
  assert(sysfs_dir);
  const auto sysfs = std::filesystem::path(sysfs_dir);
  for (const auto *name :
       {"eth0", "enp3s0", "usb0", "eth_wifi", "wlan0", "docker0", "lo"}) {
    const auto path = sysfs / name;
    std::filesystem::create_directory(path);
    std::ofstream(path / "type") << (std::string(name) == "lo" ? 772 : 1);
    if (std::string(name) != "docker0" && std::string(name) != "lo")
      std::filesystem::create_directory(path / "device");
  }
  std::filesystem::create_directory(sysfs / "eth_wifi" / "wireless");
  std::filesystem::create_directory(sysfs / "wlan0" / "phy80211");
  for (const auto *name : {"eth0", "enp3s0", "usb0"})
    assert(isEthernetInterface(name, sysfs.string()));
  for (const auto *name : {"eth_wifi", "wlan0", "docker0", "lo", "missing",
                           "../eth0", ".", "..", "eth0;exit"})
    assert(!isEthernetInterface(name, sysfs.string()));
  std::filesystem::remove_all(sysfs);

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
  config.mavproxy_path.clear();
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
  const auto probe = probeTargetOnEthernet("192.168.1.198", cancelled);
  assert(!probe.reachable && probe.error.empty());
  assert(chooseScanTarget({}, "192.168.1.198").empty());
  assert(chooseScanTarget({"192.168.1.20"}, "192.168.1.198") == "192.168.1.20");
  assert(chooseScanTarget({"192.168.1.20", "192.168.1.198"}, "192.168.1.198") ==
         "192.168.1.198");
  assert(chooseScanTarget({"192.168.1.20", "192.168.1.30"}, "192.168.1.198")
             .empty());
  assert(routineOutput("Setting pipeline to PLAYING ..."));
  assert(!routineOutput("ERROR: camera unavailable"));

  // Process tests require executable children for the target architecture.
#ifndef ROV_CROSS_COMPILED
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
#endif
  return 0;
}
