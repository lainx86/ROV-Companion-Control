#pragma once

#include <string>

namespace rov {

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

bool validate(const Config &cfg, std::string &error);
Config loadConfig();
bool saveConfig(const Config &cfg);

} // namespace rov
