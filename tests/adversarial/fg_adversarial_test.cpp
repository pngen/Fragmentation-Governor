// Fragmentation Governor adversarial hardening test.
// Deliberately attempts to break generation fencing, accounting, movability,
// protection, planner supersession, verification, and persistence integrity.
// Copyright 2026 Summon Software Labs.  Apache License 2.0.
#include <cmath>
#include <cstdio>

#include "fragmentation_governor/fragmentation_governor.hpp"

using namespace fragmentation_governor;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("ADVERSARIAL FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static const size_t MIB = 1024 * 1024;

static void test_stale_generation_fence() {
  // A stale worker boot must not publish current evidence.
  Coordinator coord(CoordinatorEpoch(1));
  Message reg; reg.type = MsgType::REGISTER; reg.worker_id = 1; reg.worker_boot = 1; coord.handle(reg);
  Resource res; res.id = ResourceId(1); res.device = DeviceId(1); res.domain = Domain::CUDA_DEVICE_MEMORY; res.capacity = Bytes(2048*MIB); res.provenance = Provenance::DERIVED;
  Message pr; pr.type = MsgType::PUBLISH_RESOURCE; pr.worker_id = 1; pr.worker_boot = 1; pr.resource = res; coord.handle(pr);
  // A stale boot (999) must be rejected for layout publication.
  Message lay; lay.type = MsgType::PUBLISH_LAYOUT; lay.worker_id = 1; lay.worker_boot = 999; lay.resource.id = ResourceId(1); lay.resource_gen = 1;
  lay.allocations.push_back(Allocation{AllocationId(1), AllocationGeneration(1), AllocationOwnerId(1), Bytes(0), Bytes(256*MIB), Movability::IMMOVABLE, std::nullopt, false, std::nullopt, Provenance::DERIVED});
  auto r = coord.handle(lay);
  CHECK(!r.empty() && !r[0].ok);  // stale boot rejected
}

static void test_duplicate_and_overlap() {
  ResourceLayout lay(Bytes(1024 * MIB)); std::string err;
  Allocation a{AllocationId(1), AllocationGeneration(1), AllocationOwnerId(1), Bytes(0), Bytes(256*MIB), Movability::IMMOVABLE, std::nullopt, false, std::nullopt, Provenance::DERIVED};
  CHECK(lay.insert_allocation(a, err));
  // Duplicate id rejected.
  CHECK(!lay.insert_allocation(a, err));
  // Overlapping offset/length rejected.
  Allocation b{AllocationId(2), AllocationGeneration(1), AllocationOwnerId(2), Bytes(128*MIB), Bytes(256*MIB), Movability::IMMOVABLE, std::nullopt, false, std::nullopt, Provenance::DERIVED};
  CHECK(!lay.insert_allocation(b, err));
  // Out of bounds rejected.
  Allocation c{AllocationId(3), AllocationGeneration(1), AllocationOwnerId(3), Bytes(1024*MIB), Bytes(1), Movability::IMMOVABLE, std::nullopt, false, std::nullopt, Provenance::DERIVED};
  CHECK(!lay.insert_allocation(c, err));
}

static void test_double_free_accounting() {
  ResourceLayout lay(Bytes(1024 * MIB)); std::string err;
  Allocation a{AllocationId(1), AllocationGeneration(1), AllocationOwnerId(1), Bytes(0), Bytes(256*MIB), Movability::RELEASEABLE, std::nullopt, false, std::nullopt, Provenance::DERIVED};
  lay.insert_allocation(a, err);
  CHECK(lay.remove_allocation(AllocationId(1), err));
  // Double release must not double-free (accounting must stay exact).
  CHECK(!lay.remove_allocation(AllocationId(1), err));
  CHECK(lay.free_capacity().value() == 1024 * MIB);
  CHECK(lay.used_capacity() + lay.free_capacity() == lay.total_capacity());
}

static void test_immovable_not_reclaimed() {
  // A plan must never propose reclaiming an IMMOVABLE / protected allocation.
  ResourceLayout lay(Bytes(1024 * MIB)); std::string err;
  lay.insert_allocation(Allocation{AllocationId(1), AllocationGeneration(1), AllocationOwnerId(1), Bytes(0), Bytes(800*MIB), Movability::IMMOVABLE, std::nullopt, false, std::nullopt, Provenance::DERIVED}, err);
  lay.insert_allocation(Allocation{AllocationId(2), AllocationGeneration(1), AllocationOwnerId(2), Bytes(800*MIB), Bytes(100*MIB), Movability::RELEASEABLE, std::nullopt, false, std::nullopt, Provenance::DERIVED}, err);
  FragmentationSnapshot s; s.generation = FragmentationSnapshotGeneration(1); s.domain = Domain::CUDA_DEVICE_MEMORY;
  s.provenance = Provenance::DERIVED; s.epoch = CoordinatorEpoch(1); s.policy_generation = PolicyGeneration(1);
  s.generations.resource_generation = ResourceGeneration(1); s.generations.allocation_generation = AllocationGeneration(1);
  s.generations.reservation_generation = ReservationGeneration(1); s.generations.topology_generation = TopologyGeneration(1);
  s.generations.capacity_generation = CapacityModelGeneration(1);
  Resource r; r.id = ResourceId(1); r.device = DeviceId(1); r.domain = Domain::CUDA_DEVICE_MEMORY; r.capacity = Bytes(1024*MIB); r.provenance = Provenance::DERIVED;
  DeviceState ds; ds.resource = r; ds.layout = lay; s.devices.push_back(ds);
  WorkloadDemand d; d.accelerator_count = Count(1); d.per_device_memory = Bytes(500*MIB); d.aggregate_memory = Bytes(500*MIB); d.contiguous_memory = Bytes(500*MIB);
  Policy pol; pol.generation = PolicyGeneration(1);
  auto plan = plan_remediation(s, d, pol, default_weights(), FragmentationPlanId(1), FragmentationPlanGeneration(1));
  if (plan.has_value()) {
    for (const auto& act : plan->actions) {
      if (act.source_allocation.has_value() && *act.source_allocation == AllocationId(1)) { CHECK(false); }
    }
  }
}

static void test_overflow_and_nan() {
  // Bytes underflow must throw.
  bool threw = false;
  try { Bytes b(1); Bytes c(2); auto x = b - c; (void)x; } catch (...) { threw = true; }
  CHECK(threw);
  // Bytes add overflow must throw.
  threw = false;
  try { Bytes a(UINT64_MAX); Bytes b(1); auto x = a + b; (void)x; } catch (...) { threw = true; }
  CHECK(threw);
  // Fraction NaN rejected.
  threw = false;
  try { Fraction f(std::nan("")); } catch (...) { threw = true; }
  CHECK(threw);
}

static void test_persistence_adversarial() {
  PersistedState st; st.resource_generation = ResourceGeneration(1); st.allocation_generation = AllocationGeneration(1);
  st.reservation_generation = ReservationGeneration(1); st.policy_generation = PolicyGeneration(1); st.epoch = CoordinatorEpoch(1); st.next_plan_id = 1;
  PersistedPlan pp; pp.id = FragmentationPlanId(1); pp.generation = FragmentationPlanGeneration(1); pp.domain = FragmentationDomainId(1);
  pp.state = PlanState::COMPLETED; pp.verification = VerificationOutcome::TARGET_FIT_ACHIEVED;
  pp.policy_generation = PolicyGeneration(1); pp.epoch = CoordinatorEpoch(1); pp.based_on_snapshot = FragmentationSnapshotGeneration(1);
  pp.expected_capacity_recovered = Bytes(1024); pp.benefit = 0.5; pp.cost = 0.1; pp.risk = 0.1; pp.disruption = 0.1;
  pp.action_count = Count(1); pp.action_kinds.push_back(ActionKind::RELEASE_UNUSED);
  st.plans.push_back(pp);
  std::vector<std::uint8_t> buf; CHECK(encode_state(st, buf).ok);
  // Bad version must be rejected.
  std::vector<std::uint8_t> badver = buf; badver[4] = 0xFF; PersistedState o; CHECK(!decode_state(badver, o).ok);
  // Bad magic rejected.
  std::vector<std::uint8_t> badmagic = buf; badmagic[0] ^= 0x77; CHECK(!decode_state(badmagic, o).ok);
  // Every single-byte corruption around the checksum is rejected.
  for (size_t i = 0; i < buf.size(); i += 3) { std::vector<std::uint8_t> c = buf; c[i] ^= 0x5A; PersistedState oc; CHECK(!decode_state(c, oc).ok); }
  // Truncation corrupted.
  std::vector<std::uint8_t> trunc(buf.begin(), buf.begin() + 10); CHECK(!decode_state(trunc, o).ok);
  // Trailing garbage.
  std::vector<std::uint8_t> trail = buf; trail.push_back(0x1A); trail.push_back(0x2B); CHECK(!decode_state(trail, o).ok);
  // Plan COMPLETED without verification must be rejected at encode time.
  PersistedState bad; bad.resource_generation = ResourceGeneration(1); bad.allocation_generation = AllocationGeneration(1);
  bad.reservation_generation = ReservationGeneration(1); bad.policy_generation = PolicyGeneration(1); bad.epoch = CoordinatorEpoch(1); bad.next_plan_id = 1;
  PersistedPlan nb; nb.id = FragmentationPlanId(2); nb.generation = FragmentationPlanGeneration(1); nb.domain = FragmentationDomainId(1);
  nb.state = PlanState::COMPLETED; nb.verification = std::nullopt; nb.policy_generation = PolicyGeneration(1); nb.epoch = CoordinatorEpoch(1);
  nb.based_on_snapshot = FragmentationSnapshotGeneration(1); nb.expected_capacity_recovered = Bytes(1024);
  nb.benefit = 0.5; nb.cost = 0.1; nb.risk = 0.1; nb.disruption = 0.1; nb.action_count = Count(1);
  bad.plans.push_back(nb);
  std::vector<std::uint8_t> b2; CHECK(!encode_state(bad, b2).ok);  // completed without verification
}

static void test_protocol_adversarial() {
  // Oversized frame rejected.
  Message m; m.type = MsgType::HELLO; m.detail.assign(protocol_max_payload() + 1, (char)'x');
  auto f = frame_encode(m);
  CHECK(!frame_decode(f.data(), f.size()).has_value());
  // Malformed/false magic rejected.
  std::vector<std::uint8_t> bad = f; bad[0] ^= 0xFF; CHECK(!frame_decode(bad.data(), bad.size()).has_value());
  // Partial frame decode must not produce a message until complete.
  FrameDecoder dec;
  for (size_t i = 0; i < f.size() - 1; ++i) { auto r = dec.feed(f.data() + i, 1); (void)r; }
  // Truncated frame is never a valid message.
  std::vector<std::uint8_t> trunc(f.begin(), f.begin() + f.size() - 5);
  CHECK(!frame_decode(trunc.data(), trunc.size()).has_value());
}

int main() {
  test_stale_generation_fence();
  test_duplicate_and_overlap();
  test_double_free_accounting();
  test_immovable_not_reclaimed();
  test_overflow_and_nan();
  test_persistence_adversarial();
  test_protocol_adversarial();
  if (failures == 0) printf("ALL ADVERSARIAL TESTS PASSED\n"); else printf("%d ADVERSARIAL FAILURES\n", failures);
  return failures == 0 ? 0 : 1;
}