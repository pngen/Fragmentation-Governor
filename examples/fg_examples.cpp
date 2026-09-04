// Fragmentation Governor runnable examples.
// Copyright 2026 Summon Software Labs.  Apache License 2.0.
#include <cstdio>
#include "fragmentation_governor/fragmentation_governor.hpp"
using namespace fragmentation_governor;
static const size_t MIB = 1024 * 1024;
static WorkloadDemand mkdemand(size_t bytes) {
  WorkloadDemand d; d.id = WorkloadDemandId(1); d.generation = WorkloadDemandGeneration(1);
  d.accelerator_count = Count(1); d.per_device_memory = Bytes(bytes); d.aggregate_memory = Bytes(bytes); d.contiguous_memory = Bytes(bytes);
  return d;
}
static FragmentationSnapshot mksnap(const Resource& r, const ResourceLayout& lay) {
  FragmentationSnapshot s; s.generation = FragmentationSnapshotGeneration(1); s.domain = Domain::CUDA_DEVICE_MEMORY; s.provenance = Provenance::DERIVED; s.epoch = CoordinatorEpoch(1); s.policy_generation = PolicyGeneration(1);
  s.generations.resource_generation = ResourceGeneration(1); s.generations.allocation_generation = AllocationGeneration(1); s.generations.reservation_generation = ReservationGeneration(1); s.generations.topology_generation = TopologyGeneration(1); s.generations.capacity_generation = CapacityModelGeneration(1);
  DeviceState ds; ds.resource = r; ds.layout = lay; s.devices.push_back(ds); return s;
}
int main() {
  // 1. Basic memory fragmentation.
  { ResourceLayout lay(Bytes(2048 * MIB)); std::string err;
    for (int i = 0; i < 4; ++i) lay.insert_allocation(Allocation{AllocationId(100+i), AllocationGeneration(1), AllocationOwnerId(10+i), Bytes((size_t)i*512*MIB), Bytes(256*MIB), Movability::IMMOVABLE, std::nullopt, false, std::nullopt, Provenance::DERIVED}, err);
    WorkloadDemand dd = mkdemand(512*MIB);
    FragmentationMetrics m = compute_metrics(lay, &dd);
    printf("[basic] free=%zuMiB largest=%zuMiB demand=512MiB extRatio=%.4f\n", (size_t)m.free_capacity.value()/MIB, (size_t)m.largest_free_block.value()/MIB, m.external_fragmentation_ratio); }
  // 2. Workload-specific fit.
  { ResourceLayout lay(Bytes(2048 * MIB)); std::string err;
    for (int i = 0; i < 4; ++i) lay.insert_allocation(Allocation{AllocationId(200+i), AllocationGeneration(1), AllocationOwnerId(20+i), Bytes((size_t)i*512*MIB), Bytes(256*MIB), Movability::IMMOVABLE, std::nullopt, false, std::nullopt, Provenance::DERIVED}, err);
    bool sf = lay.largest_free_block() >= mkdemand(128*MIB).contiguous_memory;
    bool bf = lay.largest_free_block() >= mkdemand(512*MIB).contiguous_memory;
    printf("[workload] largest=%zuMiB smallFits=%d bigFits=%d\n", (size_t)lay.largest_free_block().value()/MIB, (int)sf, (int)bf); }
  // 3. Largest-block deficit.
  { ResourceLayout lay(Bytes(1024 * MIB)); std::string err;
    lay.insert_allocation(Allocation{AllocationId(1), AllocationGeneration(1), AllocationOwnerId(1), Bytes(0), Bytes(400*MIB), Movability::IMMOVABLE, std::nullopt, false, std::nullopt, Provenance::DERIVED}, err);
    lay.insert_allocation(Allocation{AllocationId(2), AllocationGeneration(1), AllocationOwnerId(2), Bytes(400*MIB), Bytes(300*MIB), Movability::IMMOVABLE, std::nullopt, false, std::nullopt, Provenance::DERIVED}, err);
    FragmentationMetrics m = compute_metrics(lay);
    printf("[largest-block-deficit] free=%zuMiB largest=%zuMiB stranded=%zuMiB\n", (size_t)m.free_capacity.value()/MIB, (size_t)m.largest_free_block.value()/MIB, (size_t)m.stranded_capacity.value()/MIB); }
  // 4. Release remediation.
  { ResourceLayout lay(Bytes(2048 * MIB)); std::string err;
    for (int i = 0; i < 4; ++i) lay.insert_allocation(Allocation{AllocationId(300+i), AllocationGeneration(1), AllocationOwnerId(30+i), Bytes((size_t)i*512*MIB), Bytes(256*MIB), Movability::RELEASEABLE, std::nullopt, false, std::nullopt, Provenance::DERIVED}, err);
    Resource r; r.id = ResourceId(1); r.device = DeviceId(1); r.domain = Domain::CUDA_DEVICE_MEMORY; r.capacity = Bytes(2048*MIB); r.provenance = Provenance::DERIVED;
    FragmentationSnapshot s = mksnap(r, lay); Policy pol; pol.generation = PolicyGeneration(1);
    auto plan = plan_remediation(s, mkdemand(512*MIB), pol, default_weights(), FragmentationPlanId(1), FragmentationPlanGeneration(1));
    if (plan.has_value()) printf("[release] plan actions=%zu kind=%s recovery=%zuMiB\n", plan->actions.size(), to_string(plan->actions[0].kind).data(), (size_t)plan->actions[0].expected_capacity_recovered.value()/MIB);
    else printf("[release] no beneficial action\n"); }
  // 5. Topology fragmentation.
  { FragmentationSnapshot s; s.generation = FragmentationSnapshotGeneration(1); s.domain = Domain::CUDA_DEVICE_MEMORY; s.provenance = Provenance::DERIVED; s.epoch = CoordinatorEpoch(1); s.policy_generation = PolicyGeneration(1);
    s.generations.resource_generation = ResourceGeneration(1); s.generations.allocation_generation = AllocationGeneration(1); s.generations.reservation_generation = ReservationGeneration(1); s.generations.topology_generation = TopologyGeneration(1); s.generations.capacity_generation = CapacityModelGeneration(1);
    Resource ra; ra.id = ResourceId(1); ra.device = DeviceId(1); ra.topology_group = 1; ra.domain = Domain::CUDA_DEVICE_MEMORY; ra.provenance = Provenance::DERIVED;
    Resource rb; rb.id = ResourceId(2); rb.device = DeviceId(2); rb.topology_group = 2; rb.domain = Domain::CUDA_DEVICE_MEMORY; rb.provenance = Provenance::DERIVED;
    DeviceState dsa; dsa.resource = ra; dsa.layout = ResourceLayout(Bytes(1024*MIB)); DeviceState dsb; dsb.resource = rb; dsb.layout = ResourceLayout(Bytes(1024*MIB));
    s.devices.push_back(dsa); s.devices.push_back(dsb);
    WorkloadDemand d; d.accelerator_count = Count(2); d.topology_group = 1; d.per_device_memory = Bytes(100*MIB); d.aggregate_memory = Bytes(200*MIB); d.contiguous_memory = Bytes(100*MIB);
    printf("[topology] outcome=%s\n", to_string(analyze_fit(s, d).outcome).data()); }
  // 6. Temporal fragmentation.
  { std::vector<Reservation> rs;
    Reservation r1; r1.id = ReservationId(1); r1.generation = ReservationGeneration(1); r1.start = Duration(1000); r1.end = Duration(2000); r1.amount = Bytes(1024); r1.hard = true; rs.push_back(r1);
    Reservation r2; r2.id = ReservationId(2); r2.generation = ReservationGeneration(1); r2.start = Duration(3000); r2.end = Duration(4000); r2.amount = Bytes(1024); r2.hard = true; rs.push_back(r2);
    Duration earliest; bool ok = temporal_continuous_fit(rs, Duration(0), Duration(10000), Duration(500), earliest);
    printf("[temporal] continuous-500ns-window=%d earliest=%llu\n", ok, (unsigned long long)earliest.nanoseconds()); }
  // 7. Persistence round-trip.
  { PersistedState st; st.resource_generation = ResourceGeneration(1); st.allocation_generation = AllocationGeneration(1); st.reservation_generation = ReservationGeneration(1); st.policy_generation = PolicyGeneration(1); st.epoch = CoordinatorEpoch(1); st.next_plan_id = 2;
    PersistedPlan pp; pp.id = FragmentationPlanId(1); pp.generation = FragmentationPlanGeneration(1); pp.domain = FragmentationDomainId(1); pp.state = PlanState::COMPLETED; pp.verification = VerificationOutcome::TARGET_FIT_ACHIEVED; pp.policy_generation = PolicyGeneration(1); pp.epoch = CoordinatorEpoch(1); pp.based_on_snapshot = FragmentationSnapshotGeneration(1); pp.expected_capacity_recovered = Bytes(256*MIB); pp.benefit = 0.9; pp.cost = 0.1; pp.risk = 0.05; pp.disruption = 0.1; pp.action_count = Count(1); pp.action_kinds.push_back(ActionKind::RELEASE_UNUSED);
    st.plans.push_back(pp);
    std::vector<std::uint8_t> b; CodecResult e = encode_state(st, b); PersistedState out; CodecResult d = decode_state(b, out);
    printf("[persistence] round-trip ok=%d/%d\n", e.ok, d.ok); }
  printf("\nEXAMPLES COMPLETE\n");
  return 0;
}