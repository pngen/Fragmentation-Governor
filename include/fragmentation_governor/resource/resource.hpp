#ifndef FRAGMENTATION_GOVERNOR_RESOURCE_RESOURCE_HPP
#define FRAGMENTATION_GOVERNOR_RESOURCE_RESOURCE_HPP

#include <optional>

#include "fragmentation_governor/core/enums.hpp"
#include "fragmentation_governor/core/types.hpp"
#include "fragmentation_governor/core/units.hpp"

namespace fragmentation_governor {

/// A fragmentable resource (a device's memory, a pinned-host pool, a NUMA node,
/// an accelerator slot, ...).  The generation is the authoritative incarnation:
/// a stale resource generation contributes no current fragmentation evidence.
struct Resource {
  ResourceId id;
  ResourceGeneration generation;
  ResourcePoolId pool;             // logical pool grouping
  DeviceId device;                 // possibly null for non-device domains
  NodeId node;                     // possibly null
  MemoryDomainId memory_domain;    // possibly null
  /// Topology/placement group the resource belongs to, when known.  A resource
  /// that does not report membership cannot be assumed member of any group.
  std::optional<std::int64_t> topology_group;
  Domain domain = Domain::UNKNOWN;
  Bytes capacity;
  Provenance provenance = Provenance::UNKNOWN;
};

/// A logical pool of resources that may be governed together.
struct ResourcePool {
  ResourcePoolId id;
  ResourcePoolGeneration generation;
  std::string_view name;
  Domain domain = Domain::UNKNOWN;
};

}  // namespace fragmentation_governor

#endif  // FRAGMENTATION_GOVERNOR_RESOURCE_RESOURCE_HPP