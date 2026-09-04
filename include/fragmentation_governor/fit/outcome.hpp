#ifndef FRAGMENTATION_GOVERNOR_FIT_OUTCOME_HPP
#define FRAGMENTATION_GOVERNOR_FIT_OUTCOME_HPP

#include <cstdint>
#include <string_view>

namespace fragmentation_governor {

/// Deterministic outcome of a shape-aware fit analysis.  UNKNOWN must never
/// degrade into FIT_AFTER_RECLAIM; a caller that needs certainty must surface
/// REVALIDATION_REQUIRED / INSUFFICIENT_EVIDENCE / UNKNOWN instead.
enum class FitOutcome : std::uint8_t {
  FIT_NOW,
  NO_FIT_RAW_CAPACITY,
  NO_FIT_FRAGMENTATION,
  NO_FIT_CONTIGUITY,
  NO_FIT_TOPOLOGY,
  NO_FIT_LOCALITY,
  NO_FIT_RESERVATION,
  NO_FIT_TEMPORAL,
  NO_FIT_PROTECTED_STATE,
  FIT_AFTER_RECLAIM,
  FIT_AFTER_RELOCATION,
  FIT_AFTER_RESERVATION_CHANGE,
  FIT_AFTER_PREEMPTION,
  FIT_AFTER_REVALIDATION,
  REVALIDATION_REQUIRED,
  INSUFFICIENT_EVIDENCE,
  UNKNOWN,
};

inline constexpr std::string_view to_string(FitOutcome o) noexcept {
  switch (o) {
    case FitOutcome::FIT_NOW: return "FIT_NOW";
    case FitOutcome::NO_FIT_RAW_CAPACITY: return "NO_FIT_RAW_CAPACITY";
    case FitOutcome::NO_FIT_FRAGMENTATION: return "NO_FIT_FRAGMENTATION";
    case FitOutcome::NO_FIT_CONTIGUITY: return "NO_FIT_CONTIGUITY";
    case FitOutcome::NO_FIT_TOPOLOGY: return "NO_FIT_TOPOLOGY";
    case FitOutcome::NO_FIT_LOCALITY: return "NO_FIT_LOCALITY";
    case FitOutcome::NO_FIT_RESERVATION: return "NO_FIT_RESERVATION";
    case FitOutcome::NO_FIT_TEMPORAL: return "NO_FIT_TEMPORAL";
    case FitOutcome::NO_FIT_PROTECTED_STATE: return "NO_FIT_PROTECTED_STATE";
    case FitOutcome::FIT_AFTER_RECLAIM: return "FIT_AFTER_RECLAIM";
    case FitOutcome::FIT_AFTER_RELOCATION: return "FIT_AFTER_RELOCATION";
    case FitOutcome::FIT_AFTER_RESERVATION_CHANGE: return "FIT_AFTER_RESERVATION_CHANGE";
    case FitOutcome::FIT_AFTER_PREEMPTION: return "FIT_AFTER_PREEMPTION";
    case FitOutcome::FIT_AFTER_REVALIDATION: return "FIT_AFTER_REVALIDATION";
    case FitOutcome::REVALIDATION_REQUIRED: return "REVALIDATION_REQUIRED";
    case FitOutcome::INSUFFICIENT_EVIDENCE: return "INSUFFICIENT_EVIDENCE";
    case FitOutcome::UNKNOWN: return "UNKNOWN";
  }
  return "UNKNOWN";
}

}  // namespace fragmentation_governor

#endif  // FRAGMENTATION_GOVERNOR_FIT_OUTCOME_HPP
