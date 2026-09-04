// Fragmentation Governor CLI inspection tooling.
// Builds a representative domain in-process and prints full governance state.
// Copyright 2026 Summon Software Labs.  Apache License 2.0.
#include <cstdio>
#include <string>

#include "fragmentation_governor/fragmentation_governor.hpp"

using namespace fragmentation_governor;

int main() {
  const size_t MIB = 1024 * 1024;
  const size_t U = 256 * MIB;

  // Build the representative fragmented domain (immovable blockers + a small
  // movable allocation).
  FragmentationSnapshot snap;
  snap.generation = FragmentationSnapshotGeneration(1);
  snap.domain = Domain::CUDA_DEVICE_MEMORY; snap.provenance = Provenance::DERIVED;
  snap.epoch = CoordinatorEpoch(1); snap.policy_generation = PolicyGeneration(1);
  snap.generations.resource_generation = ResourceGeneration(1);
  snap.generations.allocation_generation = AllocationGeneration(1);
  snap.generations.reservation_generation = ReservationGeneration(1);
  snap.generations.topology_generation = TopologyGeneration(1);
  snap.generations.capacity_generation = CapacityModelGeneration(1);
  snap.generations.capability_generation = CapabilityGeneration(1);
  snap.generations.placement_generation = PlacementGeneration(1);
  snap.generations.health_generation = HealthGeneration(1);

  Resource res; res.id = ResourceId(1); res.generation = ResourceGeneration(1);
  res.pool = ResourcePoolId(1); res.device = DeviceId(1); res.domain = Domain::CUDA_DEVICE_MEMORY;
  res.capacity = Bytes(2048 * MIB); res.provenance = Provenance::DERIVED;
  ResourceLayout layout(res.capacity);
  std::string err;
  Allocation a1{AllocationId(1), AllocationGeneration(1), AllocationOwnerId(10), Bytes(0), Bytes(U), Movability::IMMOVABLE, std::nullopt, false, std::nullopt, Provenance::DERIVED};
  Allocation a2{AllocationId(2), AllocationGeneration(1), AllocationOwnerId(11), Bytes(2*U), Bytes(U), Movability::IMMOVABLE, std::nullopt, false, std::nullopt, Provenance::DERIVED};
  Allocation a3{AllocationId(3), AllocationGeneration(1), AllocationOwnerId(12), Bytes(4*U), Bytes(U), Movability::IMMOVABLE, std::nullopt, false, std::nullopt, Provenance::DERIVED};
  Allocation a4{AllocationId(4), AllocationGeneration(1), AllocationOwnerId(13), Bytes(6*U), Bytes(U), Movability::IMMOVABLE, std::nullopt, false, std::nullopt, Provenance::DERIVED};
  Allocation a5{AllocationId(5), AllocationGeneration(1), AllocationOwnerId(14), Bytes(3*U), Bytes(U/4), Movability::MOVABLE_AFTER_QUIESCE, std::nullopt, false, std::nullopt, Provenance::DERIVED};
  layout.insert_allocation(a1, err); layout.insert_allocation(a2, err);
  layout.insert_allocation(a3, err); layout.insert_allocation(a4, err);
  layout.insert_allocation(a5, err);
  DeviceState ds; ds.resource = res; ds.layout = layout; snap.devices.push_back(ds);

  WorkloadDemand demand; demand.id = WorkloadDemandId(1); demand.generation = WorkloadDemandGeneration(1);
  demand.accelerator_count = Count(1); demand.per_device_memory = Bytes(512 * MIB);
  demand.aggregate_memory = Bytes(512 * MIB); demand.contiguous_memory = Bytes(512 * MIB);

  printf("DOMAIN = %s\n", to_string(snap.domain).data());
  printf("generations: res=%llu alloc=%llu resv=%llu topo=%llu cap=%llu policy=%llu epoch=%llu\n",
         (unsigned long long)snap.generations.resource_generation.value(),
         (unsigned long long)snap.generations.allocation_generation.value(),
         (unsigned long long)snap.generations.reservation_generation.value(),
         (unsigned long long)snap.generations.topology_generation.value(),
         (unsigned long long)snap.generations.capacity_generation.value(),
         (unsigned long long)snap.policy_generation.value(),
         (unsigned long long)snap.epoch.value());

  FragmentationMetrics m = compute_metrics(layout, &demand);
  printf("total=%zuMiB used=%zuMiB free=%zuMiB largest=%zuMiB\n",
         (size_t)m.total_capacity.value()/MIB, (size_t)m.used_capacity.value()/MIB,
         (size_t)m.free_capacity.value()/MIB, (size_t)m.largest_free_block.value()/MIB);
  printf("stranded=%zuMiB reclaimable=%zuMiB protected=%zuMiB\n",
         (size_t)m.stranded_capacity.value()/MIB, (size_t)m.reclaimable_capacity.value()/MIB,
         (size_t)m.protected_capacity.value()/MIB);
  printf("extern_frag_ratio=%.4f workload_fits=%d\n", m.external_fragmentation_ratio, m.workload_fits);

  printf("allocations:\n");
  for (const auto& [id, alloc] : layout.allocations()) {
    printf("  id=%llu off=%zuMiB len=%zuMiB mov=%s protected=%d\n",
           (unsigned long long)id.value(), (size_t)alloc.offset.value()/MIB,
           (size_t)alloc.length.value()/MIB, to_string(alloc.movability).data(),
           alloc.policy_protected);
  }

  FitResult fit = analyze_fit(snap, demand);
  printf("workload fit = %s\n", to_string(fit.outcome).data());
  for (const auto& d : fit.block_diagnostics) printf("  blocker: %s\n", d.c_str());

  Policy pol; pol.generation = PolicyGeneration(1);
  auto plan = plan_remediation(snap, demand, pol, default_weights(), FragmentationPlanId(1), FragmentationPlanGeneration(1));
  if (plan.has_value()) {
    printf("plan state=%s benefit=%.4f cost=%.4f risk=%.4f disruption=%.4f\n",
           to_string(plan->state).data(), plan->benefit.value(), plan->cost.value(),
           plan->risk.value(), plan->disruption.value());
    printf("actions=%zu\n", plan->actions.size());
    for (const auto& act : plan->actions) printf("  action=%s cap=%zuMiB\n", to_string(act.kind).data(), (size_t)act.expected_capacity_recovered.value()/MIB);
  } else {
    printf("plan = none (no beneficial action)\n");
  }
  return 0;
}
