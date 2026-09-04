#ifndef FRAGMENTATION_GOVERNOR_FIT_DEMAND_HPP
#define FRAGMENTATION_GOVERNOR_FIT_DEMAND_HPP

#include <cstdint>
#include <optional>

#include "fragmentation_governor/core/types.hpp"
#include "fragmentation_governor/core/units.hpp"

namespace fragmentation_governor {

/// The shape of a workload demand.  Fragmentation only matters relative to a
/// demand: a system can be highly fragmented for one workload and perfectly
/// usable for another.
struct WorkloadDemand {
  WorkloadDemandId id;
  WorkloadDemandGeneration generation;
  /// Number of accelerator devices required together in one placement group.
  Count accelerator_count;
  /// Memory needed on each device.
  Bytes per_device_memory;
  /// Aggregate physical memory the workload must own.
  Bytes aggregate_memory;
  /// Largest single contiguous span the workload must obtain in one resource.
  Bytes contiguous_memory;
  /// Pinned / host-pinned memory requirement (may be zero).
  Bytes pinned_memory;
  /// Topology group that all devices must belong to, if the workload constrains
  /// placement to a shared interconnect / NUMA locality group.
  std::optional<std::int64_t> topology_group;
  /// Required locality group (e.g. same NUMA node), when constrained.
  std::optional<std::int64_t> locality_group;
  /// Communication-path (e.g. NVLink-class) requirement, when constrained.
  bool requires_highspeed_comm_path = false;
  /// Required reservation interval, when the workload occupies a future window.
  std::optional<Duration> reservation_duration;
  bool requires_model_residency = false;
  bool requires_adapter_residency = false;
  /// Elastic shape (min/max) when the workload can resize.  min must be <= max.
  std::optional<Bytes> elastic_min;
  std::optional<Bytes> elastic_max;
};

}  // namespace fragmentation_governor

#endif  // FRAGMENTATION_GOVERNOR_FIT_DEMAND_HPP
