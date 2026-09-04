#ifndef FRAGMENTATION_GOVERNOR_VERIFICATION_OUTCOME_HPP
#define FRAGMENTATION_GOVERNOR_VERIFICATION_OUTCOME_HPP

#include <cstdint>
#include <string_view>

namespace fragmentation_governor {

/// Post-remediation verification outcome.  Moving bytes without improving
/// useful capacity is NOT successful defragmentation.
enum class VerificationOutcome : std::uint8_t {
  IMPROVED,
  TARGET_FIT_ACHIEVED,
  PARTIAL_IMPROVEMENT,
  NO_CHANGE,
  REGRESSION,
  VERIFICATION_FAILED,
  REVALIDATION_REQUIRED,
};

inline constexpr std::string_view to_string(VerificationOutcome v) noexcept {
  switch (v) {
    case VerificationOutcome::IMPROVED: return "IMPROVED";
    case VerificationOutcome::TARGET_FIT_ACHIEVED: return "TARGET_FIT_ACHIEVED";
    case VerificationOutcome::PARTIAL_IMPROVEMENT: return "PARTIAL_IMPROVEMENT";
    case VerificationOutcome::NO_CHANGE: return "NO_CHANGE";
    case VerificationOutcome::REGRESSION: return "REGRESSION";
    case VerificationOutcome::VERIFICATION_FAILED: return "VERIFICATION_FAILED";
    case VerificationOutcome::REVALIDATION_REQUIRED: return "REVALIDATION_REQUIRED";
  }
  return "REVALIDATION_REQUIRED";
}

}  // namespace fragmentation_governor

#endif  // FRAGMENTATION_GOVERNOR_VERIFICATION_OUTCOME_HPP
