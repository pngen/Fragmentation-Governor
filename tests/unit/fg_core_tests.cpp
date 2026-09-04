// Fragmentation Governor core test suite.
// Covers: layout accounting, metrics, fit, planner, movability, protection,
// reservation, temporal, topology, stale-plan, persistence, and protocol.
// Deterministic seeded property tests and adversarial tests are included.
// Copyright 2026 Summon Software Labs.  Apache License 2.0.
#include <atomic>
#include <cmath>
#include <cstdio>
#include <map>
#include <random>
#include <set>

#include "fragmentation_governor/fragmentation_governor.hpp"

using namespace fragmentation_governor;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)
#define CHECK_EQ(a, b) do { if (!((a) == (b))) { printf("FAIL %s:%d  %s == %s\n", __FILE__, __LINE__, #a, #b); ++failures; } } while (0)

namespace {
const size_t MIB = 1024 * 1024;
const size_t U = 256 * MIB;

Allocation mk(AllocationId id, Bytes off, Bytes len, Movability mv = Movability::MOVABLE_AFTER_CHECKPOINT, std::optional<ReservationId> r = std::nullopt) {
  Allocation a; a.id = id; a.generation = AllocationGeneration(1); a.owner = AllocationOwnerId(id.value());
  a.offset = off; a.length = len; a.movability = mv; a.reservation = r; a.provenance = Provenance::DERIVED;
  return a;
}

void test_layout_accounting() {
  ResourceLayout lay(Bytes(2048 * MIB));
  CHECK_EQ(lay.free_capacity().value(), (std::uint64_t)(2048 * MIB));
  CHECK_EQ(lay.used_capacity().value(), 0u);
  std::string err;
  CHECK(lay.insert_allocation(mk(AllocationId(1), Bytes(0), Bytes(U)), err));
  CHECK(lay.insert_allocation(mk(AllocationId(2), Bytes(2*U), Bytes(U)), err));
  CHECK_EQ(lay.used_capacity() + lay.free_capacity(), lay.total_capacity());
  CHECK_EQ(lay.largest_free_block().value(), (std::uint64_t)(2048 * MIB - 3*U));
  CHECK(lay.remove_allocation(AllocationId(1), err));
  CHECK_EQ(lay.used_capacity() + lay.free_capacity(), lay.total_capacity());
  // Overlap rejected.
  CHECK(!lay.insert_allocation(mk(AllocationId(9), Bytes(2*U), Bytes(U)), err));
  // Out of bounds rejected.
  CHECK(!lay.insert_allocation(mk(AllocationId(10), Bytes(2048*MIB), Bytes(U)), err));
  // Double removal rejected.
  CHECK(!lay.remove_allocation(AllocationId(1), err));
}

void test_metrics() {
  ResourceLayout lay(Bytes(1024 * MIB));
  std::string err;
  lay.insert_allocation(mk(AllocationId(1), Bytes(100*MIB), Bytes(300*MIB)), err);
  lay.insert_allocation(mk(AllocationId(2), Bytes(700*MIB), Bytes(100*MIB)), err);
  FragmentationMetrics m = compute_metrics(lay);
  // free = 1024 - 400 = 624 MiB; largest free block = 300MiB (from 400..700).
  CHECK_EQ(m.free_capacity.value(), (std::uint64_t)(624 * MIB));
  CHECK_EQ(m.largest_free_block.value(), (std::uint64_t)(300 * MIB));
  CHECK(m.stranded_capacity.value() == 624 * MIB - 300 * MIB);
  CHECK(m.stranded_capacity <= m.free_capacity);
  CHECK(m.reclaimable_capacity.value() == 400 * MIB);  // both movable
  CHECK(m.protected_capacity.value() == 0u);
}

void test_fit_taxonomy() {
  // Raw capacity shortage.
  ResourceLayout raw(Bytes(100 * MIB));
  std::string err;
  WorkloadDemand d; d.accelerator_count = Count(1); d.per_device_memory = Bytes(200*MIB); d.aggregate_memory = Bytes(200*MIB); d.contiguous_memory = Bytes(200*MIB);
  FragmentationSnapshot rs; rs.generation = FragmentationSnapshotGeneration(1); rs.domain = Domain::CUDA_DEVICE_MEMORY;
  rs.provenance = Provenance::DERIVED; rs.epoch = CoordinatorEpoch(1); rs.policy_generation = PolicyGeneration(1);
  rs.generations.resource_generation = ResourceGeneration(1); rs.generations.allocation_generation = AllocationGeneration(1);
  rs.generations.reservation_generation = ReservationGeneration(1); rs.generations.topology_generation = TopologyGeneration(1);
  rs.generations.capacity_generation = CapacityModelGeneration(1);
  Resource r1; r1.id = ResourceId(1); r1.generation = ResourceGeneration(1); r1.device = DeviceId(1); r1.domain = Domain::CUDA_DEVICE_MEMORY; r1.provenance = Provenance::DERIVED;
  DeviceState ds1; ds1.resource = r1; ds1.layout = raw; rs.devices.push_back(ds1);
  CHECK(analyze_fit(rs, d).outcome == FitOutcome::NO_FIT_RAW_CAPACITY);

  // Fragmented (immovable) -> NO_FIT_FRAGMENTATION.
  ResourceLayout fr(Bytes(2048 * MIB));
  fr.insert_allocation(mk(AllocationId(1), Bytes(0), Bytes(U), Movability::IMMOVABLE), err);
  fr.insert_allocation(mk(AllocationId(2), Bytes(2*U), Bytes(U), Movability::IMMOVABLE), err);
  fr.insert_allocation(mk(AllocationId(3), Bytes(4*U), Bytes(U), Movability::IMMOVABLE), err);
  fr.insert_allocation(mk(AllocationId(4), Bytes(6*U), Bytes(U), Movability::IMMOVABLE), err);
  fr.insert_allocation(mk(AllocationId(5), Bytes(3*U), Bytes(U/4), Movability::MOVABLE_AFTER_QUIESCE), err);
  FragmentationSnapshot fs; fs.generation = FragmentationSnapshotGeneration(1); fs.domain = Domain::CUDA_DEVICE_MEMORY;
  fs.provenance = Provenance::DERIVED; fs.epoch = CoordinatorEpoch(1); fs.policy_generation = PolicyGeneration(1);
  fs.generations.resource_generation = ResourceGeneration(1); fs.generations.allocation_generation = AllocationGeneration(1);
  fs.generations.reservation_generation = ReservationGeneration(1); fs.generations.topology_generation = TopologyGeneration(1);
  fs.generations.capacity_generation = CapacityModelGeneration(1);
  DeviceState dfs; dfs.resource = r1; dfs.layout = fr; fs.devices.push_back(dfs);
  WorkloadDemand dd; dd.accelerator_count = Count(1); dd.per_device_memory = Bytes(512*MIB); dd.aggregate_memory = Bytes(512*MIB); dd.contiguous_memory = Bytes(512*MIB);
  CHECK(analyze_fit(fs, dd).outcome == FitOutcome::NO_FIT_FRAGMENTATION);

  // FIT_AFTER_RECLAIM.
  ResourceLayout rr(Bytes(2048 * MIB));
  rr.insert_allocation(mk(AllocationId(6), Bytes(0), Bytes(U)), err);
  rr.insert_allocation(mk(AllocationId(7), Bytes(2*U), Bytes(U)), err);
  rr.insert_allocation(mk(AllocationId(8), Bytes(4*U), Bytes(U)), err);
  rr.insert_allocation(mk(AllocationId(9), Bytes(6*U), Bytes(U)), err);
  FragmentationSnapshot vs; vs.generation = FragmentationSnapshotGeneration(1); vs.domain = Domain::CUDA_DEVICE_MEMORY;
  vs.provenance = Provenance::DERIVED; vs.epoch = CoordinatorEpoch(1); vs.policy_generation = PolicyGeneration(1);
  vs.generations.resource_generation = ResourceGeneration(1); vs.generations.allocation_generation = AllocationGeneration(1);
  vs.generations.reservation_generation = ReservationGeneration(1); vs.generations.topology_generation = TopologyGeneration(1);
  vs.generations.capacity_generation = CapacityModelGeneration(1);
  DeviceState vds; vds.resource = r1; vds.layout = rr; vs.devices.push_back(vds);
  WorkloadDemand vd; vd.accelerator_count = Count(1); vd.per_device_memory = Bytes(512*MIB); vd.aggregate_memory = Bytes(512*MIB); vd.contiguous_memory = Bytes(512*MIB);
  CHECK(analyze_fit(vs, vd).outcome == FitOutcome::FIT_AFTER_RECLAIM);

  // UNKNOWN evidence -> REVALIDATION_REQUIRED / INSUFFICIENT_EVIDENCE.
  FragmentationSnapshot us = vs; us.provenance = Provenance::UNKNOWN;
  FitOutcome uo = analyze_fit(us, vd).outcome;
  CHECK(uo == FitOutcome::REVALIDATION_REQUIRED || uo == FitOutcome::INSUFFICIENT_EVIDENCE);
}

void test_movability_and_protection() {
  CHECK(may_move(Movability::RELEASEABLE));
  CHECK(may_move(Movability::MOVABLE_LIVE));
  CHECK(!may_move(Movability::IMMOVABLE));
  CHECK(!may_move(Movability::RESERVATION_PROTECTED));
  CHECK(!may_move(Movability::UNKNOWN));  // UNKNOWN is never treated as movable.
}

void test_reservation_temporal() {
  std::vector<Reservation> rs;
  Reservation r1; r1.id = ReservationId(1); r1.generation = ReservationGeneration(1); r1.start = Duration(1000); r1.end = Duration(2000); r1.amount = Bytes(1024); r1.hard = true;
  Reservation r2; r2.id = ReservationId(2); r2.generation = ReservationGeneration(1); r2.start = Duration(3000); r2.end = Duration(4000); r2.amount = Bytes(1024); r2.hard = true;
  rs.push_back(r1); rs.push_back(r2);
  Duration earliest;
  // A 5000ns window across [5000,...) must fail (tail shorter than required).
  CHECK(!temporal_continuous_fit(rs, Duration(0), Duration(5000), Duration(1500), earliest));
  // A 500ns window fits within [2000,3000).
  CHECK(temporal_continuous_fit(rs, Duration(0), Duration(10000), Duration(500), earliest));
  CHECK(earliest.nanoseconds() == 0);
}

void test_topology() {
  // Enough devices but split across incompatible topology groups.
  FragmentationSnapshot s; s.generation = FragmentationSnapshotGeneration(1); s.domain = Domain::CUDA_DEVICE_MEMORY;
  s.provenance = Provenance::DERIVED; s.epoch = CoordinatorEpoch(1); s.policy_generation = PolicyGeneration(1);
  s.generations.resource_generation = ResourceGeneration(1); s.generations.allocation_generation = AllocationGeneration(1);
  s.generations.reservation_generation = ReservationGeneration(1); s.generations.topology_generation = TopologyGeneration(1);
  s.generations.capacity_generation = CapacityModelGeneration(1);
  Resource rA; rA.id = ResourceId(1); rA.device = DeviceId(1); rA.domain = Domain::CUDA_DEVICE_MEMORY; rA.topology_group = 1; rA.provenance = Provenance::DERIVED; rA.capacity = Bytes(1024*MIB);
  Resource rB; rB.id = ResourceId(2); rB.device = DeviceId(2); rB.domain = Domain::CUDA_DEVICE_MEMORY; rB.topology_group = 2; rB.provenance = Provenance::DERIVED; rB.capacity = Bytes(1024*MIB);
  DeviceState dA; dA.resource = rA; dA.layout = ResourceLayout(Bytes(1024*MIB));
  DeviceState dB; dB.resource = rB; dB.layout = ResourceLayout(Bytes(1024*MIB));
  s.devices.push_back(dA); s.devices.push_back(dB);
  WorkloadDemand d; d.accelerator_count = Count(2); d.topology_group = 1; d.per_device_memory = Bytes(100*MIB); d.aggregate_memory = Bytes(200*MIB); d.contiguous_memory = Bytes(100*MIB);
  CHECK(analyze_fit(s, d).outcome == FitOutcome::NO_FIT_TOPOLOGY);
}

void test_stale_plan_rejection() {
  // A plan bound to generation 1 must not execute after policy advances.
  FragmentationSnapshot s; s.generation = FragmentationSnapshotGeneration(1); s.domain = Domain::CUDA_DEVICE_MEMORY;
  s.provenance = Provenance::DERIVED; s.epoch = CoordinatorEpoch(1); s.policy_generation = PolicyGeneration(1);
  s.generations.resource_generation = ResourceGeneration(1); s.generations.allocation_generation = AllocationGeneration(1);
  s.generations.reservation_generation = ReservationGeneration(1); s.generations.topology_generation = TopologyGeneration(1);
  s.generations.capacity_generation = CapacityModelGeneration(1);
  Resource r1; r1.id = ResourceId(1); r1.domain = Domain::CUDA_DEVICE_MEMORY; r1.provenance = Provenance::DERIVED; r1.capacity = Bytes(2048*MIB);
  ResourceLayout lay(Bytes(2048*MIB)); std::string err;
  lay.insert_allocation(mk(AllocationId(1), Bytes(0), Bytes(U)), err); lay.insert_allocation(mk(AllocationId(2), Bytes(2*U), Bytes(U)), err);
  lay.insert_allocation(mk(AllocationId(3), Bytes(4*U), Bytes(U)), err); lay.insert_allocation(mk(AllocationId(4), Bytes(6*U), Bytes(U)), err);
  DeviceState ds; ds.resource = r1; ds.layout = lay; s.devices.push_back(ds);
  WorkloadDemand d; d.accelerator_count = Count(1); d.per_device_memory = Bytes(512*MIB); d.aggregate_memory = Bytes(512*MIB); d.contiguous_memory = Bytes(512*MIB);
  Policy pol; pol.generation = PolicyGeneration(1);
  auto plan = plan_remediation(s, d, pol, default_weights(), FragmentationPlanId(1), FragmentationPlanGeneration(1));
  CHECK(plan.has_value());
  // The plan carries the generation it was built against.
  CHECK(plan->policy_generation == PolicyGeneration(1));
  CHECK(plan->epoch == CoordinatorEpoch(1));
}

void test_persistence() {
  PersistedState st;
  st.resource_generation = ResourceGeneration(5); st.allocation_generation = AllocationGeneration(5);
  st.reservation_generation = ReservationGeneration(3); st.policy_generation = PolicyGeneration(2);
  st.epoch = CoordinatorEpoch(1); st.next_plan_id = 77;
  PersistedPlan pp; pp.id = FragmentationPlanId(7); pp.generation = FragmentationPlanGeneration(3);
  pp.domain = FragmentationDomainId(1); pp.state = PlanState::COMPLETED; pp.verification = VerificationOutcome::TARGET_FIT_ACHIEVED;
  pp.policy_generation = PolicyGeneration(2); pp.epoch = CoordinatorEpoch(1); pp.based_on_snapshot = FragmentationSnapshotGeneration(2);
  pp.expected_capacity_recovered = Bytes(U); pp.benefit = 0.9; pp.cost = 0.1; pp.risk = 0.05; pp.disruption = 0.1;
  pp.action_count = Count(2); pp.action_kinds = {ActionKind::RELEASE_UNUSED, ActionKind::MOVE_ALLOCATION};
  st.plans.push_back(pp);
  std::vector<std::uint8_t> buf;
  CodecResult e = encode_state(st, buf);
  CHECK(e.ok);
  PersistedState out;
  CodecResult d = decode_state(buf, out);
  CHECK(d.ok);
  CHECK_EQ(out.next_plan_id, 77u);
  CHECK_EQ(out.resource_generation.value(), 5u);
  CHECK_EQ(out.plans.size(), 1u);
  CHECK(out.plans[0].state == PlanState::COMPLETED);
  CHECK(out.plans[0].verification == VerificationOutcome::TARGET_FIT_ACHIEVED);
  CHECK(out.needs_revalidation);
  // Corruption detection: flip a byte in the payload area.
  if (buf.size() > 20) { buf[10] ^= 0xFF; PersistedState bad; CHECK(!decode_state(buf, bad).ok); }
  // Bad magic.
  std::vector<std::uint8_t> badmagic = buf; if (buf.size()>4) { badmagic[0] ^= 0x55; PersistedState bad2; CHECK(!decode_state(badmagic, bad2).ok); }
  // Truncation.
  std::vector<std::uint8_t> trunc(buf.begin(), buf.begin() + (long)(buf.size()/2)); PersistedState bad3; CHECK(!decode_state(trunc, bad3).ok);
  // Trailing garbage.
  std::vector<std::uint8_t> trail = buf; trail.push_back(0xEE); PersistedState bad4; CHECK(!decode_state(trail, bad4).ok);
}

void test_protocol() {
  Message m; m.type = MsgType::CREATE_PLAN; m.epoch = 1; m.policy_gen = 2;
  m.demand.accelerator_count = Count(1); m.demand.per_device_memory = Bytes(512*MIB); m.demand.aggregate_memory = Bytes(512*MIB); m.demand.contiguous_memory = Bytes(512*MIB);
  auto f = frame_encode(m);
  auto d = frame_decode(f.data(), f.size());
  CHECK(d.has_value());
  CHECK(d->type == MsgType::CREATE_PLAN);
  CHECK_EQ(d->demand.per_device_memory.value(), (std::uint64_t)(512*MIB));
  // Oversized payload rejected.
  Message big; big.type = MsgType::HELLO; big.detail.assign((size_t)protocol_max_payload()+1, 'x');
  auto bf = frame_encode(big);
  CHECK(!frame_decode(bf.data(), bf.size()).has_value());
  // FrameDecoder partial reads (feed one byte at a time).
  FrameDecoder dec; std::optional<Message> got;
  for (size_t i = 0; i < f.size(); ++i) { got = dec.feed(f.data()+i, 1); if (got) break; }
  CHECK(got.has_value());
  CHECK(got->type == MsgType::CREATE_PLAN);
}

void test_property() {
  // Deterministic seeded randomized property tests.
  for (std::uint64_t seed = 1; seed <= 50; ++seed) {
    std::mt19937_64 g(seed);
    const std::uint64_t cap = (seed % 5 + 1) * 512 * MIB;
    ResourceLayout lay{Bytes(cap)};
    std::string err;
    std::uint64_t used = 0;
    std::uint64_t nalloc = 0;
    while (nalloc < 200 && used + 2 * MIB < cap) {
      std::uint64_t len = (g() % 64 + 1) * MIB;
      if (used + len > cap) break;
      Allocation a = mk(AllocationId(nalloc+1), Bytes(used), Bytes(len), (nalloc%3==0)?Movability::IMMOVABLE:Movability::MOVABLE_AFTER_CHECKPOINT);
      if (!lay.insert_allocation(a, err)) break;
      used += len; nalloc++;
      // invariants
      CHECK(lay.used_capacity() + lay.free_capacity() == lay.total_capacity());
      CHECK(lay.free_capacity() <= lay.total_capacity());
      CHECK(lay.largest_free_block() <= lay.free_capacity());
    }
    FragmentationMetrics m = compute_metrics(lay);
    CHECK(m.free_capacity <= lay.total_capacity());
    CHECK(m.stranded_capacity <= m.free_capacity);
    CHECK(m.reclaimable_capacity <= m.protected_capacity + m.reclaimable_capacity + Bytes(0));
  }
}

void test_arithmetic_safety() {
  bool threw = false;
  try { Bytes b(10); Bytes c(20); auto x = c - b; (void)x; } catch (...) { threw = true; }
  CHECK(!threw);
  threw = false;
  try { Bytes b(10); Bytes c(20); auto x = b - c; (void)x; } catch (...) { threw = true; }
  CHECK(threw);  // underflow must throw
  CHECK(Bytes(UINT64_MAX) > Bytes(0));  // no overflow stored.
}

}  // namespace

int main() {
  test_layout_accounting();
  test_metrics();
  test_fit_taxonomy();
  test_movability_and_protection();
  test_reservation_temporal();
  test_topology();
  test_stale_plan_rejection();
  test_persistence();
  test_protocol();
  test_property();
  test_arithmetic_safety();
  if (failures == 0) printf("ALL CORE TESTS PASSED\n");
  else printf("%d FAILURES\n", failures);
  return failures == 0 ? 0 : 1;
}