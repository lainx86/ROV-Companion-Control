#include "config.hpp"

#include <arpa/inet.h>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <regex>

namespace rov {

namespace {
std::string homeConfigPath() {
  const char *home = std::getenv("HOME");
  return std::string(home ? home : ".") + "/.rov_control.json";
}
bool validPort(int value) { return value >= 1 && value <= 65535; }
std::string jsonEscape(const std::string &value) {
  std::string out;
  for (char c : value) {
    if (c == '\\' || c == '"')
      out.push_back('\\');
    out.push_back(c);
  }
  return out;
}
std::string readFile(const std::string &path) {
  std::ifstream file(path);
  return std::string((std::istreambuf_iterator<char>(file)),
                     std::istreambuf_iterator<char>());
}

std::string jsonValue(const std::string &source, const std::string &key) {
  const std::regex pattern(
      "\\\"" + key + "\\\"\\s*:\\s*(\\\"([^\\\"]*)\\\"|true|false|-?[0-9]+)");
  std::smatch match;
  if (!std::regex_search(source, match, pattern))
    return {};
  return match[2].matched ? match[2].str() : match[1].str();
}
} // namespace

bool validate(const Config &cfg, std::string &error) {
  in_addr address{};
  if (inet_pton(AF_INET, cfg.ip.c_str(), &address) != 1) {
    error = "Target IP tidak valid";
    return false;
  }
  if (cfg.cam0_device.empty() || cfg.cam1_device.empty() ||
      cfg.mav_device.empty() || cfg.mavproxy_path.empty()) {
    error = "Device kamera, MAV, dan path MAVProxy tidak boleh kosong";
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
bool saveConfig(const Config &cfg) {
  std::ofstream file(homeConfigPath());
  if (!file)
    return false;
  file << "{\n"
       << "  \"ip\": \"" << jsonEscape(cfg.ip) << "\",\n"
       << "  \"cam0_device\": \"" << jsonEscape(cfg.cam0_device) << "\",\n"
       << "  \"cam0_port\": " << cfg.cam0_port << ",\n"
       << "  \"cam1_device\": \"" << jsonEscape(cfg.cam1_device) << "\",\n"
       << "  \"cam1_port\": " << cfg.cam1_port << ",\n"
       << "  \"mav_port0\": " << cfg.mav_port0 << ",\n"
       << "  \"mav_port1\": " << cfg.mav_port1 << ",\n"
       << "  \"mavproxy_path\": \"" << jsonEscape(cfg.mavproxy_path)
       << "\",\n"
       << "  \"mav_device\": \"" << jsonEscape(cfg.mav_device) << "\",\n"
       << "  \"baudrate\": " << cfg.baudrate << "\n}\n";
  return static_cast<bool>(file);
}
Config loadConfig() {
  Config cfg;
  const std::string source = readFile(homeConfigPath());
  if (source.empty())
    return cfg;
  auto stringField = [&](const char *key, std::string &target) {
    const auto value = jsonValue(source, key);
    if (!value.empty())
      target = value;
  };
  auto intField = [&](const char *key, int &target) {
    const auto value = jsonValue(source, key);
    try {
      if (!value.empty())
        target = std::stoi(value);
    } catch (...) {
    }
  };
  stringField("ip", cfg.ip);
  stringField("cam0_device", cfg.cam0_device);
  stringField("cam1_device", cfg.cam1_device);
  stringField("mavproxy_path", cfg.mavproxy_path);
  stringField("mav_device", cfg.mav_device);
  intField("cam0_port", cfg.cam0_port);
  intField("cam1_port", cfg.cam1_port);
  intField("mav_port0", cfg.mav_port0);
  intField("mav_port1", cfg.mav_port1);
  intField("baudrate", cfg.baudrate);
  // Legacy "autostart" is ignored: startup now always scans, then starts streams.
  std::string error;
  return validate(cfg, error) ? cfg : Config{};
}

} // namespace rov
