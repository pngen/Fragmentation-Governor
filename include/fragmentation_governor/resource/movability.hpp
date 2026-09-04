#ifndef FRAGMENTATION_GOVERNOR_RESOURCE_MOVABILITY_HPP
#define FRAGMENTATION_GOVERNOR_RESOURCE_MOVABILITY_HPP

#include <cstdint>
#include <string_view>

namespace fragmentation_governor {

/// Explicit movability semantics for an allocation / resource consumer.
/// Movement is never assumed merely because bytes could technically be copied.
enum class Movability : std::uint8_t {
  IMMOVABLE,                     // pinned by hardware, ownership, or design
  MOVABLE_LIVE,                  // may be moved while live
  MOVABLE_AFTER_QUIESCE,         // move once quiesced
  MOVABLE_AFTER_CHECKPOINT,      // move after state is preserved
  RECOMPUTABLE,                  // may be dropped and recomputed
  EVICTABLE,                     // may be evicted
  RELEASEABLE,                   // may be released without data loss
  RESERVATION_PROTECTED,         // protected by a reservation
  POLICY_PROTECTED,              // protected by policy
  UNKNOWN,                       // definitive answer is unknown
};

inline constexpr std::string_view to_string(Movability m) noexcept {
  switch (m) {
    case Movability::IMMOVABLE: return "IMMOVABLE";
    case Movability::MOVABLE_LIVE: return "MOVABLE_LIVE";
    case Movability::MOVABLE_AFTER_QUIESCE: return "MOVABLE_AFTER_QUIESCE";
    case Movability::MOVABLE_AFTER_CHECKPOINT: return "MOVABLE_AFTER_CHECKPOINT";
    case Movability::RECOMPUTABLE: return "RECOMPUTABLE";
    case Movability::EVICTABLE: return "EVICTABLE";
    case Movability::RELEASEABLE: return "RELEASEABLE";
    case Movability::RESERVATION_PROTECTED: return "RESERVATION_PROTECTED";
    case Movability::POLICY_PROTECTED: return "POLICY_PROTECTED";
    case Movability::UNKNOWN: return "UNKNOWN";
  }
  return "UNKNOWN";
}

/// True when a consumer with this movability may be displaced to recover
/// capacity.  Positioned as the single authority-safe eligibility predicate so
/// that callers cannot inadvertently treat UNKNOWN as movable.
inline constexpr bool may_move(Movability m) noexcept {
  switch (m) {
    case Movability::MOVABLE_LIVE:
    case Movability::MOVABLE_AFTER_QUIESCE:
    case Movability::MOVABLE_AFTER_CHECKPOINT:
    case Movability::RECOMPUTABLE:
    case Movability::EVICTABLE:
    case Movability::RELEASEABLE:
      return true;
    case Movability::IMMOVABLE:
    case Movability::RESERVATION_PROTECTED:
    case Movability::POLICY_PROTECTED:
    case Movability::UNKNOWN:
      return false;
  }
  return false;
}

}  // namespace fragmentation_governor

#endif  // FRAGMENTATION_GOVERNOR_RESOURCE_MOVABILITY_HPP
