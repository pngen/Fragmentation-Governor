#ifndef FRAGMENTATION_GOVERNOR_PLANNER_PLAN_HPP
#define FRAGMENTATION_GOVERNOR_PLANNER_PLAN_HPP

#include <optional>
#include <string>
#include <vector>

#include "fragmentation_governor/core/enums.hpp"
#include "fragmentation_governor/core/types.hpp"
#include "fragmentation_governor/core/units.hpp"
#include "fragmentation_governor/domain/generations.hpp"
#include "fragmentation_governor/fit/outcome.hpp"
#include "fragmentation_governor/planner/action.hpp"
#include "fragmentation_governor/planner/plan_state.hpp"
#include "fragmentation_governor/verification/outcome.hpp"

namespace fragmentation_governor {

/// Impact of a remediation action on reservations.
enum class ReservationImpact : std::uint8_t {
  NONE,
  SHIFT_ELIGIBLE,
  WOULD_VIOLATE,   // not permitted; action must be excluded
  UNKNOWN,
};
inline constexpr std::string_view to_string(ReservationImpact i) noexcept {
  switch (i) {
    case ReservationImpact::NONE: return "NONE";
    case ReservationImpact::SHIFT_ELIGIBLE: return "SHIFT_ELIGIBLE";
    case ReservationImpact::WOULD_VIOLATE: return "WOULD_VIOLATE";
    case ReservationImpact::UNKNOWN: return "UNKNOWN";
  }
  return "UNKNOWN";
}

/// One structured remediation intent with explicit proof obligations.  An action
/// must not commute to an executable step until every precondition holds.
struct PlanAction {
  ActionKind kind = ActionKind::NO_BENEFICIAL_ACTION;
  ResourceId target_resource;
  AllocationOwnerId affected_owner;
  std::optional<AllocationId> source_allocation;  // allocation being displaced
  std::optional<ResourceId> destination;          // where applicable
  std::vector<AllocationId> affected_allocations;

  AuthorityGeneration required_authority;
  std::vector<std::string> required_preconditions;  // explicit, named
  std::vector<std::string> proof_obligations;       // what must be proven

  Bytes expected_capacity_recovered;
  HealthyScore expected_fit_improvement = HealthyScore(0.0);
  HealthyScore cost = HealthyScore(0.0);
  HealthyScore risk = HealthyScore(0.0);
  HealthyScore disruption = HealthyScore(0.0);
  bool reversible = false;
  bool state_preservation_required = false;
  bool preemption_required = false;
  ReservationImpact reservation_impact = ReservationImpact::NONE;

  /// Generations captured at plan creation; all must be current at execution.
  KnownResourceGenerations generations;
};

/// A candidate remediation plan.  Generation-bound and authority-fenced.
struct RemediationPlan {
  FragmentationPlanId id;
  FragmentationPlanGeneration generation;
  FragmentationDomainId domain_id;
  FragmentationSnapshotGeneration based_on_snapshot;
  PolicyGeneration policy_generation;
  CoordinatorEpoch epoch;
  std::vector<PlanAction> actions;
  // Aggregate scored factors.
  Bytes expected_capacity_recovered;
  HealthyScore expected_fit_improvement = HealthyScore(0.0);
  HealthyScore benefit = HealthyScore(0.0);
  HealthyScore cost = HealthyScore(0.0);
  HealthyScore risk = HealthyScore(0.0);
  HealthyScore disruption = HealthyScore(0.0);
  std::vector<std::string> factor_explanation;   // named factors, transparent
  PlanState state = PlanState::DETECTED;
  std::optional<VerificationOutcome> verification;
  std::vector<std::string> precondition_failures;  // non-empty when BLOCKED
};

}  // namespace fragmentation_governor

#endif  // FRAGMENTATION_GOVERNOR_PLANNER_PLAN_HPP
