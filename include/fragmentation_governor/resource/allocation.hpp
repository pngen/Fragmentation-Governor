#ifndef FRAGMENTATION_GOVERNOR_RESOURCE_ALLOCATION_HPP
#define FRAGMENTATION_GOVERNOR_RESOURCE_ALLOCATION_HPP

#include <optional>
#include <string_view>

#include "fragmentation_governor/core/enums.hpp"
#include "fragmentation_governor/core/types.hpp"
#include "fragmentation_governor/core/units.hpp"
#include "fragmentation_governor/resource/movability.hpp"

namespace fragmentation_governor {

/// A live allocation of a contiguous span within a resource.  Carries the
/// strongly typed generation so that a stale allocation can never participate
/// in current remediation.
struct Allocation {
  AllocationId id;
  AllocationGeneration generation;
  AllocationOwnerId owner;
  Bytes offset;   // start within the canonical layout
  Bytes length;   // contiguous span
  Movability movability = Movability::UNKNOWN;
  /// Set when the allocation is bound to/guarded by a reservation.
  std::optional<ReservationId> reservation;
  /// True when policy forbids displacement regardless of movability.
  bool policy_protected = false;
  /// Optional workload binding.
  std::optional<WorkloadId> workload;
  /// Explicit provenance for this allocation fact.
  Provenance provenance = Provenance::UNKNOWN;

  Bytes end() const { return offset + length; }
};

}  // namespace fragmentation_governor

#endif  // FRAGMENTATION_GOVERNOR_RESOURCE_ALLOCATION_HPP
