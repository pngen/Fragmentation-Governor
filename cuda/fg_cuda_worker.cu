// Fragmentation Governor CUDA Scenario F - real CUDA worker OS process.
//
// A real, independent OS process.  Discovers the RTX 5090 / sm_120, performs
// bounded real CUDA allocations, runs a real kernel with CPU parity, and
// publishes current real/derived allocation-layout evidence to the coordinator
// over framed TCP.  The real device allocations are REAL; the governed logical
// layout is DERIVED because CUDA does not expose driver free-list offsets.
//
// Copyright 2026 Summon Software Labs.  Apache License 2.0.
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <chrono>

#include <cuda_runtime.h>

#include "fragmentation_governor/fragmentation_governor.hpp"

using namespace fragmentation_governor;

#define CUDA_CHECK(x) do { cudaError_t e = (x); if (e != cudaSuccess) { fprintf(stderr, "CUDA error %s at %s:%d\n", cudaGetErrorString(e), __FILE__, __LINE__); return 1; } } while (0)

__global__ void add_one_kernel(float* out, const float* in, int n) {
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i < n) out[i] = in[i] + 1.0f;
}

static const size_t MIB = 1024 * 1024;

// Do a real kernel run on a freshly written 16MiB buffer and return parity.
static int run_kernel_and_parity() {
  const size_t bytes = 16 * MIB;
  float* din = nullptr; float* dout = nullptr;
  if (cudaMalloc(&din, bytes) != cudaSuccess) return 0;
  if (cudaMalloc(&dout, bytes) != cudaSuccess) { cudaFree(din); return 0; }
  const int n = (int)(bytes / sizeof(float));
  float* hi = (float*)malloc(bytes); float* ho = (float*)malloc(bytes);
  for (int i = 0; i < n; ++i) hi[i] = (float)(i % 1000);
  cudaMemcpy(din, hi, bytes, cudaMemcpyHostToDevice);
  add_one_kernel<<<(n + 255) / 256, 256>>>(dout, din, n);
  cudaDeviceSynchronize();
  cudaMemcpy(ho, dout, bytes, cudaMemcpyDeviceToHost);
  int parity = 1;
  for (int i = 0; i < n; ++i) if (ho[i] != hi[i] + 1.0f) { parity = 0; break; }
  free(hi); free(ho);
  cudaFree(din); cudaFree(dout);
  return parity;
}

int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: fg_cuda_worker <port> <boot_id>\n"); return 2; }
  std::uint16_t port = (std::uint16_t)std::atoi(argv[1]);
  std::uint64_t boot = (std::uint64_t)std::stoull(argv[2]);
  const std::uint64_t wid = 1;

  if (cudaSetDevice(0) != cudaSuccess) { fprintf(stderr, "no CUDA device\n"); return 3; }
  cudaDeviceProp prop{}; CUDA_CHECK(cudaGetDeviceProperties(&prop, 0));
  if (prop.major < 12) { fprintf(stderr, "requires sm_120\n"); return 3; }
  size_t base = 0, total = 0;
  CUDA_CHECK(cudaMemGetInfo(&base, &total));
  printf("CUDA_WORKER dev=%s base=%zuMiB\n", prop.name, base / MIB); fflush(stdout);

  // Real, bounded CUDA allocations (REAL evidence).
  const size_t RB = 256 * MIB;
  void* bufs[4] = {nullptr, nullptr, nullptr, nullptr};
  for (int i = 0; i < 4; ++i) { if (cudaMalloc(&bufs[i], RB) != cudaSuccess) { fprintf(stderr, "cudaMalloc failed\n"); return 1; } }

  // Real kernel + CPU parity before failure.
  int parity = run_kernel_and_parity();
  printf("CUDA_WORKER parity=%d\n", parity); fflush(stdout);
  if (!parity) { fprintf(stderr, "kernel parity failed\n"); return 1; }

  // Publish evidence to the coordinator.
  TcpClient c;
  if (!c.connect(port)) { fprintf(stderr, "worker connect failed\n"); return 1; }
  Message reg; reg.type = MsgType::REGISTER; reg.worker_id = wid; reg.worker_boot = boot; reg.epoch = 1;
  c.send_frame(reg); c.recv_frame();

  Resource res;
  res.id = ResourceId(wid); res.generation = ResourceGeneration(boot);
  res.pool = ResourcePoolId(1); res.device = DeviceId(wid); res.node = NodeId(1);
  res.domain = Domain::CUDA_DEVICE_MEMORY; res.capacity = Bytes(2048 * MIB);
  res.provenance = Provenance::DERIVED;   // logical pool derived from real allocations
  Message pr; pr.type = MsgType::PUBLISH_RESOURCE; pr.worker_id = wid; pr.worker_boot = boot; pr.epoch = 1; pr.resource = res;
  c.send_frame(pr); c.recv_frame();

  // DERIVED logical layout reflecting the four real 256MiB allocations.  The
  // last is RELEASEABLE so a fresh remediation can be planned.
  Allocation a0{AllocationId(1), AllocationGeneration(boot), AllocationOwnerId(100), Bytes(0), Bytes(RB), Movability::IMMOVABLE, std::nullopt, false, std::nullopt, Provenance::DERIVED};
  Allocation a1{AllocationId(2), AllocationGeneration(boot), AllocationOwnerId(101), Bytes(2*RB), Bytes(RB), Movability::IMMOVABLE, std::nullopt, false, std::nullopt, Provenance::DERIVED};
  Allocation a2{AllocationId(3), AllocationGeneration(boot), AllocationOwnerId(102), Bytes(4*RB), Bytes(RB), Movability::IMMOVABLE, std::nullopt, false, std::nullopt, Provenance::DERIVED};
  Allocation a3{AllocationId(4), AllocationGeneration(boot), AllocationOwnerId(103), Bytes(6*RB), Bytes(RB), Movability::RELEASEABLE, std::nullopt, false, std::nullopt, Provenance::DERIVED};
  Message lay; lay.type = MsgType::PUBLISH_LAYOUT; lay.worker_id = wid; lay.worker_boot = boot; lay.epoch = 1;
  lay.resource.id = res.id; lay.resource_gen = boot; lay.allocations = {a0, a1, a2, a3};
  c.send_frame(lay); c.recv_frame();

  printf("CUDA_WORKER READY boot=%llu\n", (unsigned long long)boot); fflush(stdout);

  // Stay alive as an independent process until killed.
  for (;;) std::this_thread::sleep_for(std::chrono::seconds(1));
  return 0;
}