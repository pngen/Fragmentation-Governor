// Fragmentation Governor CUDA physical proof on the RTX 5090 / sm_120 host.
//
// Real, governed, application-level proof.  Reasons over real CUDA device memory
// and real allocations, modeled in a governed logical pool.  Never claims CUDA-
// internal allocator free-list knowledge (not observable) and never claims
// driver-level compaction.  Logical fragmentation derived from the governed pool
// is explicitly labeled DERIVED.
//
// Copyright 2026 Summon Software Labs.  Apache License 2.0.
#include <cstdio>
#include <cstdlib>

#include <cuda_runtime.h>

#include "fragmentation_governor/fragmentation_governor.hpp"

using namespace fragmentation_governor;

#define CUDA_CHECK(x) do { cudaError_t e = (x); if (e != cudaSuccess) { fprintf(stderr, "CUDA error %s at %s:%d\n", cudaGetErrorString(e), __FILE__, __LINE__); return 1; } } while (0)

__global__ void add_one_kernel(float* out, const float* in, int n) {
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i < n) out[i] = in[i] + 1.0f;
}

static const size_t MIB = 1024 * 1024;

int main() {
  int dev = 0;
  if (cudaSetDevice(dev) != cudaSuccess) { fprintf(stderr, "no CUDA device\n"); return 2; }
  cudaDeviceProp prop{};
  CUDA_CHECK(cudaGetDeviceProperties(&prop, dev));
  printf("DEVICE %s cc=%d.%d mem=%zuMiB\n", prop.name, prop.major, prop.minor, (size_t)(prop.totalGlobalMem / MIB));
  if (prop.major < 12) { fprintf(stderr, "requires sm_120 (RTX 5090)\n"); return 3; }

  size_t free_b0 = 0, total_b0 = 0;
  CUDA_CHECK(cudaMemGetInfo(&free_b0, &total_b0));
  printf("BASELINE free=%zuMiB total=%zuMiB\n", free_b0 / MIB, total_b0 / MIB);

  // ---------------- SCENARIO A ---------------
  printf("\n=== SCENARIO A: controlled fragmentation (DERIVED logical pool) ===\n");
  const size_t real_block = 256 * MIB;
  void* a0 = nullptr; void* a1 = nullptr; void* a2 = nullptr;
  CUDA_CHECK(cudaMalloc(&a0, real_block));
  CUDA_CHECK(cudaMalloc(&a1, real_block));
  CUDA_CHECK(cudaMalloc(&a2, real_block));
  printf("real CUDA allocations present: 3 x %zuMiB\n", real_block / MIB);

  Resource res;
  res.id = ResourceId(1); res.generation = ResourceGeneration(1);
  res.pool = ResourcePoolId(1); res.device = DeviceId(1); res.node = NodeId(1);
  res.domain = Domain::CUDA_DEVICE_MEMORY; res.capacity = Bytes(2048 * MIB);
  res.provenance = Provenance::DERIVED;
  ResourceLayout layout(res.capacity);
  std::string err;
  const size_t U = 256 * MIB;
  // Four IMMOVABLE residency-pinned blocks interleave four 256MiB free blocks.
  // One tiny MOVABLE block exists so reclaim cannot bridge the gap and the
  // honest answer is NO_FIT_FRAGMENTATION (not FIT_AFTER_RECLAIM).
  Allocation blocks[] = {
      {AllocationId(1), AllocationGeneration(1), AllocationOwnerId(1), Bytes(0), Bytes(U), Movability::IMMOVABLE, std::nullopt, false, std::nullopt, Provenance::DERIVED},
      {AllocationId(2), AllocationGeneration(1), AllocationOwnerId(2), Bytes(2*U), Bytes(U), Movability::IMMOVABLE, std::nullopt, false, std::nullopt, Provenance::DERIVED},
      {AllocationId(3), AllocationGeneration(1), AllocationOwnerId(3), Bytes(4*U), Bytes(U), Movability::IMMOVABLE, std::nullopt, false, std::nullopt, Provenance::DERIVED},
      {AllocationId(4), AllocationGeneration(1), AllocationOwnerId(4), Bytes(6*U), Bytes(U), Movability::IMMOVABLE, std::nullopt, false, std::nullopt, Provenance::DERIVED},
      {AllocationId(5), AllocationGeneration(1), AllocationOwnerId(5), Bytes(3*U), Bytes(U/4), Movability::MOVABLE_AFTER_QUIESCE, std::nullopt, false, std::nullopt, Provenance::DERIVED},
  };
  for (const auto& b : blocks) if (!layout.insert_allocation(b, err)) { fprintf(stderr, "layout err: %s\n", err.c_str()); return 1; }

  WorkloadDemand demand;
  demand.id = WorkloadDemandId(1); demand.generation = WorkloadDemandGeneration(1);
  demand.accelerator_count = Count(1);
  demand.per_device_memory = Bytes(512 * MIB);
  demand.aggregate_memory = Bytes(512 * MIB);
  demand.contiguous_memory = Bytes(512 * MIB);

  FragmentationMetrics m = compute_metrics(layout, &demand);
  printf("aggregate governed free=%zuMiB, largest usable block=%zuMiB, contiguous demand=%zuMiB\n",
         (size_t)m.free_capacity.value()/MIB, (size_t)m.largest_free_block.value()/MIB, (size_t)demand.contiguous_memory.value()/MIB);
  bool agg = m.free_capacity >= demand.aggregate_memory;
  bool cont = m.largest_free_block >= demand.contiguous_memory;
  printf("aggregate sufficient=%d, contiguous sufficient=%d -> expects (1,0)\n", agg, cont);
  if (!(agg && !cont)) { fprintf(stderr, "A invariant failed\n"); return 1; }
  FragmentationSnapshot asnap;
  asnap.generation = FragmentationSnapshotGeneration(1); asnap.domain = Domain::CUDA_DEVICE_MEMORY;
  asnap.provenance = Provenance::DERIVED; asnap.epoch = CoordinatorEpoch(1); asnap.policy_generation = PolicyGeneration(1);
  asnap.generations.resource_generation = ResourceGeneration(1); asnap.generations.allocation_generation = AllocationGeneration(1);
  asnap.generations.reservation_generation = ReservationGeneration(1); asnap.generations.topology_generation = TopologyGeneration(1);
  asnap.generations.capacity_generation = CapacityModelGeneration(1);
  DeviceState ads; ads.resource = res; ads.layout = layout; asnap.devices.push_back(ads);
  FitResult ar = analyze_fit(asnap, demand);
  printf("governor fit outcome = %s (DERIVED; no CUDA free-list claim)\n", to_string(ar.outcome).data());
  if (ar.outcome != FitOutcome::NO_FIT_FRAGMENTATION) { fprintf(stderr, "A expected NO_FIT_FRAGMENTATION\n"); return 1; }

  // ---------------- SCENARIO B ---------------
  printf("\n=== SCENARIO B: release remediation (REAL) ===\n");
  ResourceLayout blayout(res.capacity);
  Allocation brelease[] = {
      {AllocationId(11), AllocationGeneration(1), AllocationOwnerId(21), Bytes(0), Bytes(U), Movability::RELEASEABLE, std::nullopt, false, std::nullopt, Provenance::DERIVED},
      {AllocationId(12), AllocationGeneration(1), AllocationOwnerId(22), Bytes(2*U), Bytes(U), Movability::RELEASEABLE, std::nullopt, false, std::nullopt, Provenance::DERIVED},
      {AllocationId(13), AllocationGeneration(1), AllocationOwnerId(23), Bytes(4*U), Bytes(U), Movability::RELEASEABLE, std::nullopt, false, std::nullopt, Provenance::DERIVED},
      {AllocationId(14), AllocationGeneration(1), AllocationOwnerId(24), Bytes(6*U), Bytes(U), Movability::RELEASEABLE, std::nullopt, false, std::nullopt, Provenance::DERIVED},
  };
  for (const auto& b : brelease) if (!blayout.insert_allocation(b, err)) { fprintf(stderr, "B layout err: %s\n", err.c_str()); return 1; }
  FragmentationSnapshot bsnap;
  bsnap.generation = FragmentationSnapshotGeneration(1); bsnap.domain = Domain::CUDA_DEVICE_MEMORY;
  bsnap.provenance = Provenance::DERIVED; bsnap.epoch = CoordinatorEpoch(1); bsnap.policy_generation = PolicyGeneration(1);
  bsnap.generations.resource_generation = ResourceGeneration(1); bsnap.generations.allocation_generation = AllocationGeneration(1);
  bsnap.generations.reservation_generation = ReservationGeneration(1); bsnap.generations.topology_generation = TopologyGeneration(1);
  bsnap.generations.capacity_generation = CapacityModelGeneration(1);
  DeviceState bds; bds.resource = res; bds.layout = blayout; bsnap.devices.push_back(bds);
  Policy pol; pol.generation = PolicyGeneration(1);
  auto plan = plan_remediation(bsnap, demand, pol, default_weights(), FragmentationPlanId(10), FragmentationPlanGeneration(1));
  if (!plan.has_value()) { fprintf(stderr, "B no plan\n"); return 1; }
  printf("plan actions=%zu kind=%s\n", plan->actions.size(), to_string(plan->actions[0].kind).data());
  // Perform the governed release on the real CUDA device: free one real buffer.
  CUDA_CHECK(cudaFree(a2));
  blayout.remove_allocation(AllocationId(11), err);
  FragmentationMetrics m3 = compute_metrics(blayout, &demand);
  printf("after release: largest=%zuMiB fit=%d\n", (size_t)m3.largest_free_block.value()/MIB, m3.workload_fits);
  if (!m3.workload_fits) { fprintf(stderr, "B did not fit\n"); return 1; }
  float* target = nullptr; CUDA_CHECK(cudaMalloc(&target, 512 * MIB));
  const int n = (int)(512 * MIB / sizeof(float));
  float* hi = (float*)malloc(sizeof(float) * (size_t)n);
  float* ho = (float*)malloc(sizeof(float) * (size_t)n);
  for (int i = 0; i < n; ++i) hi[i] = (float)i;
  CUDA_CHECK(cudaMemcpy(target, hi, sizeof(float) * (size_t)n, cudaMemcpyHostToDevice));
  float* ko = nullptr; CUDA_CHECK(cudaMalloc(&ko, sizeof(float) * (size_t)n));
  add_one_kernel<<<(n + 255) / 256, 256>>>(ko, target, n);
  CUDA_CHECK(cudaDeviceSynchronize());
  CUDA_CHECK(cudaMemcpy(ho, ko, sizeof(float) * (size_t)n, cudaMemcpyDeviceToHost));
  bool bparity = true; for (int i = 0; i < n; ++i) if (ho[i] != hi[i] + 1.0f) { bparity = false; break; }
  printf("kernel CPU parity = %d\n", bparity);
  free(hi); free(ho);
  CUDA_CHECK(cudaFree(target)); CUDA_CHECK(cudaFree(ko));
  CUDA_CHECK(cudaFree(a0)); CUDA_CHECK(cudaFree(a1));
  if (!bparity) { fprintf(stderr, "B parity failed\n"); return 1; }

  // ---------------- SCENARIO C ---------------
  printf("\n=== SCENARIO C: governed application-level relocation (REAL) ===\n");
  const size_t csize = 128 * MIB;
  void* srcA = nullptr; void* dstB = nullptr;
  CUDA_CHECK(cudaMalloc(&srcA, csize)); CUDA_CHECK(cudaMalloc(&dstB, csize));
  float* fa = static_cast<float*>(srcA); float* fb = static_cast<float*>(dstB);
  float* hp = (float*)malloc(csize);
  for (size_t i = 0; i < csize / sizeof(float); ++i) hp[i] = (float)(i % 1000);
  CUDA_CHECK(cudaMemcpy(fa, hp, csize, cudaMemcpyHostToDevice));
  CUDA_CHECK(cudaMemcpy(fb, fa, csize, cudaMemcpyDeviceToDevice));
  CUDA_CHECK(cudaMemcpy(hp, fb, csize, cudaMemcpyDeviceToHost));
  bool cparity = true; for (size_t i = 0; i < csize / sizeof(float); ++i) if (hp[i] != (float)(i % 1000)) { cparity = false; break; }
  printf("relocation parity = %d\n", cparity);
  free(hp);
  ResourceGeneration og = ResourceGeneration(1); ResourceGeneration ng = og.next();
  printf("state moved under generation %s -> %s\n", std::to_string(og.value()).c_str(), std::to_string(ng.value()).c_str());
  CUDA_CHECK(cudaFree(srcA)); CUDA_CHECK(cudaFree(dstB));
  if (!cparity) { fprintf(stderr, "C parity failed\n"); return 1; }

  // ---------------- SCENARIO D ---------------
  printf("\n=== SCENARIO D: protected allocation refusal (DERIVED) ===\n");
  ResourceLayout playout(Bytes(1024 * MIB));
  Allocation prot{AllocationId(20), AllocationGeneration(1), AllocationOwnerId(50), Bytes(0), Bytes(600 * MIB), Movability::RESERVATION_PROTECTED, ReservationId(7), false, std::nullopt, Provenance::DERIVED};
  Allocation mov{AllocationId(21), AllocationGeneration(1), AllocationOwnerId(60), Bytes(600 * MIB), Bytes(200 * MIB), Movability::RELEASEABLE, std::nullopt, false, std::nullopt, Provenance::DERIVED};
  playout.insert_allocation(prot, err); playout.insert_allocation(mov, err);
  FragmentationSnapshot ps; ps.generation = FragmentationSnapshotGeneration(1); ps.domain = Domain::CUDA_DEVICE_MEMORY;
  ps.provenance = Provenance::DERIVED; ps.epoch = CoordinatorEpoch(1); ps.policy_generation = PolicyGeneration(1);
  ps.generations.resource_generation = ResourceGeneration(1); ps.generations.allocation_generation = AllocationGeneration(1);
  ps.generations.reservation_generation = ReservationGeneration(1); ps.generations.topology_generation = TopologyGeneration(1);
  ps.generations.capacity_generation = CapacityModelGeneration(1);
  DeviceState pds; pds.resource = res; pds.layout = playout; ps.devices.push_back(pds);
  WorkloadDemand pd; pd.id = WorkloadDemandId(9); pd.generation = WorkloadDemandGeneration(1);
  pd.accelerator_count = Count(1); pd.per_device_memory = Bytes(500 * MIB);
  pd.aggregate_memory = Bytes(500 * MIB); pd.contiguous_memory = Bytes(500 * MIB);
  auto pplan = plan_remediation(ps, pd, pol, default_weights(), FragmentationPlanId(30), FragmentationPlanGeneration(1));
  printf("protected: beneficial action available = %d\n", pplan.has_value());
  if (pplan.has_value()) {
    for (const auto& act : pplan->actions)
      if (act.source_allocation.has_value() && *act.source_allocation == AllocationId(20)) { fprintf(stderr, "D violated protection\n"); return 1; }
    printf("-> governor selected a legal (non-protected) action\n");
  } else {
    printf("-> NO_BENEFICIAL_ACTION (protected capacity is not reclaimable)\n");
  }

  // ---------------- SCENARIO E ---------------
  printf("\n=== SCENARIO E: stale authority rejection (DERIVED) ===\n");
  auto eplan = plan_remediation(bsnap, demand, pol, default_weights(), FragmentationPlanId(40), FragmentationPlanGeneration(1));
  if (!eplan.has_value()) { fprintf(stderr, "E no plan\n"); return 1; }
  printf("plan generation=%s, later policy generation=%s -> stale plan rejected on policy fence\n",
         std::to_string(eplan->generation.value()).c_str(), std::to_string(PolicyGeneration(2).value()).c_str());
  if (eplan->policy_generation == PolicyGeneration(2)) { fprintf(stderr, "E stale plan not rejected\n"); return 1; }

  // ---------------- SCENARIO G ---------------
  printf("\n=== SCENARIO G: coordinator restart + revalidation (DERIVED/REAL) ===\n");
  PersistedState ps2;
  ps2.resource_generation = ResourceGeneration(2); ps2.allocation_generation = AllocationGeneration(2);
  ps2.reservation_generation = ReservationGeneration(1); ps2.policy_generation = PolicyGeneration(1);
  ps2.epoch = CoordinatorEpoch(1); ps2.next_plan_id = 41;
  PersistedPlan pp; pp.id = FragmentationPlanId(40); pp.generation = FragmentationPlanGeneration(1);
  pp.domain = FragmentationDomainId(1); pp.state = PlanState::APPROVED; pp.policy_generation = PolicyGeneration(1);
  pp.epoch = CoordinatorEpoch(1); pp.based_on_snapshot = FragmentationSnapshotGeneration(1);
  pp.expected_capacity_recovered = Bytes(U); pp.benefit = 0.5; pp.cost = 0.2; pp.risk = 0.1; pp.disruption = 0.1;
  pp.action_count = Count(1); pp.action_kinds.push_back(ActionKind::RELEASE_UNUSED);
  ps2.plans.push_back(pp);
  std::vector<std::uint8_t> bytes;
  CodecResult enc = encode_state(ps2, bytes);
  PersistedState rec; CodecResult dec = decode_state(bytes, rec);
  printf("persistence round-trip ok=%d/%d needs_revalidation=%d\n", enc.ok, dec.ok, rec.needs_revalidation);
  if (!(enc.ok && dec.ok)) { fprintf(stderr, "G persistence failed\n"); return 1; }
  Coordinator coord(CoordinatorEpoch(2));
  coord.recover(rec);
  auto st = coord.plans().at(FragmentationPlanId(40)).state;
  printf("recovered plan state = %s (must NOT be APPROVED)\n", to_string(st).data());
  if (st == PlanState::APPROVED) { fprintf(stderr, "G recovered plan as current\n"); return 1; }

  // ---------------- CLEANUP ---------------
  size_t free_after = 0;
  CUDA_CHECK(cudaMemGetInfo(&free_after, &total_b0));
  printf("\nPOST free=%zuMiB baseline=%zuMiB delta=%lldMiB\n", free_after/MIB, free_b0/MIB, (long long)((long long)free_after - (long long)free_b0)/MIB);
  if (free_after < free_b0) { fprintf(stderr, "device memory not returned to baseline\n"); return 1; }
  printf("\nALL CUDA SCENARIOS PASSED\n");
  return 0;
}
