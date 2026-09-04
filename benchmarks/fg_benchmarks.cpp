// Fragmentation Governor benchmarks.
// Copyright 2026 Summon Software Labs.  Apache License 2.0.
#include <chrono>
#include <cstdio>
#include <vector>
#include "fragmentation_governor/fragmentation_governor.hpp"
using namespace fragmentation_governor;
using clk = std::chrono::steady_clock;
static double ms(clk::time_point a, clk::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); }
static void bench(int n) {
  const size_t MIB = 1024 * 1024; const size_t U = 8 * MIB; const size_t cap = (size_t)n * U + 1024 * MIB;
  printf("\n==== scale n=%d ====\n", n);
  ResourceLayout lay{Bytes(cap)}; std::string err;
  auto t0 = clk::now();
  for (int i = 0; i < n; ++i) { Movability mv = (i % 3 == 0) ? Movability::IMMOVABLE : Movability::MOVABLE_AFTER_CHECKPOINT; lay.insert_allocation(Allocation{AllocationId(i+1), AllocationGeneration(1), AllocationOwnerId(i+1), Bytes((size_t)i*U), Bytes(U), mv, std::nullopt, false, std::nullopt, Provenance::DERIVED}, err); }
  auto t1 = clk::now(); printf("  layout ingest  : %8.3f ms  (%d inserts)\n", ms(t0, t1), n);
  WorkloadDemand d; d.accelerator_count = Count(1); d.per_device_memory = Bytes(64*MIB); d.aggregate_memory = Bytes(64*MIB); d.contiguous_memory = Bytes(64*MIB);
  auto t2 = clk::now(); FragmentationMetrics m = compute_metrics(lay, &d); auto t3 = clk::now();
  printf("  metrics        : %8.3f ms  largest=%zuMiB free=%zuMiB\n", ms(t2, t3), (size_t)m.largest_free_block.value()/MIB, (size_t)m.free_capacity.value()/MIB);
  auto t4 = clk::now(); Bytes lb = lay.largest_free_block(); auto t5 = clk::now();
  printf("  largest-block  : %8.3f ms\n", ms(t4, t5));
  FragmentationSnapshot s; s.generation = FragmentationSnapshotGeneration(1); s.domain = Domain::CUDA_DEVICE_MEMORY; s.provenance = Provenance::DERIVED; s.epoch = CoordinatorEpoch(1); s.policy_generation = PolicyGeneration(1);
  s.generations.resource_generation = ResourceGeneration(1); s.generations.allocation_generation = AllocationGeneration(1); s.generations.reservation_generation = ReservationGeneration(1); s.generations.topology_generation = TopologyGeneration(1); s.generations.capacity_generation = CapacityModelGeneration(1);
  Resource r; r.id = ResourceId(1); r.device = DeviceId(1); r.domain = Domain::CUDA_DEVICE_MEMORY; r.capacity = Bytes(cap); r.provenance = Provenance::DERIVED;
  DeviceState ds; ds.resource = r; ds.layout = lay; s.devices.push_back(ds);
  auto t6 = clk::now(); FitResult fit = analyze_fit(s, d); auto t7 = clk::now();
  printf("  workload-fit   : %8.3f ms  outcome=%s\n", ms(t6, t7), to_string(fit.outcome).data());
  Policy pol; pol.generation = PolicyGeneration(1);
  auto t8 = clk::now(); auto plan = plan_remediation(s, d, pol, default_weights(), FragmentationPlanId(1), FragmentationPlanGeneration(1)); auto t9 = clk::now();
  printf("  plan-rank      : %8.3f ms  plan=%d\n", ms(t8, t9), (int)plan.has_value());
  PersistedState st; st.resource_generation = ResourceGeneration(1); st.allocation_generation = AllocationGeneration(1); st.reservation_generation = ReservationGeneration(1); st.policy_generation = PolicyGeneration(1); st.epoch = CoordinatorEpoch(1); st.next_plan_id = n + 1;
  std::vector<std::uint8_t> bytes;
  auto t10 = clk::now(); CodecResult enc = encode_state(st, bytes); auto t11 = clk::now();
  printf("  persistence   : %8.3f ms  (%zu bytes, ok=%d)\n", ms(t10, t11), bytes.size(), enc.ok);
  fragmentation_governor::Message laymsg; laymsg.type = MsgType::PUBLISH_LAYOUT; laymsg.worker_id = 1; laymsg.worker_boot = 1; laymsg.epoch = 1; laymsg.resource.id = ResourceId(1); laymsg.resource_gen = 1;
  for (int i = 0; i < n; ++i) laymsg.allocations.push_back(Allocation{AllocationId(i+1), AllocationGeneration(1), AllocationOwnerId(i+1), Bytes((size_t)i*U), Bytes(U), Movability::MOVABLE_AFTER_CHECKPOINT, std::nullopt, false, std::nullopt, Provenance::DERIVED});
  auto t12 = clk::now(); auto f = frame_encode(laymsg); auto fdec = frame_decode(f.data(), f.size()); auto t13 = clk::now();
  printf("  protocol      : %8.3f ms  encode+decode(%d)\n", ms(t12, t13), (int)fdec->allocations.size());
}
int main() { bench(1000); bench(10000); bench(100000); printf("\nBENCHMARKS COMPLETE\n"); return 0; }