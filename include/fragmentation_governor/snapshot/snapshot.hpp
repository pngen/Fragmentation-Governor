#ifndef FRAGMENTATION_GOVERNOR_SNAPSHOT_SNAPSHOT_HPP
#define FRAGMENTATION_GOVERNOR_SNAPSHOT_SNAPSHOT_HPP

#include <chrono>
#include <map>
#include <vector>

#include "fragmentation_governor/core/enums.hpp"
#include "fragmentation_governor/core/types.hpp"
#include "fragmentation_governor/core/units.hpp"
#include "fragmentation_governor/domain/generations.hpp"
#include "fragmentation_governor/metrics/metrics.hpp"
#include "fragmentation_governor/resource/layout.hpp"
#include "fragmentation_governor/resource/resource.hpp"
#include "fragmentation_governor/snapshot/reservation.hpp"

namespace fragmentation_governor {

/// A single device/resource's authoritative state at snapshot time.
struct DeviceState {
  Resource resource;
  ResourceLayout layout;
};

/// Current fragmentation as an explicit, generation-bound snapshot.  A snapshot
/// binds every relevant generation so that a stale snapshot cannot drive
/// current remediation.
struct FragmentationSnapshot {
  FragmentationSnapshotId id;
  FragmentationSnapshotGeneration generation;
  FragmentationDomainId domain_id;
  FragmentationDomainGeneration domain_generation;
  ResourcePoolId pool;
  ResourcePoolGeneration pool_generation;
  Domain domain = Domain::UNKNOWN;

  KnownResourceGenerations generations;
  WorkerBootId source_worker;      // incarnation of the evidence source
  CoordinatorEpoch epoch;          // coordinator incarnation at snapshot time
  PolicyGeneration policy_generation;
  CapacityModelGeneration capacity_generation;
  TopologyGeneration topology_generation;

  std::chrono::system_clock::time_point evidence_time;
  Provenance provenance = Provenance::UNKNOWN;

  std::vector<DeviceState> devices;
  std::vector<Reservation> reservations;
  std::vector<FragmentationMetrics> metrics;   // parallel to devices
};

}  // namespace fragmentation_governor

#endif  // FRAGMENTATION_GOVERNOR_SNAPSHOT_SNAPSHOT_HPP