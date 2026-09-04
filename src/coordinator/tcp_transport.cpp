#include "fragmentation_governor/coordinator/tcp_transport.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

namespace fragmentation_governor {

namespace {
struct WinsockInit {
  WinsockInit() { WSADATA d; WSAStartup(MAKEWORD(2, 2), &d); }
  ~WinsockInit() { WSACleanup(); }
};
void ensure_winsock() { static WinsockInit init; }

bool send_all(SOCKET s, const std::uint8_t* p, size_t n) {
  size_t sent = 0;
  while (sent < n) {
    int r = ::send(s, reinterpret_cast<const char*>(p + sent),
                   static_cast<int>(n - sent), 0);
    if (r == SOCKET_ERROR) return false;
    sent += static_cast<size_t>(r);
  }
  return true;
}
}  // namespace

// --------------------------------------------------------------------------
TcpClient::TcpClient() { ensure_winsock(); }
TcpClient::~TcpClient() { close(); }

bool TcpClient::connect(std::uint16_t port) {
  close();
  SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return false;
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(port);
  if (::connect(s, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
    ::closesocket(s);
    return false;
  }
  sock_ = static_cast<long long>(s);
  return true;
}

void TcpClient::close() {
  if (sock_ != -1) {
    ::closesocket(static_cast<SOCKET>(sock_));
    sock_ = -1;
  }
}

bool TcpClient::send_frame(const Message& msg) {
  if (sock_ == -1) return false;
  std::vector<std::uint8_t> frame = frame_encode(msg);
  return send_all(static_cast<SOCKET>(sock_), frame.data(), frame.size());
}

std::optional<Message> TcpClient::recv_frame() {
  if (sock_ == -1) return std::nullopt;
  FrameDecoder dec;
  std::uint8_t buf[4096];
  while (true) {
    int n = ::recv(static_cast<SOCKET>(sock_), reinterpret_cast<char*>(buf),
                   static_cast<int>(sizeof(buf)), 0);
    if (n == 0) return std::nullopt;
    if (n < 0) {
      int e = WSAGetLastError();
      if (e == WSAEINTR) continue;
      return std::nullopt;
    }
    auto m = dec.feed(buf, static_cast<size_t>(n));
    if (m.has_value()) return m;
  }
}

// --------------------------------------------------------------------------
std::uint16_t find_free_tcp_port() {
  ensure_winsock();
  SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return 0;
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(0);
  if (::bind(s, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
    ::closesocket(s);
    return 0;
  }
  int len = sizeof(addr);
  getsockname(s, reinterpret_cast<sockaddr*>(&addr), &len);
  std::uint16_t port = ntohs(addr.sin_port);
  ::closesocket(s);
  return port;
}

// --------------------------------------------------------------------------
struct TcpServer::Impl {
  std::mutex mu;
  std::mutex loop_mu;
  std::atomic<bool> running{false};
  SOCKET listen_sock = INVALID_SOCKET;
  std::vector<std::thread> threads;
  std::mutex sockets_mu;
  std::vector<SOCKET> client_socks;
};

TcpServer::TcpServer(Coordinator& coord) : impl_(new Impl), coord_(coord) {
  ensure_winsock();
}
TcpServer::~TcpServer() { stop(); delete impl_; }

bool TcpServer::start(std::uint16_t port) {
  if (port == 0) port = find_free_tcp_port();
  SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return false;
  BOOL reuse = TRUE;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(port);
  if (::bind(s, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
    ::closesocket(s);
    return false;
  }
  if (::listen(s, 16) == SOCKET_ERROR) { ::closesocket(s); return false; }
  impl_->listen_sock = s;
  port_ = (port != 0) ? port : [&]() { int len = sizeof(addr); getsockname(s, reinterpret_cast<sockaddr*>(&addr), &len); return ntohs(addr.sin_port); }();
  listening_ = true;
  impl_->running.store(true);
  return true;
}

void TcpServer::serve() {
  while (impl_->running.load()) {
    SOCKET cs = ::accept(impl_->listen_sock, nullptr, nullptr);
    if (cs == INVALID_SOCKET) {
      if (!impl_->running.load()) break;
      continue;
    }
    {
      std::lock_guard<std::mutex> g(impl_->sockets_mu);
      impl_->client_socks.push_back(cs);
    }
    impl_->threads.emplace_back([this, cs]() {
      FrameDecoder dec;
      std::uint8_t buf[4096];
      std::uint64_t conn_worker = 0;
      while (true) {
        int n = ::recv(cs, reinterpret_cast<char*>(buf), static_cast<int>(sizeof(buf)), 0);
        if (n <= 0) break;
        auto msg = dec.feed(buf, static_cast<size_t>(n));
        if (!msg.has_value()) continue;
        if (msg->type == MsgType::REGISTER) conn_worker = msg->worker_id;
        std::vector<Message> replies;
        {
          std::lock_guard<std::mutex> g(impl_->mu);
          replies = coord_.handle(*msg);
        }
        for (const auto& r : replies) {
          std::vector<std::uint8_t> frame = frame_encode(r);
          send_all(cs, frame.data(), frame.size());
        }
      }
      ::closesocket(cs);
      if (conn_worker != 0) {
        std::lock_guard<std::mutex> g(impl_->mu);
        coord_.mark_worker_dead(WorkerId(conn_worker));
      }
    });
  }
}

void TcpServer::stop() {
  impl_->running.store(false);
  if (impl_->listen_sock != INVALID_SOCKET) {
    ::closesocket(impl_->listen_sock);
    impl_->listen_sock = INVALID_SOCKET;
  }
  {
    std::lock_guard<std::mutex> g(impl_->sockets_mu);
    for (SOCKET s : impl_->client_socks) ::closesocket(s);
    impl_->client_socks.clear();
  }
  for (auto& th : impl_->threads) if (th.joinable()) th.join();
  impl_->threads.clear();
  listening_ = false;
}

}  // namespace fragmentation_governor
