#ifndef FRAGMENTATION_GOVERNOR_COORDINATOR_TCP_TRANSPORT_HPP
#define FRAGMENTATION_GOVERNOR_COORDINATOR_TCP_TRANSPORT_HPP

#include <cstdint>
#include <optional>
#include <string>

#include "fragmentation_governor/coordinator/coordinator.hpp"
#include "fragmentation_governor/protocol/protocol.hpp"

namespace fragmentation_governor {

/// A blocking loopback TCP client that sends and receives framed messages.
class TcpClient {
 public:
  TcpClient();
  ~TcpClient();
  TcpClient(const TcpClient&) = delete;
  TcpClient& operator=(const TcpClient&) = delete;

  bool connect(std::uint16_t port);
  void close();
  bool send_frame(const Message& msg);
  std::optional<Message> recv_frame();     // blocks until a complete frame or error
  bool is_open() const { return sock_ != -1; }

 private:
  long long sock_ = -1;   // SOCKET is SOCKET (uintptr_t); store as 8-byte handle
  bool winsock_ = false;
};

/// A blocking loopback TCP server hosting a Coordinator, one thread per
/// connection.  The server is not thread-safe internally and serialises all
/// Coordinator access through a single mutex.
class TcpServer {
 public:
  explicit TcpServer(Coordinator& coord);
  ~TcpServer();
  TcpServer(const TcpServer&) = delete;
  TcpServer& operator=(const TcpServer&) = delete;

  bool start(std::uint16_t port);   // bind + listen
  void serve();                     // accept loop (blocks until stop())
  void stop();
  std::uint16_t port() const { return port_; }
  bool listening() const { return listening_; }

  static std::uint16_t find_free_port();

 private:
  struct Impl;
  Impl* impl_;
  Coordinator& coord_;
  std::uint16_t port_ = 0;
  bool listening_ = false;
  bool stopping_ = false;
};

/// Convenience: find a free TCP loopback port.
std::uint16_t find_free_tcp_port();

}  // namespace fragmentation_governor

#endif  // FRAGMENTATION_GOVERNOR_COORDINATOR_TCP_TRANSPORT_HPP
