// Independent downstream consumer of the installed Fragmentation Governor.
// Copyright 2026 Summon Software Labs.  Apache License 2.0.
#include <cstdio>
#include "fragmentation_governor/fragmentation_governor.hpp"
using namespace fragmentation_governor;
int main() {
  const size_t MIB = 1024 * 1024;
  Resource r; r.id = ResourceId(1); r.generation = ResourceGeneration(1); r.device = DeviceId(1);
  r.domain = Domain::CUDA_DEVICE_MEMORY; r.capacity = Bytes(2048 * MIB); r.provenance = Provenance::DERIVED;
  ResourceLayout lay{r.capacity};
  std::string err;
  for (int i = 0; i < 4; ++i) lay.insert_allocation(Allocation{AllocationId(1+i), AllocationGeneration(1), AllocationOwnerId(10+i), Bytes((size_t)i*512*MIB), Bytes(256*MIB), Movability::IMMOVABLE, std::nullopt, false, std::nullopt, Provenance::DERIVED}, err);
  WorkloadDemand d; d.id = WorkloadDemandId(1); d.generation = WorkloadDemandGeneration(1); d.accelerator_count = Count(1);
  d.per_device_memory = Bytes(512*MIB); d.aggregate_memory = Bytes(512*MIB); d.contiguous_memory = Bytes(512*MIB);
  FragmentationSnapshot s; s.generation = FragmentationSnapshotGeneration(1); s.domain = Domain::CUDA_DEVICE_MEMORY;
  s.provenance = Provenance::DERIVED; s.epoch = CoordinatorEpoch(1); s.policy_generation = PolicyGeneration(1);
  s.generations.resource_generation = ResourceGeneration(1); s.generations.allocation_generation = AllocationGeneration(1); s.generations.reservation_generation = ReservationGeneration(1); s.generations.topology_generation = TopologyGeneration(1); s.generations.capacity_generation = CapacityModelGeneration(1);
  DeviceState ds; ds.resource = r; ds.layout = lay; s.devices.push_back(ds);
  FragmentationMetrics m = compute_metrics(lay, &d);
  FitResult fit = analyze_fit(s, d);
  printf("downstream: free=%zuMiB largest=%zuMiB fit=%s\n", (size_t)m.free_capacity.value()/MIB, (size_t)m.largest_free_block.value()/MIB, to_string(fit.outcome).data());
  bool deterministic = fit.outcome == FitOutcome::NO_FIT_FRAGMENTATION || fit.outcome == FitOutcome::NO_FIT_PROTECTED_STATE || fit.outcome == FitOutcome::FIT_NOW;
  printf("downstream result: %s\n", deterministic ? "DETERMINISTIC_RESULT" : "NON_DETERMINISTIC");
  return deterministic ? 0 : 1;
}