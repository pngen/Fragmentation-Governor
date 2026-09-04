#ifndef FRAGMENTATION_GOVERNOR_FIT_FIT_HPP
#define FRAGMENTATION_GOVERNOR_FIT_FIT_HPP

#include <string>
#include <vector>

#include "fragmentation_governor/core/enums.hpp"
#include "fragmentation_governor/fit/demand.hpp"
#include "fragmentation_governor/fit/outcome.hpp"
#include "fragmentation_governor/metrics/metrics.hpp"
#include "fragmentation_governor/snapshot/snapshot.hpp"

namespace fragmentation_governor {

/// Deterministic result of a shape-aware fit analysis against a snapshot.
struct FitResult {
  FitOutcome outcome = FitOutcome::UNKNOWN;
  FragmentationCategory category = FragmentationCategory::UNKNOWN;
  std::vector<std::string> block_diagnostics;  // human-readable exact blockers
  FragmentationMetrics aggregate_metrics;      // metrics for the governing view
  Count matching_devices;
  Count required_devices;
  EvidenceKind evidence = EvidenceKind::UNKNOWN;
};

/// Analyze whether a workload demand fits a fragmentation snapshot.  Deterministic
/// and returns the exact blocking cause.  UNKNOWN never degrades to FIT_AFTER_*.
FitResult analyze_fit(const FragmentationSnapshot& snapshot, const WorkloadDemand& demand);

/// Given a resource's reservation set and a required contiguous duration
/// (returning the earliest start), answer whether a continuous window exists in
/// [horizon_start, horizon_end).  Returns false if no continuous fit exists.
bool temporal_continuous_fit(const std::vector<Reservation>& reservations,
                             const Duration& horizon_start, const Duration& horizon_end,
                             const Duration& required, Duration& earliest_start);

}  // namespace fragmentation_governor

#endif  // FRAGMENTATION_GOVERNOR_FIT_FIT_HPP
