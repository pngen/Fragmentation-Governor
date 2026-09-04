#ifndef FRAGMENTATION_GOVERNOR_PLANNER_PLAN_STATE_HPP
#define FRAGMENTATION_GOVERNOR_PLANNER_PLAN_STATE_HPP

#include <cstdint>
#include <string_view>

namespace fragmentation_governor {

/// Guarded remediation lifecycle.  A plan cannot become COMPLETED merely
/// because an action was attempted; COMPLETED requires post-action verification.
enum class PlanState : std::uint8_t {
  DETECTED,
  ASSESSING,
  PLAN_READY,
  BLOCKED,
  APPROVED,
  EXECUTING,
  QUIESCING,
  PRESERVING_STATE,
  MOVING,
  REBINDING,
  VERIFYING,
  COMPLETED,
  PARTIALLY_COMPLETED,
  FAILED,
  ABORTED,
  CANCELLED,
  SUPERSEDED,
  REVALIDATION_REQUIRED,
  RETIRED,
};

inline constexpr std::string_view to_string(PlanState s) noexcept {
  switch (s) {
    case PlanState::DETECTED: return "DETECTED";
    case PlanState::ASSESSING: return "ASSESSING";
    case PlanState::PLAN_READY: return "PLAN_READY";
    case PlanState::BLOCKED: return "BLOCKED";
    case PlanState::APPROVED: return "APPROVED";
    case PlanState::EXECUTING: return "EXECUTING";
    case PlanState::QUIESCING: return "QUIESCING";
    case PlanState::PRESERVING_STATE: return "PRESERVING_STATE";
    case PlanState::MOVING: return "MOVING";
    case PlanState::REBINDING: return "REBINDING";
    case PlanState::VERIFYING: return "VERIFYING";
    case PlanState::COMPLETED: return "COMPLETED";
    case PlanState::PARTIALLY_COMPLETED: return "PARTIALLY_COMPLETED";
    case PlanState::FAILED: return "FAILED";
    case PlanState::ABORTED: return "ABORTED";
    case PlanState::CANCELLED: return "CANCELLED";
    case PlanState::SUPERSEDED: return "SUPERSEDED";
    case PlanState::REVALIDATION_REQUIRED: return "REVALIDATION_REQUIRED";
    case PlanState::RETIRED: return "RETIRED";
  }
  return "RETIRED";
}

}  // namespace fragmentation_governor

#endif  // FRAGMENTATION_GOVERNOR_PLANNER_PLAN_STATE_HPP
