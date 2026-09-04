#ifndef FRAGMENTATION_GOVERNOR_POLICY_POLICY_HPP
#define FRAGMENTATION_GOVERNOR_POLICY_POLICY_HPP

#include <cstdint>

#include "fragmentation_governor/core/types.hpp"
#include "fragmentation_governor/core/units.hpp"
#include "fragmentation_governor/planner/action.hpp"

namespace fragmentation_governor {

/// Immutable governance policy generation.  A stale policy generation must not
/// authorize remediation.
struct Policy {
  PolicyGeneration generation;
  bool allow_release = true;
  bool allow_eviction = false;
  bool allow_preemption = false;
  bool allow_reservation_shift = false;
  bool require_verification = true;
  /// Maximum acceptable disruption score [0,1] for any generated plan.
  HealthyScore max_disruption = HealthyScore(1.0);
};

/// Caller-supplied priority / value weights.  Fragmentation Governor never
/// invents company-specific scheduling policy; these are explicit inputs.
struct PlanningWeights {
  HealthyScore workload_priority;        // importance of the target workload
  HealthyScore urgency;                  // external urgency
  HealthyScore reclaim_value;            // value of recovered usable capacity
  HealthyScore displacement_cost_weight; // cost of displacing others
  HealthyScore migration_cost_weight;    // cost of moving bytes/state
  HealthyScore disruption_limit;         // max disruption tolerated
};

/// Derive the planning factors' relative order when only priority is known.
inline PlanningWeights default_weights() {
  return PlanningWeights{HealthyScore(0.5), HealthyScore(0.5), HealthyScore(0.6),
                         HealthyScore(0.5), HealthyScore(0.5), HealthyScore(1.0)};
}

}  // namespace fragmentation_governor

#endif  // FRAGMENTATION_GOVERNOR_POLICY_POLICY_HPP
