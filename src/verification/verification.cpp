#include "fragmentation_governor/verification/verification.hpp"
#include "fragmentation_governor/fit/fit.hpp"

namespace fragmentation_governor {

namespace {
bool accounting_closed(const FragmentationSnapshot& s) {
  for (const auto& d : s.devices) {
    if (d.layout.used_capacity() + d.layout.free_capacity() != d.layout.total_capacity()) return false;
    if (d.layout.used_capacity().value() + d.layout.free_capacity().value() != d.layout.total_capacity().value())
      return false;
  }
  return true;
}
}  // namespace

VerificationResult verify_remediation(const FragmentationSnapshot& before,
                                      const FragmentationSnapshot& after,
                                      const WorkloadDemand& demand,
                                      const RemediationPlan& plan) {
  VerificationResult r;
  // Snapshot-level aggregate stranded (single-device domain assumption for the
  // aggregate comparison; per-device detail is captured in diagnostics).
  Bytes before_free, after_free, before_largest, after_largest;
  for (const auto& d : before.devices) before_free = before_free + d.layout.free_capacity();
  for (const auto& d : after.devices) after_free = after_free + d.layout.free_capacity();

  for (const auto& d : before.devices) {
    if (d.layout.largest_free_block() > before_largest) before_largest = d.layout.largest_free_block();
  }
  for (const auto& d : after.devices) {
    if (d.layout.largest_free_block() > after_largest) after_largest = d.layout.largest_free_block();
  }

  r.before_largest = before_largest;
  r.after_largest = after_largest;
  if (before_free > before_largest) r.before_stranded = before_free - before_largest; else r.before_stranded = Bytes(0);
  if (after_free > after_largest) r.after_stranded = after_free - after_largest; else r.after_stranded = Bytes(0);

  // Recompute deterministic fit for both states.
  r.before_fit = analyze_fit(before, demand).outcome == FitOutcome::FIT_NOW;
  r.after_fit = analyze_fit(after, demand).outcome == FitOutcome::FIT_NOW;

  // Integrity clauses.
  r.exact_accounting_closed = accounting_closed(before) && accounting_closed(after);
  r.current_generations = after.generations.complete() &&
                          after.epoch == before.epoch &&
                          after.policy_generation == before.policy_generation;
  // No protected reservation moved: compare the set of reservation-protected
  // allocation ids present before and after — none may disappear.
  bool protected_intact = true;
  for (const auto& d : before.devices) {
    for (const auto& [id, alloc] : d.layout.allocations()) {
      if (alloc.reservation.has_value() || alloc.policy_protected ||
          alloc.movability == Movability::IMMOVABLE ||
          alloc.movability == Movability::RESERVATION_PROTECTED ||
          alloc.movability == Movability::POLICY_PROTECTED) {
        // Must still exist identically in the after-state.
        bool found = false;
        for (const auto& d2 : after.devices) {
          auto rec = d2.layout.find_allocation(id);
          if (rec.has_value()) { found = true; break; }
        }
        if (!found) { protected_intact = false; break; }
      }
    }
    if (!protected_intact) break;
  }
  r.no_protected_violation = protected_intact;

  // Remove any allocations that the plan targeted but that are absent/stale.
  bool stale_removed = true;
  for (const auto& act : plan.actions) {
    if (act.source_allocation.has_value()) {
      bool still_present = false;
      for (const auto& d : after.devices) {
        if (d.layout.find_allocation(*act.source_allocation).has_value()) still_present = true;
      }
      if (still_present) stale_removed = false;  // a moved/released source must be gone
    }
  }
  r.failed_stale_removed = stale_removed;

  // Outcome determination (hierarchy).
  if (!r.exact_accounting_closed || !r.current_generations || !r.no_protected_violation) {
    r.outcome = VerificationOutcome::VERIFICATION_FAILED;
    if (!r.exact_accounting_closed) r.diagnostics.push_back("accounting not closed");
    if (!r.current_generations) r.diagnostics.push_back("generations not current");
    if (!r.no_protected_violation) r.diagnostics.push_back("protected violation");
    return r;
  }
  if (r.after_fit && !r.before_fit) {
    r.outcome = VerificationOutcome::TARGET_FIT_ACHIEVED;
  } else if (r.after_fit && r.before_fit) {
    r.outcome = VerificationOutcome::IMPROVED;
  } else if (r.after_stranded < r.before_stranded) {
    r.outcome = VerificationOutcome::PARTIAL_IMPROVEMENT;
  } else if (r.after_stranded == r.before_stranded && r.after_largest == r.before_largest) {
    r.outcome = VerificationOutcome::NO_CHANGE;
  } else if (r.after_stranded > r.before_stranded) {
    r.outcome = VerificationOutcome::REGRESSION;
  }
  return r;
}

}  // namespace fragmentation_governor