#include "fragmentation_governor/coordinator/coordinator.hpp"

#include <algorithm>

#include "fragmentation_governor/fit/fit.hpp"
#include "fragmentation_governor/persistence/codec.hpp"
#include "fragmentation_governor/policy/policy.hpp"
#include "fragmentation_governor/planner/planner.hpp"
#include "fragmentation_governor/verification/verification.hpp"

namespace fragmentation_governor {

Coordinator::Coordinator(CoordinatorEpoch epoch) : epoch_(epoch), snapshot_generation_(FragmentationSnapshotGeneration(1)) {
  generations_.resource_generation = ResourceGeneration(1);
  generations_.allocation_generation = AllocationGeneration(1);
  generations_.reservation_generation = ReservationGeneration(1);
  generations_.topology_generation = TopologyGeneration(1);
  generations_.capacity_generation = CapacityModelGeneration(1);
  generations_.capability_generation = CapabilityGeneration(1);
  generations_.placement_generation = PlacementGeneration(1);
  generations_.health_generation = HealthGeneration(1);
  generations_.policy_generation = PolicyGeneration(1);
  policy_generation_ = PolicyGeneration(1);
}

Message Coordinator::fail(std::string why) const {
  Message m; m.type = MsgType::ERROR; m.ok = false; m.error = std::move(why); return m;
}

void Coordinator::advance_resource_generation() {
  generations_.resource_generation = generations_.resource_generation.next();
  generations_.allocation_generation = generations_.allocation_generation.next();
}

void Coordinator::mark_worker_dead(WorkerId worker) {
  if (worker_alive_.count(worker)) worker_alive_[worker] = false;
  for (auto& [rid, rec] : resources_) {
    (void)rid;
    if (rec.worker == worker) rec.alive = false;
  }
  // Any plan whose target resources were owned by this worker must be revalidated.
  for (auto& [pid, plan] : plans_) {
    for (const auto& act : plan.actions) {
      auto it = resources_.find(act.target_resource);
      if (it != resources_.end() && it->second.worker == worker) {
        plan.state = PlanState::REVALIDATION_REQUIRED;
        plan.precondition_failures.push_back("evidence source died");
        break;
      }
    }
  }
  // Reject stale dynamic evidence by not treating dead-worker resources as current.
}

bool Coordinator::can_execute(const RemediationPlan& p, const Message& in) const {
  if (p.generation.value() != in.plan_gen) return false;
  if (p.policy_generation != policy_generation_) return false;
  if (p.epoch != epoch_) return false;
  if (p.state == PlanState::SUPERSEDED || p.state == PlanState::CANCELLED ||
      p.state == PlanState::ABORTED || p.state == PlanState::RETIRED ||
      p.state == PlanState::REVALIDATION_REQUIRED || p.state == PlanState::BLOCKED) return false;
  return true;
}

FragmentationSnapshot Coordinator::assemble_snapshot() const {
  FragmentationSnapshot s;
  s.id = FragmentationSnapshotId(1);
  s.generation = snapshot_generation_;
  s.domain_id = FragmentationDomainId(1);
  s.domain_generation = FragmentationDomainGeneration(1);
  s.pool = ResourcePoolId(1);
  s.pool_generation = ResourcePoolGeneration(1);
  s.domain = Domain::COMPOSITE_RESOURCE;
  s.generations = generations_;
  s.source_worker = WorkerBootId(1);
  s.epoch = epoch_;
  s.policy_generation = policy_generation_;
  s.capacity_generation = generations_.capacity_generation;
  s.topology_generation = generations_.topology_generation;
  s.evidence_time = std::chrono::system_clock::now();
  s.provenance = Provenance::MEASURED;
  bool saw_synthetic = false;
  for (const auto& [rid, rec] : resources_) {
    (void)rid;
    if (!rec.alive) continue;
    DeviceState ds;
    ds.resource = rec.resource;
    ds.layout = rec.layout;
    s.devices.push_back(ds);
    if (rec.provenance == Provenance::SYNTHETIC) saw_synthetic = true;
  }
  if (saw_synthetic) s.provenance = Provenance::SYNTHETIC;
  return s;
}

void Coordinator::recover(const PersistedState& state) {
  persisted_ = state;
  generations_.resource_generation = state.resource_generation;
  generations_.allocation_generation = state.allocation_generation;
  generations_.reservation_generation = state.reservation_generation;
  generations_.policy_generation = state.policy_generation;
  policy_generation_ = state.policy_generation;
  next_plan_id_ = state.next_plan_id;
  // Restore durable plans (dynamic-dependent ones degrade to revalidation).
  for (const auto& pp : state.plans) {
    RemediationPlan p;
    p.id = pp.id; p.generation = pp.generation; p.domain_id = pp.domain;
    p.policy_generation = pp.policy_generation; p.epoch = pp.epoch;
    p.based_on_snapshot = pp.based_on_snapshot;
    p.expected_capacity_recovered = pp.expected_capacity_recovered;
    p.benefit = HealthyScore(pp.benefit); p.cost = HealthyScore(pp.cost);
    p.risk = HealthyScore(pp.risk); p.disruption = HealthyScore(pp.disruption);
    p.state = pp.state;
    p.verification = pp.verification;
    for (const auto& k : pp.action_kinds) { PlanAction a; a.kind = k; p.actions.push_back(a); }
    if (p.state == PlanState::EXECUTING || p.state == PlanState::APPROVED ||
        p.state == PlanState::MOVING || p.state == PlanState::QUIESCING ||
        p.state == PlanState::PRESERVING_STATE || p.state == PlanState::REBINDING ||
        p.state == PlanState::VERIFYING) {
      p.state = PlanState::REVALIDATION_REQUIRED;
      p.precondition_failures.push_back("dynamic evidence not authoritative after restart");
    }
    plans_[p.id] = p;
  }
  // Dynamic evidence is NOT restored as current.
  resources_.clear();
  worker_alive_.clear();
  worker_boot_.clear();
}

std::vector<Message> Coordinator::handle(const Message& in) {
  switch (in.type) {
    case MsgType::HELLO:
      return {};
    case MsgType::REGISTER: {
      if (in.worker_boot == 0) return {fail("REGISTER requires fresh WorkerBootId")};
      // Fresh incarnation detection: a different boot id on a known worker means
      // the previous incarnation's evidence is stale.
      auto it = worker_boot_.find(WorkerId(in.worker_id));
      if (it != worker_boot_.end() && it->second.value() != in.worker_boot) {
        mark_worker_dead(WorkerId(in.worker_id));
      }
      worker_boot_[WorkerId(in.worker_id)] = WorkerBootId(in.worker_boot);
      worker_alive_[WorkerId(in.worker_id)] = true;
      Message a; a.type = MsgType::ERROR; a.ok = true; return {a};
    }
    case MsgType::PUBLISH_RESOURCE: {
      auto alive = worker_alive_.find(WorkerId(in.worker_id));
      if (alive == worker_alive_.end() || !alive->second) return {fail("unregistered/dead worker")};
      if (worker_boot_[WorkerId(in.worker_id)].value() != in.worker_boot) return {fail("stale WorkerBootId")};
      Resource res = in.resource;
      res.device = res.device.is_null() ? DeviceId(WorkerId(in.worker_id).value()) : res.device;
      if (resources_.count(res.id)) {
        if (resources_[res.id].resource.generation.value() >= res.generation.value()) {
          return {fail("stale ResourceGeneration")};
        }
      }
      WorkerResourceRecord rec;
      rec.resource = res;
      rec.layout = ResourceLayout(res.capacity);
      rec.worker = WorkerId(in.worker_id);
      rec.boot = WorkerBootId(in.worker_boot);
      rec.alive = true;
      rec.provenance = res.provenance;
      resources_[res.id] = rec;
      advance_resource_generation();
      Message a; a.type = MsgType::ERROR; a.ok = true; return {a};
    }
    case MsgType::PUBLISH_LAYOUT: {
      auto alive = worker_alive_.find(WorkerId(in.worker_id));
      if (alive == worker_alive_.end() || !alive->second) return {fail("unregistered/dead worker")};
      if (worker_boot_[WorkerId(in.worker_id)].value() != in.worker_boot) return {fail("stale WorkerBootId")};
      if (resources_.count(in.resource.id) == 0) return {fail("unknown resource")};
      if (resources_[in.resource.id].worker != WorkerId(in.worker_id)) return {fail("cross-worker resource claim")};
      // A stale resource generation cannot publish current layout evidence.
      if (in.resource_gen < resources_[in.resource.id].resource.generation.value()) {
        return {fail("stale ResourceGeneration")};
      }
      ResourceLayout layout(resources_[in.resource.id].resource.capacity);
      std::string err;
      for (const auto& a : in.allocations) {
        if (!layout.insert_allocation(a, err)) return {fail("layout insert rejected: " + err)};
      }
      resources_[in.resource.id].layout = layout;
      advance_resource_generation();
      Message a; a.type = MsgType::ERROR; a.ok = true; return {a};
    }
    case MsgType::PUBLISH_ALLOCATION: {
      auto alive = worker_alive_.find(WorkerId(in.worker_id));
      if (alive == worker_alive_.end() || !alive->second) return {fail("unregistered/dead worker")};
      if (worker_boot_[WorkerId(in.worker_id)].value() != in.worker_boot) return {fail("stale WorkerBootId")};
      if (resources_.count(in.resource.id) == 0) return {fail("unknown resource")};
      std::string err;
      if (!resources_[in.resource.id].layout.insert_allocation(in.allocation, err)) {
        return {fail("allocation rejected: " + err)};
      }
      advance_resource_generation();
      Message a; a.type = MsgType::ERROR; a.ok = true; return {a};
    }
    case MsgType::INVALIDATE_ALLOCATION: {
      auto alive = worker_alive_.find(WorkerId(in.worker_id));
      if (alive == worker_alive_.end() || !alive->second) return {fail("unregistered/dead worker")};
      if (worker_boot_[WorkerId(in.worker_id)].value() != in.worker_boot) return {fail("stale WorkerBootId")};
      bool removed = false;
      for (auto& [rid, rec] : resources_) {
        if (rec.worker != WorkerId(in.worker_id)) continue;
        std::string err;
        if (rec.layout.remove_allocation(AllocationId(in.allocation_id), err)) { removed = true; break; }
      }
      if (!removed) return {fail("allocation not found")};
      advance_resource_generation();
      Message a; a.type = MsgType::ERROR; a.ok = true; return {a};
    }
    case MsgType::QUERY_FRAGMENTATION: {
      FragmentationSnapshot s = assemble_snapshot();
      Message m; m.type = MsgType::QUERY_FRAGMENTATION; m.ok = true;
      std::string detail;
      for (size_t i = 0; i < s.devices.size(); ++i) {
        FragmentationMetrics met = compute_metrics(s.devices[i].layout);
        detail += "device[" + std::to_string(i) + "] free=" + std::to_string(met.free_capacity.value()) +
                  " largest=" + std::to_string(met.largest_free_block.value()) +
                  " stranded=" + std::to_string(met.stranded_capacity.value()) + "; ";
      }
      m.detail = detail;
      return {m};
    }
    case MsgType::QUERY_FIT: {
      FragmentationSnapshot s = assemble_snapshot();
      FitResult r = analyze_fit(s, in.demand);
      Message m; m.type = MsgType::QUERY_FIT; m.ok = true;
      m.fit_outcome = r.outcome;
      m.detail = r.block_diagnostics.empty() ? std::string() : r.block_diagnostics[0];
      return {m};
    }
    case MsgType::CREATE_PLAN: {
      FragmentationSnapshot s = assemble_snapshot();
      last_demand_ = in.demand;
      Policy policy; policy.generation = policy_generation_;
      auto plan = plan_remediation(s, in.demand, policy, default_weights(),
                                   FragmentationPlanId(next_plan_id_),
                                   FragmentationPlanGeneration(next_plan_id_));
      if (!plan.has_value()) {
        Message m; m.type = MsgType::ERROR; m.ok = false; m.detail = "no beneficial action";
        return {m};
      }
      plan->state = PlanState::PLAN_READY;
      plans_[plan->id] = *plan;
      before_snapshots_[plan->id] = s;
      next_plan_id_ = plan->id.value() + 1;
      Message m; m.type = MsgType::PLAN_RESULT; m.ok = true; m.plan = *plan;
      return {m};
    }
    case MsgType::APPROVE_PLAN: {
      auto it = plans_.find(FragmentationPlanId(in.plan_id));
      if (it == plans_.end()) return {fail("unknown plan")};
      if (it->second.state == PlanState::SUPERSEDED) return {fail("plan superseded")};
      if (it->second.policy_generation != policy_generation_) return {fail("stale PolicyGeneration")};
      it->second.state = PlanState::APPROVED;
      Message a; a.type = MsgType::ERROR; a.ok = true; return {a};
    }
    case MsgType::BEGIN_ACTION: {
      auto it = plans_.find(FragmentationPlanId(in.plan_id));
      if (it == plans_.end()) return {fail("unknown plan")};
      if (it->second.state != PlanState::APPROVED) return {fail("plan not approved")};
      if (!can_execute(it->second, in)) return {fail("execution authority stale")};
      if (in.action_index >= it->second.actions.size()) return {fail("bad action index")};
      it->second.state = PlanState::EXECUTING;
      Message a; a.type = MsgType::ERROR; a.ok = true; return {a};
    }
    case MsgType::ACTION_COMPLETE: {
      auto it = plans_.find(FragmentationPlanId(in.plan_id));
      if (it == plans_.end()) return {fail("unknown plan")};
      if (it->second.state != PlanState::EXECUTING) return {fail("plan not executing")};
      if (in.action_index >= it->second.actions.size()) return {fail("bad action index")};
      if (in.action_ok) {
        const PlanAction& act = it->second.actions[in.action_index];
        if (act.source_allocation.has_value()) {
          auto rit = resources_.find(act.target_resource);
          if (rit != resources_.end()) {
            std::string err;
            rit->second.layout.remove_allocation(*act.source_allocation, err);
            advance_resource_generation();
          }
        }
      }
      it->second.state = PlanState::VERIFYING;
      Message a; a.type = MsgType::ERROR; a.ok = true; return {a};
    }
    case MsgType::VERIFY: {
      auto it = plans_.find(FragmentationPlanId(in.plan_id));
      if (it == plans_.end()) return {fail("unknown plan")};
      if (it->second.state != PlanState::VERIFYING) return {fail("plan not ready to verify")};
      // Demand is captured in before_snapshot evaluation only where known.
      auto bit = before_snapshots_.find(it->first);
      if (bit == before_snapshots_.end()) return {fail("no before snapshot")};
      // Recover the demand used to create the plan from the coordinator's last
      // CREATE_PLAN demand record stored in the plan? We kept it separately.
      FragmentationSnapshot after = assemble_snapshot();
      // Recompute fit-based verification using a minimal demand placeholder.
      // The verification uses the before/after comparison for stranded + fit.
      WorkloadDemand dem = last_demand_;
      VerificationResult v = verify_remediation(bit->second, after, dem, it->second);
      it->second.verification = v.outcome;
      if (v.outcome == VerificationOutcome::TARGET_FIT_ACHIEVED ||
          v.outcome == VerificationOutcome::IMPROVED ||
          v.outcome == VerificationOutcome::PARTIAL_IMPROVEMENT) {
        it->second.state = PlanState::COMPLETED;
      } else if (v.outcome == VerificationOutcome::NO_CHANGE) {
        it->second.state = PlanState::PARTIALLY_COMPLETED;
      } else if (v.outcome == VerificationOutcome::REGRESSION ||
                 v.outcome == VerificationOutcome::VERIFICATION_FAILED) {
        it->second.state = PlanState::FAILED;
      } else {
        it->second.state = PlanState::REVALIDATION_REQUIRED;
      }
      Message m; m.type = MsgType::VERIFY; m.ok = true;
      m.verification_outcome = v.outcome;
      m.detail = v.diagnostics.empty() ? std::string() : v.diagnostics[0];
      return {m};
    }
    case MsgType::CANCEL_PLAN: {
      auto it = plans_.find(FragmentationPlanId(in.plan_id));
      if (it == plans_.end()) return {fail("unknown plan")};
      it->second.state = PlanState::CANCELLED;
      Message a; a.type = MsgType::ERROR; a.ok = true; return {a};
    }
    case MsgType::ADVANCE_GENERATION: {
      switch (in.generation_kind) {
        case 0: generations_.resource_generation = generations_.resource_generation.next(); break;
        case 1: generations_.allocation_generation = generations_.allocation_generation.next(); break;
        case 2: generations_.policy_generation = generations_.policy_generation.next();
                policy_generation_ = generations_.policy_generation; break;
        default: return {fail("bad generation kind")};
      }
      Message a; a.type = MsgType::ERROR; a.ok = true; return {a};
    }
    case MsgType::REVALIDATE: {
      auto it = plans_.find(FragmentationPlanId(in.plan_id));
      if (it == plans_.end()) return {fail("unknown plan")};
      it->second.state = PlanState::REVALIDATION_REQUIRED;
      Message a; a.type = MsgType::ERROR; a.ok = true; return {a};
    }
    case MsgType::SAVE: {
      // Persist durable governance state to the requested path (or default).
      PersistedState st;
      st.resource_generation = generations_.resource_generation;
      st.allocation_generation = generations_.allocation_generation;
      st.reservation_generation = generations_.reservation_generation;
      st.policy_generation = policy_generation_;
      st.epoch = epoch_;
      st.next_plan_id = next_plan_id_;
      st.carried_live_dynamic_state = true;
      for (const auto& [pid, plan] : plans_) {
        PersistedPlan pp;
        pp.id = plan.id; pp.generation = plan.generation; pp.domain = plan.domain_id;
        pp.state = plan.state; pp.verification = plan.verification;
        pp.policy_generation = plan.policy_generation; pp.epoch = plan.epoch;
        pp.based_on_snapshot = plan.based_on_snapshot;
        pp.expected_capacity_recovered = plan.expected_capacity_recovered;
        pp.benefit = plan.benefit.value(); pp.cost = plan.cost.value();
        pp.risk = plan.risk.value(); pp.disruption = plan.disruption.value();
        pp.action_count = Count(plan.actions.size());
        for (const auto& act : plan.actions) pp.action_kinds.push_back(act.kind);
        st.plans.push_back(pp);
      }
      for (auto& res : resources_) {
        for (const auto& [aid, alloc] : res.second.layout.allocations()) {
          if (alloc.reservation.has_value()) {
            Reservation rv; rv.id = *alloc.reservation; rv.resource = res.second.resource.id;
            rv.generation = generations_.reservation_generation; rv.start = Duration(0);
            rv.end = Duration(1); rv.amount = alloc.length; rv.hard = true;
            st.reservations.push_back(rv);
          }
        }
      }
      std::string path = in.detail.empty() ? std::string("fg_state.bin") : in.detail;
      CodecResult r = save_state(st, path);
      Message a; a.type = MsgType::ERROR; a.ok = r.ok; a.error = r.error;
      if (!r.ok) return {a};
      a.error.clear();
      return {a};
    }
    case MsgType::SHUTDOWN:
    case MsgType::ERROR:
      return {};
  }
  return {fail("unhandled")};
}

}  // namespace fragmentation_governor