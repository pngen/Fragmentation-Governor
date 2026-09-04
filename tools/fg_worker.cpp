// Fragmentation Governor reference worker process.
// Usage: fg_worker <port> <worker_id> <boot_id>
// Worker A (id=1) publishes a fragmented (immovable) resource;
// Worker B (id=2) publishes a resource with reclaimable allocations.
// Copyright 2026 Summon Software Labs.  Apache License 2.0.
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <chrono>

#include "fragmentation_governor/fragmentation_governor.hpp"

using namespace fragmentation_governor;

int main(int argc, char** argv) {
  (void)argc;
  std::uint16_t port = static_cast<std::uint16_t>(std::atoi(argv[1]));
  std::uint64_t wid = static_cast<std::uint64_t>(std::stoull(argv[2]));
  std::uint64_t boot = static_cast<std::uint64_t>(std::stoull(argv[3]));

  TcpClient c;
  if (!c.connect(port)) { fprintf(stderr, "worker %llu cannot connect\n", (unsigned long long)wid); return 1; }

  const size_t MIB = 1024 * 1024;
  const size_t U = 256 * MIB;

  Message reg; reg.type = MsgType::REGISTER; reg.worker_id = wid; reg.worker_boot = boot; reg.epoch = 1;
  c.send_frame(reg);
  c.recv_frame();  // ack

  Resource res;
  res.id = ResourceId(wid); res.generation = ResourceGeneration(1);
  res.pool = ResourcePoolId(1); res.device = DeviceId(wid); res.node = NodeId(1);
  res.domain = Domain::CUDA_DEVICE_MEMORY; res.capacity = Bytes(2048 * MIB);
  res.provenance = Provenance::DERIVED;
  Message pr; pr.type = MsgType::PUBLISH_RESOURCE; pr.worker_id = wid; pr.worker_boot = boot; pr.epoch = 1; pr.resource = res;
  c.send_frame(pr); c.recv_frame();

  Message lay; lay.type = MsgType::PUBLISH_LAYOUT; lay.worker_id = wid; lay.worker_boot = boot; lay.epoch = 1;
  lay.resource.id = res.id; lay.resource_gen = 1;
  if (wid == 1) {
    // Fragmented immutable resource: 4 immovable 256MiB + 1 movable 64MiB.
    Allocation a{AllocationId(101), AllocationGeneration(1), AllocationOwnerId(100+wid), Bytes(0), Bytes(U), Movability::IMMOVABLE, std::nullopt, false, std::nullopt, Provenance::DERIVED};
    Allocation b{AllocationId(102), AllocationGeneration(1), AllocationOwnerId(100+wid), Bytes(2*U), Bytes(U), Movability::IMMOVABLE, std::nullopt, false, std::nullopt, Provenance::DERIVED};
    Allocation c2{AllocationId(103), AllocationGeneration(1), AllocationOwnerId(100+wid), Bytes(4*U), Bytes(U), Movability::IMMOVABLE, std::nullopt, false, std::nullopt, Provenance::DERIVED};
    Allocation d{AllocationId(104), AllocationGeneration(1), AllocationOwnerId(100+wid), Bytes(6*U), Bytes(U), Movability::IMMOVABLE, std::nullopt, false, std::nullopt, Provenance::DERIVED};
    Allocation e{AllocationId(105), AllocationGeneration(1), AllocationOwnerId(100+wid), Bytes(3*U), Bytes(U/4), Movability::MOVABLE_AFTER_QUIESCE, std::nullopt, false, std::nullopt, Provenance::DERIVED};
    lay.allocations = {a, b, c2, d, e};
  } else {
    // Reclaimable resource: 4 releaseable 256MiB blocks.
    for (int i = 0; i < 4; ++i) {
      Allocation a{AllocationId(201+i), AllocationGeneration(1), AllocationOwnerId(200+wid), Bytes((size_t)(2*i)*U), Bytes(U), Movability::RELEASEABLE, std::nullopt, false, std::nullopt, Provenance::DERIVED};
      lay.allocations.push_back(a);
    }
  }
  c.send_frame(lay); c.recv_frame();

  printf("WORKER %llu READY boot=%llu\n", (unsigned long long)wid, (unsigned long long)boot);
  fflush(stdout);

  // Stay alive (this is a real, independent OS process).
  for (;;) { std::this_thread::sleep_for(std::chrono::seconds(1)); }
  return 0;
}