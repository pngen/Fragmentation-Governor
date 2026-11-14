// Fragmentation Governor regression tests: an older WorkerBootId cannot register
// over a newer one; a worker cannot take over or write into another worker's
// live resource; approval does not revive cancelled or revalidation-required
// plans; VERIFY judges a plan against its own demand; malformed payloads and
// out-of-range persisted scores are rejected rather than thrown; the transport
// fences only the incarnation whose connection closed.
// Copyright 2026 Summon Software Labs.  Apache License 2.0.
#include <bit>
#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>
#include <thread>
#include <vector>

#include "fragmentation_governor/fragmentation_governor.hpp"

using namespace fragmentation_governor;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("REGRESSION FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static const std::size_t MIB = 1024 * 1024;

namespace {

bool accepted(const std::vector<Message>& r) { return !r.empty() && r.front().ok; }

Message reg(std::uint64_t worker, std::uint64_t boot) {
  Message m; m.type = MsgType::REGISTER; m.worker_id = worker; m.worker_boot = boot; m.epoch = 1;
  return m;
}

Message publishResource(std::uint64_t worker, std::uint64_t boot, std::uint64_t id, std::uint64_t gen) {
  Resource res; res.id = ResourceId(id); res.generation = ResourceGeneration(gen);
  res.pool = ResourcePoolId(1); res.device = DeviceId(id); res.node = NodeId(1);
  res.domain = Domain::CUDA_DEVICE_MEMORY; res.capacity = Bytes(2048 * MIB); res.provenance = Provenance::DERIVED;
  Message m; m.type = MsgType::PUBLISH_RESOURCE; m.worker_id = worker; m.worker_boot = boot; m.epoch = 1; m.resource = res;
  return m;
}

Allocation releasable(std::uint64_t id, std::size_t offset_mib) {
  return Allocation{AllocationId(id), AllocationGeneration(1), AllocationOwnerId(9), Bytes(offset_mib * MIB),
                    Bytes(256 * MIB), Movability::RELEASEABLE, std::nullopt, false, std::nullopt, Provenance::DERIVED};
}

// Worker 1 (boot 1) owns resource 1: 2048 MiB with releasable 256 MiB blocks at
// 0, 512, 1024 and 1536 MiB, so the largest free block is 256 MiB.
void populate(Coordinator& c) {
  CHECK(accepted(c.handle(reg(1, 1))));
  CHECK(accepted(c.handle(publishResource(1, 1, 1, 1))));
  Message lay; lay.type = MsgType::PUBLISH_LAYOUT; lay.worker_id = 1; lay.worker_boot = 1; lay.epoch = 1;
  lay.resource.id = ResourceId(1); lay.resource_gen = 1;
  lay.allocations = {releasable(201, 0), releasable(202, 512), releasable(203, 1024), releasable(204, 1536)};
  CHECK(accepted(c.handle(lay)));
}

WorkloadDemand contiguous(std::uint64_t id, std::size_t mib) {
  WorkloadDemand d; d.id = WorkloadDemandId(id); d.generation = WorkloadDemandGeneration(1);
  d.accelerator_count = Count(1); d.per_device_memory = Bytes(mib * MIB);
  d.aggregate_memory = Bytes(mib * MIB); d.contiguous_memory = Bytes(mib * MIB);
  return d;
}

std::optional<RemediationPlan> createPlan(Coordinator& c, const WorkloadDemand& d) {
  Message m; m.type = MsgType::CREATE_PLAN; m.epoch = 1; m.policy_gen = 1; m.demand = d;
  auto r = c.handle(m);
  if (r.empty() || r.front().type != MsgType::PLAN_RESULT || !r.front().ok) return std::nullopt;
  return r.front().plan;
}

Message planMsg(MsgType t, const RemediationPlan& p) {
  Message m; m.type = t; m.plan_id = p.id.value(); m.plan_gen = p.generation.value();
  m.epoch = 1; m.policy_gen = 1; m.action_index = 0;
  return m;
}

}  // namespace

static void test_older_boot_cannot_register() {
  Coordinator c(CoordinatorEpoch(1));
  CHECK(accepted(c.handle(reg(1, 2))));
  CHECK(accepted(c.handle(publishResource(1, 2, 1, 1))));
  CHECK(!accepted(c.handle(reg(1, 1))));                    // replay of an older incarnation
  CHECK(c.resources().at(ResourceId(1)).alive);
  CHECK(!accepted(c.handle(publishResource(1, 1, 1, 2))));  // and it cannot publish
}

static void test_worker_cannot_claim_anothers_resource() {
  Coordinator c(CoordinatorEpoch(1));
  populate(c);
  CHECK(accepted(c.handle(reg(2, 1))));
  CHECK(!accepted(c.handle(publishResource(2, 1, 1, 2))));  // take over worker 1's live resource
  Message al; al.type = MsgType::PUBLISH_ALLOCATION; al.worker_id = 2; al.worker_boot = 1; al.epoch = 1;
  al.resource.id = ResourceId(1); al.allocation = releasable(301, 256);
  CHECK(!accepted(c.handle(al)));                           // write into worker 1's layout
  const auto& rec = c.resources().at(ResourceId(1));
  CHECK(rec.worker == WorkerId(1));
  CHECK(rec.layout.allocation_count().value() == 4);
}

static void test_approval_does_not_revive_plans() {
  Coordinator c(CoordinatorEpoch(1));
  populate(c);
  auto p = createPlan(c, contiguous(1, 512));
  CHECK(p.has_value());
  if (!p) return;
  CHECK(accepted(c.handle(planMsg(MsgType::CANCEL_PLAN, *p))));
  CHECK(!accepted(c.handle(planMsg(MsgType::APPROVE_PLAN, *p))));
  CHECK(!accepted(c.handle(planMsg(MsgType::BEGIN_ACTION, *p))));
  // A plan degraded by its worker's death cannot be re-approved into execution.
  auto q = createPlan(c, contiguous(2, 512));
  CHECK(q.has_value());
  if (!q) return;
  c.mark_worker_dead(WorkerId(1));
  CHECK(c.plans().at(q->id).state == PlanState::REVALIDATION_REQUIRED);
  CHECK(!accepted(c.handle(planMsg(MsgType::APPROVE_PLAN, *q))));
  CHECK(!accepted(c.handle(planMsg(MsgType::BEGIN_ACTION, *q))));
}

static void test_verify_uses_the_plans_own_demand() {
  Coordinator c(CoordinatorEpoch(1));
  populate(c);
  // 1024 MiB contiguous needs more than one release; only the first action runs.
  auto p = createPlan(c, contiguous(1, 1024));
  CHECK(p.has_value());
  if (!p) return;
  CHECK(p->actions.size() > 1);
  CHECK(accepted(c.handle(planMsg(MsgType::APPROVE_PLAN, *p))));
  CHECK(accepted(c.handle(planMsg(MsgType::BEGIN_ACTION, *p))));
  Message done = planMsg(MsgType::ACTION_COMPLETE, *p);
  done.action_ok = true;
  CHECK(accepted(c.handle(done)));
  // A later CREATE_PLAN for a 512 MiB demand that now fits creates no plan.
  CHECK(!createPlan(c, contiguous(2, 512)).has_value());
  auto v = c.handle(planMsg(MsgType::VERIFY, *p));
  CHECK(accepted(v));
  if (!v.empty()) CHECK(v.front().verification_outcome != VerificationOutcome::TARGET_FIT_ACHIEVED);
  CHECK(c.plans().at(p->id).state != PlanState::COMPLETED);
}

static void test_malformed_payload_rejected_not_thrown() {
  Message m; m.type = MsgType::PLAN_RESULT;
  m.plan.id = FragmentationPlanId(1); m.plan.generation = FragmentationPlanGeneration(1);
  m.plan.benefit = HealthyScore(0.375);
  std::vector<std::uint8_t> bytes = encode_payload(m);
  CHECK(decode_payload(bytes.data(), bytes.size()).has_value());
  const std::uint64_t pattern = std::bit_cast<std::uint64_t>(0.375);
  const std::uint64_t nan = std::bit_cast<std::uint64_t>(std::numeric_limits<double>::quiet_NaN());
  bool patched = false;
  for (std::size_t i = 0; i + 8 <= bytes.size() && !patched; ++i) {
    std::uint64_t v = 0;
    std::memcpy(&v, bytes.data() + i, 8);   // payload integers are little-endian
    if (v == pattern) { std::memcpy(bytes.data() + i, &nan, 8); patched = true; }
  }
  CHECK(patched);
  bool threw = false;
  try { CHECK(!decode_payload(bytes.data(), bytes.size()).has_value()); } catch (...) { threw = true; }
  CHECK(!threw);
}

static void test_persisted_scores_bounded() {
  PersistedState st;
  st.resource_generation = ResourceGeneration(1); st.allocation_generation = AllocationGeneration(1);
  st.reservation_generation = ReservationGeneration(1); st.policy_generation = PolicyGeneration(1);
  st.epoch = CoordinatorEpoch(1); st.next_plan_id = 2;
  PersistedPlan pp; pp.id = FragmentationPlanId(1); pp.generation = FragmentationPlanGeneration(1);
  pp.domain = FragmentationDomainId(1); pp.state = PlanState::PLAN_READY;
  pp.policy_generation = PolicyGeneration(1); pp.epoch = CoordinatorEpoch(1);
  pp.based_on_snapshot = FragmentationSnapshotGeneration(1); pp.expected_capacity_recovered = Bytes(1024);
  pp.benefit = 2.0; pp.cost = 0.1; pp.risk = 0.1; pp.disruption = 0.1;
  st.plans.push_back(pp);
  std::vector<std::uint8_t> buf;
  CHECK(encode_state(st, buf).ok);
  PersistedState out;
  CHECK(!decode_state(buf, out).ok);   // recover() would throw on HealthyScore(2.0)
}

static void test_transport_fences_only_the_closing_incarnation() {
  // A rejected REGISTER does not bind its connection to the worker.
  {
    Coordinator c(CoordinatorEpoch(1));
    CHECK(accepted(c.handle(reg(1, 2))));
    CHECK(accepted(c.handle(publishResource(1, 2, 1, 1))));
    TcpServer server(c);
    CHECK(server.start(0));
    std::thread srv([&]() { server.serve(); });
    {
      TcpClient replay;
      CHECK(replay.connect(server.port()));
      CHECK(replay.send_frame(reg(1, 1)));
      auto r = replay.recv_frame();
      CHECK(r.has_value() && !r->ok);
      replay.close();
    }
    server.stop();   // joins every connection thread, including its close handling
    srv.join();
    CHECK(c.resources().at(ResourceId(1)).alive);
  }
  // An older incarnation's connection closing does not fence the newer one.
  {
    Coordinator c(CoordinatorEpoch(1));
    TcpServer server(c);
    CHECK(server.start(0));
    std::thread srv([&]() { server.serve(); });
    {
      TcpClient old_incarnation;
      CHECK(old_incarnation.connect(server.port()));
      CHECK(old_incarnation.send_frame(reg(1, 1)));
      auto r = old_incarnation.recv_frame();
      CHECK(r.has_value() && r->ok);
      // The connection thread is now idle in recv; register the newer incarnation.
      CHECK(accepted(c.handle(reg(1, 2))));
      CHECK(accepted(c.handle(publishResource(1, 2, 1, 1))));
      old_incarnation.close();
    }
    server.stop();
    srv.join();
    CHECK(c.resources().at(ResourceId(1)).alive);
  }
}

int main() {
  test_older_boot_cannot_register();
  test_worker_cannot_claim_anothers_resource();
  test_approval_does_not_revive_plans();
  test_verify_uses_the_plans_own_demand();
  test_malformed_payload_rejected_not_thrown();
  test_persisted_scores_bounded();
  test_transport_fences_only_the_closing_incarnation();
  printf("fg_regression_test: %d failure(s)\n", failures);
  return failures == 0 ? 0 : 1;
}
