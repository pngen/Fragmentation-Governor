// Fragmentation Governor reference coordinator process.
// Usage: fg_coordinator <port> <epoch> [state_file]
// Copyright 2026 Summon Software Labs.  Apache License 2.0.
#include <cstdio>
#include <cstdlib>
#include <filesystem>

#include "fragmentation_governor/fragmentation_governor.hpp"

using namespace fragmentation_governor;

int main(int argc, char** argv) {
  std::uint16_t port = (argc > 1) ? static_cast<std::uint16_t>(std::atoi(argv[1])) : 0;
  std::uint64_t epoch = (argc > 2) ? static_cast<std::uint64_t>(std::stoull(argv[2])) : 1;
  std::string state_file = (argc > 3) ? argv[3] : std::string();

  Coordinator coord{CoordinatorEpoch(epoch)};
  if (!state_file.empty() && std::filesystem::exists(state_file)) {
    PersistedState st;
    CodecResult r = load_state(state_file, st);
    if (r.ok) { coord.recover(st); printf("coordinator recovered from %s\n", state_file.c_str()); }
    else { printf("coordinator load failed: %s\n", r.error.c_str()); }
  }

  TcpServer server(coord);
  if (!server.start(port)) { fprintf(stderr, "coordinator could not bind\n"); return 1; }
  printf("FG_COORDINATOR_PORT=%u EPOCH=%llu\n", server.port(), (unsigned long long)epoch);
  fflush(stdout);
  server.serve();  // blocks until process is terminated
  return 0;
}