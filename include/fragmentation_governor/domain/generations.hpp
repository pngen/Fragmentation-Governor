#ifndef FRAGMENTATION_GOVERNOR_DOMAIN_GENERATIONS_HPP
#define FRAGMENTATION_GOVERNOR_DOMAIN_GENERATIONS_HPP

#include "fragmentation_governor/core/types.hpp"

namespace fragmentation_governor {

/// The set of known authoritative generations for a snapshot.  Every element
/// must be supplied; an unknown generation is represented by the null
/// generation (0) and is treated as insufficient evidence.
struct KnownResourceGenerations {
  ResourceGeneration resource_generation;
  AllocationGeneration allocation_generation;
  ReservationGeneration reservation_generation;
  TopologyGeneration topology_generation;
  CapacityModelGeneration capacity_generation;
  CapabilityGeneration capability_generation;
  PlacementGeneration placement_generation;
  HealthGeneration health_generation;
  PolicyGeneration policy_generation;

  /// True when all generation slots required for current evidence are valid
  /// (non-null).
  bool complete() const {
    return resource_generation.is_valid() && allocation_generation.is_valid() &&
           reservation_generation.is_valid() && topology_generation.is_valid() &&
           capacity_generation.is_valid();
  }
};

}  // namespace fragmentation_governor

#endif  // FRAGMENTATION_GOVERNOR_DOMAIN_GENERATIONS_HPP
