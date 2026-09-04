#include "fragmentation_governor/planner/planner.hpp"

#include <algorithm>
#include <cmath>

namespace fragmentation_governor {

namespace {

double disruption_of(Movability m) {
  switch (m) {
    case Movability::RELEASEABLE: return 0.10;
    case Movability::RECOMPUTABLE: return 0.25;
    case Movability::EVICTABLE: return 0.40;
    case Movability::MOVABLE_AFTER_CHECKPOINT: return 0.60;
    case Movability::MOVABLE_AFTER_QUIESCE: return 0.80;
    case Movability::MOVABLE_LIVE: return 0.95;
    case Movability::IMMOVABLE:
    case Movability::RESERVATION_PROTECTED:
    case Movability::POLICY_PROTECTED:
    case Movability::UNKNOWN:
      return 1.0;
  }
  return 1.0;
}

ActionKind kind_for(Movability m) {
  switch (m) {
    case Movability::RELEASEABLE: return ActionKind::RELEASE_UNUSED;
    case Movability::RECOMPUTABLE: return ActionKind::EVICT_RECOMPUTABLE;
    case Movability::EVICTABLE: return ActionKind::EVICT_RECOMPUTABLE;
    case Movability::MOVABLE_LIVE:
    case Movability::MOVABLE_AFTER_QUIESCE:
    case Movability::MOVABLE_AFTER_CHECKPOINT:
      return ActionKind::MOVE_ALLOCATION;
    default: return ActionKind::NO_BENEFICIAL_ACTION;
  }
}

bool reclaimable(const Allocation& a) {
  if (a.policy_protected || a.reservation.has_value()) return false;
  if (!may_move(a.movability)) return false;
  return true;
}

struct FitState {
  Count satisfying;
  Bytes aggregate_free;
};

FitState eval(const std::vector<ResourceLayout>& devs, const WorkloadDemand& d) {
  FitState s;
  for (const auto& lay : devs) {
    if (lay.free_capacity() >= d.per_device_memory &&
        lay.largest_free_block() >= d.per_device_memory) {
      s.satisfying = s.satisfying + Count(1);
    }
    s.aggregate_free = s.aggregate_free + lay.free_capacity();
  }
  return s;
}

struct ReclaimCandidate {
  size_t device_index;
  AllocationId id;
  Bytes length;
  double disruption;
  Movability movability;
  AllocationOwnerId owner;
};

}  // namespace

std::optional<RemediationPlan> plan_remediation(
    const FragmentationSnapshot& snap, const WorkloadDemand& demand, const Policy& policy,
    const PlanningWeights& weights, const FragmentationPlanId& pid,
    const FragmentationPlanGeneration& pg) {
  RemediationPlan plan;
  plan.id = pid;
  plan.generation = pg;
  plan.domain_id = snap.domain_id;
  plan.based_on_snapshot = snap.generation;
  plan.policy_generation = snap.policy_generation;
  plan.epoch = snap.epoch;

  const Count needed = demand.accelerator_count.value() == 0 ? Count(1) : demand.accelerator_count;

  std::vector<ResourceLayout> devs;
  for (const auto& d : snap.devices) devs.push_back(d.layout);

  FitState now = eval(devs, demand);
  if (now.satisfying >= needed && now.aggregate_free >= demand.aggregate_memory) {
    plan.state = PlanState::BLOCKED;
    plan.precondition_failures.push_back("already fits; no remediation needed");
    plan.factor_explanation.push_back("target already fits; no beneficial action");
    return std::nullopt;
  }

  std::vector<ReclaimCandidate> cands;
  for (size_t i = 0; i < devs.size(); ++i) {
    for (const auto& [id, alloc] : snap.devices[i].layout.allocations()) {
      if (reclaimable(alloc)) {
        double disruption = disruption_of(alloc.movability) *
                            std::max(0.05, 1.0 - weights.displacement_cost_weight.value());
        cands.push_back(ReclaimCandidate{i, id, alloc.length, disruption,
                                         alloc.movability, alloc.owner});
      }
    }
  }
  std::stable_sort(cands.begin(), cands.end(), [](const ReclaimCandidate& a, const ReclaimCandidate& b) {
    if (a.disruption != b.disruption) return a.disruption < b.disruption;
    if (a.length != b.length) return a.length < b.length;
    return a.id.value() < b.id.value();
  });

  std::string err;
  Bytes free_before;
  for (auto& d : devs) free_before = free_before + d.free_capacity();
  const double free_before_v = static_cast<double>(free_before.value());

  for (const auto& c : cands) {
    FitState st = eval(devs, demand);
    if (st.satisfying >= needed && st.aggregate_free >= demand.aggregate_memory) break;

    if (!devs[c.device_index].remove_allocation(c.id, err)) continue;

    PlanAction act;
    act.kind = kind_for(c.movability);
    act.target_resource = snap.devices[c.device_index].resource.id;
    act.affected_owner = c.owner;
    act.source_allocation = c.id;
    act.affected_allocations.push_back(c.id);
    act.required_authority = AuthorityGeneration(snap.epoch.value());
    act.required_preconditions = {
        "source generation current",
        "owning workload allows displacement",
        "state checkpointable/recomputable where required",
        "policy allows action",
        "no protected reservation violated",
    };
    act.proof_obligations = {
        "accounting closed after action",
        "no double-free / double-consume",
    };
    act.expected_capacity_recovered = c.length;
    act.expected_fit_improvement = HealthyScore(1.0);
    act.cost = HealthyScore(std::min(1.0, weights.displacement_cost_weight.value() * c.disruption));
    act.risk = HealthyScore(std::min(1.0, 0.6 * c.disruption));
    act.disruption = HealthyScore(c.disruption);
    act.reversible = c.movability != Movability::EVICTABLE && c.movability != Movability::RECOMPUTABLE;
    act.state_preservation_required = c.movability == Movability::MOVABLE_AFTER_CHECKPOINT ||
                                      c.movability == Movability::MOVABLE_LIVE;
    act.preemption_required = (c.movability == Movability::MOVABLE_LIVE);
    act.reservation_impact = ReservationImpact::NONE;
    act.generations = snap.generations;
    plan.actions.push_back(act);
  }

  FitState final = eval(devs, demand);
  if (final.satisfying < needed || final.aggregate_free < demand.aggregate_memory) {
    plan.state = PlanState::BLOCKED;
    plan.precondition_failures.push_back("no safe remediation makes target fit");
    plan.factor_explanation.push_back("no beneficial action (protected/impossible)");
    return std::nullopt;
  }

  Bytes recovered;
  for (const auto& act : plan.actions) recovered = recovered + act.expected_capacity_recovered;
  plan.expected_capacity_recovered = recovered;
  plan.expected_fit_improvement = HealthyScore(1.0);

  const double rec_v = static_cast<double>(recovered.value());
  const double usable_gain = rec_v / std::max(1.0, free_before_v + rec_v);
  const double benefit_v = std::min(1.0, 0.6 * weights.reclaim_value.value() +
                                               0.4 * usable_gain);
  double disruption_avg = 0.0;
  double cost_v = 0.0;
  double risk_v = 0.0;
  for (const auto& act : plan.actions) {
    disruption_avg += act.disruption.value() / static_cast<double>(plan.actions.size());
    cost_v += act.cost.value();
    risk_v += act.risk.value();
  }
  if (plan.actions.empty()) { disruption_avg = 0.0; cost_v = 0.0; risk_v = 0.0; }
  else { cost_v /= static_cast<double>(plan.actions.size()); risk_v /= static_cast<double>(plan.actions.size()); }

  plan.benefit = HealthyScore(benefit_v);
  plan.cost = HealthyScore(cost_v);
  plan.risk = HealthyScore(risk_v);
  plan.disruption = HealthyScore(disruption_avg);
  plan.factor_explanation.push_back(
      "benefit=" + std::to_string(benefit_v) + " cost=" + std::to_string(cost_v) +
      " risk=" + std::to_string(risk_v) + " disruption=" + std::to_string(disruption_avg));

  if (policy.require_verification) plan.state = PlanState::PLAN_READY;

  if (disruption_avg > policy.max_disruption.value()) {
    plan.state = PlanState::BLOCKED;
    plan.precondition_failures.push_back("plan disruption exceeds policy limit");
  }
  return plan;
}

}  // namespace fragmentation_governor