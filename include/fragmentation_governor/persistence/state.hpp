#ifndef FRAGMENTATION_GOVERNOR_PERSISTENCE_STATE_HPP
#define FRAGMENTATION_GOVERNOR_PERSISTENCE_STATE_HPP

#include <optional>
#include <vector>

#include "fragmentation_governor/core/enums.hpp"
#include "fragmentation_governor/core/types.hpp"
#include "fragmentation_governor/core/units.hpp"
#include "fragmentation_governor/planner/action.hpp"
#include "fragmentation_governor/planner/plan_state.hpp"
#include "fragmentation_governor/snapshot/reservation.hpp"
#include "fragmentation_governor/verification/outcome.hpp"

namespace fragmentation_governor {

/// Lightweight durable record of a completed verification outcome.
struct VerificationResultStub {
  FragmentationPlanId plan;
  VerificationOutcome outcome = VerificationOutcome::REVALIDATION_REQUIRED;
  VerificationGeneration generation;
};

/// A durable, reconstructed remediation plan.  Only well-formed, verified or
/// conservatively-degraded plans are ever persisted.
struct PersistedPlan {
  FragmentationPlanId id;
  FragmentationPlanGeneration generation;
  FragmentationDomainId domain;
  PlanState state = PlanState::DETECTED;
  std::optional<VerificationOutcome> verification;
  PolicyGeneration policy_generation;
  CoordinatorEpoch epoch;
  FragmentationSnapshotGeneration based_on_snapshot;
  Bytes expected_capacity_recovered;
  double benefit = 0.0;
  double cost = 0.0;
  double risk = 0.0;
  double disruption = 0.0;
  Count action_count;
  std::vector<ActionKind> action_kinds;
};

/// The durable governance state that survives a coordinator restart.  Dynamic
/// per-worker evidence (liveness, current allocations, device health) is never
/// persisted as authoritative: after recovery it must be revalidated.
struct PersistedState {
  std::vector<PersistedPlan> plans;
  std::vector<Reservation> reservations;
  std::vector<VerificationResultStub> verification_history;

  ResourceGeneration resource_generation;
  AllocationGeneration allocation_generation;
  ReservationGeneration reservation_generation;
  PolicyGeneration policy_generation;
  CoordinatorEpoch epoch;

  std::uint64_t next_plan_id = 1;
  bool carried_live_dynamic_state = false;
  bool needs_revalidation = true;   // always true after a decode
};

}  // namespace fragmentation_governor

#endif  // FRAGMENTATION_GOVERNOR_PERSISTENCE_STATE_HPP
