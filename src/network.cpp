#include "network.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <regex>
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
std::string interfaceCidr(const std::string &iface) {
  if (iface.empty())
    return {};
  const auto out =
      commandOutput("ip -4 -o addr show dev " + iface + " 2>/dev/null");
  std::smatch match;
  return std::regex_search(out, match, std::regex("inet\\s+([0-9.]+/[0-9]+)"))
             ? match[1].str()
             : "";
}
bool tcpAlive(const std::string &ip, int port) {
  int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (socket_fd < 0)
    return false;
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

bool hostAlive(const std::string &ip) {
  const std::string ping = "ping -c 1 -W 1 " + ip + " >/dev/null 2>&1";
  if (std::system(ping.c_str()) == 0)
    return true;
  return tcpAlive(ip, 22) || tcpAlive(ip, 80) || tcpAlive(ip, 443);
}
} // namespace

std::pair<std::string, std::string> localIpv4() {
  const auto route = commandOutput("ip -4 route get 1.1.1.1 2>/dev/null");
  std::smatch match;
  std::string ip, iface;
  if (std::regex_search(route, match, std::regex("\\bsrc\\s+([0-9.]+)")))
    ip = match[1];
  if (std::regex_search(route, match, std::regex("\\bdev\\s+(\\S+)")))
    iface = match[1];
  return {ip, iface};
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

ScanResult scanNetwork(const std::atomic<bool> &cancelled) {
  if (cancelled)
    return {};
  const auto local = localIpv4();
  std::string cidr = interfaceCidr(local.second);
  if (cidr.empty() && !local.first.empty())
    cidr = local.first.substr(0, local.first.rfind('.')) + ".0/24";
  const auto hosts = cidrHosts(cidr);
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
        if (hostAlive(hosts[index])) {
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

} // namespace rov
