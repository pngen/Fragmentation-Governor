#ifndef FRAGMENTATION_GOVERNOR_SNAPSHOT_RESERVATION_HPP
#define FRAGMENTATION_GOVERNOR_SNAPSHOT_RESERVATION_HPP

#include "fragmentation_governor/core/enums.hpp"
#include "fragmentation_governor/core/types.hpp"
#include "fragmentation_governor/core/units.hpp"

namespace fragmentation_governor {

/// A durable future commitment on a resource interval.  Fragmentation Governor
/// must not mutate reservations directly; it may recommend shifts and it must
/// honor them as protection.
struct Reservation {
  ReservationId id;
  ReservationGeneration generation;
  ResourceId resource;
  Duration start;      // start offset from a reference epoch
  Duration end;        // end offset; must satisfy start < end
  Bytes amount;        // capacity committed for the interval
  bool hard;           // true: non-remediable; false: soft / flexibility allowed
  bool policy_protected = false;
  Provenance provenance = Provenance::REPORTED;
};

/// Interval overlap test, using inclusive-start/exclusive-end semantics.
inline bool intervals_overlap(const Duration& a_start, const Duration& a_end,
                              const Duration& b_start, const Duration& b_end) {
  if (a_end <= b_start) return false;
  if (b_end <= a_start) return false;
  return true;
}

}  // namespace fragmentation_governor

#endif  // FRAGMENTATION_GOVERNOR_SNAPSHOT_RESERVATION_HPP
