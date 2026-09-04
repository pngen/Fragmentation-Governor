#ifndef FRAGMENTATION_GOVERNOR_PLANNER_PLANNER_HPP
#define FRAGMENTATION_GOVERNOR_PLANNER_PLANNER_HPP

#include <optional>

#include "fragmentation_governor/fit/demand.hpp"
#include "fragmentation_governor/policy/policy.hpp"
#include "fragmentation_governor/planner/plan.hpp"
#include "fragmentation_governor/snapshot/snapshot.hpp"

namespace fragmentation_governor {

/// Deterministically plan remediation that would make `demand` fit `snapshot`.
/// Returns std::nullopt when no beneficial action exists (equivalently, the
/// demand is impossible even after perfect compaction or only protected
/// capacity blocks it).  Ranking uses named, transparent factors.
std::optional<RemediationPlan> plan_remediation(const FragmentationSnapshot& snapshot,
                                                const WorkloadDemand& demand,
                                                const Policy& policy,
                                                const PlanningWeights& weights,
                                                const FragmentationPlanId& plan_id,
                                                const FragmentationPlanGeneration& plan_generation);

}  // namespace fragmentation_governor

#endif  // FRAGMENTATION_GOVERNOR_PLANNER_PLANNER_HPP
