#include "network.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <regex>
#include <sstream>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace rov {

namespace {
std::string commandOutput(const std::string &command) {
  std::string result;
  FILE *pipe = popen(command.c_str(), "r");
  if (!pipe)
    return result;
  char buffer[256];
  while (fgets(buffer, sizeof(buffer), pipe))
    result += buffer;
  pclose(pipe);
  return result;
}
struct EthernetAddress {
  std::string name;
  std::string ip;
  std::string cidr;
};

EthernetAddress ethernetAddress() {
  // LOWER_UP requires a live link, not just an administratively enabled NIC.
  std::istringstream links(commandOutput("ip -o link show up 2>/dev/null"));
  std::string line;
  std::smatch match;
  const std::regex link_pattern(
      "^[0-9]+:\\s+([A-Za-z0-9_.:-]+)(?:@[^: ]+)?:\\s+<([^>]+)>");
  while (std::getline(links, line)) {
    if (!std::regex_search(line, match, link_pattern))
      continue;
    const std::string name = match[1];
    if (match[2].str().find("LOWER_UP") == std::string::npos ||
        !isEthernetInterface(name))
      continue;
    const auto addresses = commandOutput("ip -4 -o addr show dev '" + name +
                                         "' scope global 2>/dev/null");
    if (std::regex_search(addresses, match,
                          std::regex("inet\\s+(([0-9.]+)/[0-9]+)")))
      return {name, match[2], match[1]};
  }
  return {};
}
bool tcpAlive(const std::string &ip, int port, const std::string &iface) {
  int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (socket_fd < 0)
    return false;
  if (setsockopt(socket_fd, SOL_SOCKET, SO_BINDTODEVICE, iface.c_str(),
                 iface.size() + 1) != 0) {
    close(socket_fd);
    return false;
  }
  timeval timeout{0, 500000};
  setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  inet_pton(AF_INET, ip.c_str(), &address.sin_addr);
  const bool alive = connect(socket_fd, reinterpret_cast<sockaddr *>(&address),
                             sizeof(address)) == 0;
  close(socket_fd);
  return alive;
}

bool hostAlive(const std::string &ip, const std::string &iface) {
  const std::string ping =
      "ping -I '" + iface + "' -c 1 -W 1 " + ip + " >/dev/null 2>&1";
  if (std::system(ping.c_str()) == 0)
    return true;
  return tcpAlive(ip, 22, iface) || tcpAlive(ip, 80, iface) ||
         tcpAlive(ip, 443, iface);
}
} // namespace

bool isEthernetInterface(const std::string &name,
                         const std::string &sysfs_root) {
  if (!std::regex_match(name, std::regex("[A-Za-z0-9_.:-]+")) || name == "." ||
      name == "..")
    return false;
  const auto path = std::filesystem::path(sysfs_root) / name;
  std::error_code error;
  if (!std::filesystem::exists(path / "device", error) || error)
    return false;
  for (const auto *marker : {"wireless", "phy80211"}) {
    if (std::filesystem::exists(path / marker, error) || error)
      return false;
  }
  int type = 0;
  std::ifstream(path / "type") >> type;
  return type == 1; // ARPHRD_ETHER, with wireless and virtual devices excluded.
}

std::pair<std::string, std::string> localIpv4() {
  const auto local = ethernetAddress();
  return {local.ip, local.name};
}
std::vector<std::string> cidrHosts(const std::string &cidr) {
  const auto slash = cidr.find('/');
  if (slash == std::string::npos)
    return {};
  int prefix = 0;
  try {
    prefix = std::stoi(cidr.substr(slash + 1));
  } catch (...) {
    return {};
  }
  if (prefix < 16 || prefix > 30)
    return {};
  in_addr address{};
  if (inet_pton(AF_INET, cidr.substr(0, slash).c_str(), &address) != 1)
    return {};
  const uint32_t raw = ntohl(address.s_addr);
  const uint32_t mask = 0xffffffffu << (32 - prefix);
  const uint32_t first = (raw & mask) + 1;
  const uint32_t last = (raw | ~mask) - 1;
  if (last < first || last - first + 1 > 1022)
    return {};
  std::vector<std::string> hosts;
  for (uint32_t value = first; value <= last; ++value) {
    in_addr host{htonl(value)};
    char text[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &host, text, sizeof(text));
    hosts.emplace_back(text);
  }
  return hosts;
}

TargetProbeResult probeTargetOnEthernet(const std::string &ip,
                                        const std::atomic<bool> &cancelled) {
  if (cancelled)
    return {};
  const auto local = ethernetAddress();
  if (local.name.empty())
    return {
        false,
        "Ethernet aktif dengan IPv4 tidak ditemukan (Wi-Fi tidak digunakan)"};
  in_addr address{};
  if (inet_pton(AF_INET, ip.c_str(), &address) != 1)
    return {false, "Target IP tidak valid"};
  return {hostAlive(ip, local.name), {}};
}

ScanResult scanNetwork(const std::atomic<bool> &cancelled) {
  if (cancelled)
    return {};
  const auto local = ethernetAddress();
  if (local.name.empty())
    return {
        {},
        "Ethernet aktif dengan IPv4 tidak ditemukan (Wi-Fi tidak digunakan)"};
  const auto hosts = cidrHosts(local.cidr);
  if (hosts.empty())
    return {{}, "Tidak bisa menentukan subnet Ethernet"};

  std::atomic<std::size_t> next{0};
  ScanResult result;
  std::mutex alive_lock;
  std::vector<std::thread> workers;
  for (int n = 0; n < 32; ++n) {
    workers.emplace_back([&] {
      while (!cancelled) {
        const auto index = next++;
        if (index >= hosts.size())
          break;
        if (hosts[index] != local.ip && hostAlive(hosts[index], local.name)) {
          std::lock_guard<std::mutex> lock(alive_lock);
          result.ips.push_back(hosts[index]);
        }
      }
    });
  }
  for (auto &worker : workers)
    worker.join();
  std::sort(result.ips.begin(), result.ips.end());
  return result;
}

std::string chooseScanTarget(const std::vector<std::string> &ips,
                             const std::string &preferred_ip) {
  if (std::find(ips.begin(), ips.end(), preferred_ip) != ips.end())
    return preferred_ip;
  return ips.size() == 1 ? ips.front() : std::string{};
}

} // namespace rov
