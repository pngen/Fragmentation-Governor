#ifndef FRAGMENTATION_GOVERNOR_METRICS_METRICS_HPP
#define FRAGMENTATION_GOVERNOR_METRICS_METRICS_HPP

#include <cstdint>
#include <vector>

#include "fragmentation_governor/core/enums.hpp"
#include "fragmentation_governor/core/types.hpp"
#include "fragmentation_governor/core/units.hpp"
#include "fragmentation_governor/fit/demand.hpp"
#include "fragmentation_governor/resource/layout.hpp"

namespace fragmentation_governor {

/// Well-defined fragmentation metrics for one canonical layout.  Each metric is
/// defined mathematically; no invented precision and no single scalar truth.
struct FragmentationMetrics {
  // Aggregate accounting.
  Bytes total_capacity;
  Bytes used_capacity;
  Bytes free_capacity;
  Count allocation_count;
  Count block_count;
  Bytes largest_free_block;

  // Ratios (all in [0,1], defined only when the denominator is non-zero; else 0).
  double external_fragmentation_ratio = 0.0;
  double usable_fraction = 1.0;   // largest_free / free (free>0), else 1.0

  // Stranded / reclaimable / protected (workload-agnostic, single resource).
  Bytes protected_capacity;   // bounded by immovable/reservation/policy protections
  Bytes movable_capacity;     // eligible for displacement (movable classes)
  Bytes reclaimable_capacity; // movable + releasable + evictable + recomputable
  Bytes stranded_capacity;    // free capacity that no single contiguous span can cover
                              // relative to the largest allocation class observed.

  // Workload-specific metrics (only populated when a demand is supplied).
  bool has_workload = false;
  Bytes workload_usable_capacity;   // aggregate capacity usable given contiguous req
  Bytes workload_stranded_capacity; // free - workload_usable_capacity
  Bytes reclaimable_opportunity;    // extra usable capacity from reclaiming movable
  bool workload_fits = false;

  std::vector<FragmentationCategory> dominant_categories;
};

/// Compute metrics for a layout.  When a demand is supplied, workload-specific
/// metrics are populated.
FragmentationMetrics compute_metrics(const ResourceLayout& layout,
                                     const WorkloadDemand* demand = nullptr);

}  // namespace fragmentation_governor

#endif  // FRAGMENTATION_GOVERNOR_METRICS_METRICS_HPP
