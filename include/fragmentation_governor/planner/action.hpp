#ifndef FRAGMENTATION_GOVERNOR_PLANNER_ACTION_HPP
#define FRAGMENTATION_GOVERNOR_PLANNER_ACTION_HPP

#include <cstdint>
#include <string_view>

namespace fragmentation_governor {

/// Structured remediation intent.  Fragmentation Governor produces intent; it
/// does not itself own every action.  Ownership for each kind is delegated via
/// the appropriate narrow interface / reference adapter.
enum class ActionKind : std::uint8_t {
  MOVE_ALLOCATION,
  COMPACT_POOL,
  RELEASE_UNUSED,
  EVICT_RECOMPUTABLE,
  MIGRATE_STATE,
  REBIND_RESERVATION,
  SHIFT_RESERVATION_WINDOW,
  REPLACE_PLACEMENT,
  PREEMPT_FOR_RECLAIM,
  RELOAD_MODEL_ELSEWHERE,
  MOVE_ADAPTER,
  REBALANCE_RESIDENCY,
  REPACK_SIZE_CLASSES,
  SPLIT_WORKLOAD,
  SHRINK_ELASTIC_WORKLOAD,
  WAIT_FOR_RELEASE,
  REVALIDATE,
  MANUAL_INTERVENTION,
  NO_BENEFICIAL_ACTION,
};

inline constexpr std::string_view to_string(ActionKind a) noexcept {
  switch (a) {
    case ActionKind::MOVE_ALLOCATION: return "MOVE_ALLOCATION";
    case ActionKind::COMPACT_POOL: return "COMPACT_POOL";
    case ActionKind::RELEASE_UNUSED: return "RELEASE_UNUSED";
    case ActionKind::EVICT_RECOMPUTABLE: return "EVICT_RECOMPUTABLE";
    case ActionKind::MIGRATE_STATE: return "MIGRATE_STATE";
    case ActionKind::REBIND_RESERVATION: return "REBIND_RESERVATION";
    case ActionKind::SHIFT_RESERVATION_WINDOW: return "SHIFT_RESERVATION_WINDOW";
    case ActionKind::REPLACE_PLACEMENT: return "REPLACE_PLACEMENT";
    case ActionKind::PREEMPT_FOR_RECLAIM: return "PREEMPT_FOR_RECLAIM";
    case ActionKind::RELOAD_MODEL_ELSEWHERE: return "RELOAD_MODEL_ELSEWHERE";
    case ActionKind::MOVE_ADAPTER: return "MOVE_ADAPTER";
    case ActionKind::REBALANCE_RESIDENCY: return "REBALANCE_RESIDENCY";
    case ActionKind::REPACK_SIZE_CLASSES: return "REPACK_SIZE_CLASSES";
    case ActionKind::SPLIT_WORKLOAD: return "SPLIT_WORKLOAD";
    case ActionKind::SHRINK_ELASTIC_WORKLOAD: return "SHRINK_ELASTIC_WORKLOAD";
    case ActionKind::WAIT_FOR_RELEASE: return "WAIT_FOR_RELEASE";
    case ActionKind::REVALIDATE: return "REVALIDATE";
    case ActionKind::MANUAL_INTERVENTION: return "MANUAL_INTERVENTION";
    case ActionKind::NO_BENEFICIAL_ACTION: return "NO_BENEFICIAL_ACTION";
  }
  return "NO_BENEFICIAL_ACTION";
}

}  // namespace fragmentation_governor

#endif  // FRAGMENTATION_GOVERNOR_PLANNER_ACTION_HPP
