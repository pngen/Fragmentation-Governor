#ifndef FRAGMENTATION_GOVERNOR_VERIFICATION_VERIFICATION_HPP
#define FRAGMENTATION_GOVERNOR_VERIFICATION_VERIFICATION_HPP

#include <string>
#include <vector>

#include "fragmentation_governor/core/enums.hpp"
#include "fragmentation_governor/core/types.hpp"
#include "fragmentation_governor/core/units.hpp"
#include "fragmentation_governor/fit/demand.hpp"
#include "fragmentation_governor/planner/plan.hpp"
#include "fragmentation_governor/snapshot/snapshot.hpp"
#include "fragmentation_governor/verification/outcome.hpp"

namespace fragmentation_governor {

/// The outcome of mandatory post-remediation verification.
struct VerificationResult {
  VerificationGeneration generation;
  VerificationOutcome outcome = VerificationOutcome::REVALIDATION_REQUIRED;

  // Before/after measurable quantities.
  Bytes before_stranded;
  Bytes after_stranded;
  bool before_fit = false;
  bool after_fit = false;
  Bytes before_largest;
  Bytes after_largest;

  // Integrity / authority clauses that must all be satisfied.
  bool exact_accounting_closed = false;    // used + free == total on every device
  bool failed_stale_removed = false;       // stale/missing allocations resolved
  bool current_generations = false;        // all observed generations current
  bool no_protected_violation = false;     // no protected reservation moved/released

  std::vector<std::string> diagnostics;
};

/// Verify post-remediation state against the before-snapshot and the target
/// demand.  The protected ids are any allocations/reservations that must not
/// have been displaced.  An action is not "successful defragmentation" unless
/// useful capacity improved or target fit was achieved.
VerificationResult verify_remediation(const FragmentationSnapshot& before,
                                      const FragmentationSnapshot& after,
                                      const WorkloadDemand& demand,
                                      const RemediationPlan& plan);

}  // namespace fragmentation_governor

#endif  // FRAGMENTATION_GOVERNOR_VERIFICATION_VERIFICATION_HPP
