#include "fragmentation_governor/fit/fit.hpp"

#include <algorithm>
#include <limits>

namespace fragmentation_governor {

namespace {
bool device_satisfies(const DeviceState& d, const WorkloadDemand& demand) {
  return d.layout.free_capacity() >= demand.per_device_memory &&
         d.layout.largest_free_block() >= demand.per_device_memory;
}

bool reclaimable(const Allocation& a) {
  if (a.policy_protected || a.reservation.has_value()) return false;
  return may_move(a.movability);
}

// Returns the number of devices that satisfy the per-device requirement after
// reclaiming all reclaimable allocations.  Does not mutate authoritative state.
Count count_satisfying_after_reclaim(const FragmentationSnapshot& snap,
                                     const WorkloadDemand& demand,
                                     const std::vector<size_t>& indices) {
  Count n;
  for (size_t idx : indices) {
    ResourceLayout sim = snap.devices[idx].layout;
    std::string err;
    for (const auto& [id, alloc] : snap.devices[idx].layout.allocations()) {
      if (reclaimable(alloc)) sim.remove_allocation(id, err);
    }
    if (sim.free_capacity() >= demand.per_device_memory &&
        sim.largest_free_block() >= demand.per_device_memory) {
      n = n + Count(1);
    }
  }
  return n;
}

Bytes aggregate_after_reclaim(const FragmentationSnapshot& snap,
                              const std::vector<size_t>& indices) {
  Bytes total;
  for (size_t idx : indices) {
    ResourceLayout sim = snap.devices[idx].layout;
    std::string err;
    for (const auto& [id, alloc] : snap.devices[idx].layout.allocations()) {
      if (reclaimable(alloc)) sim.remove_allocation(id, err);
    }
    total = total + sim.free_capacity();
  }
  return total;
}
}  // namespace

FitResult analyze_fit(const FragmentationSnapshot& sn, const WorkloadDemand& demand) {
  FitResult r;
  r.required_devices = demand.accelerator_count;

  // ---- Evidence gate ------------------------------------------------------
  if (!sn.generations.complete() || sn.provenance == Provenance::UNKNOWN) {
    r.outcome = FitOutcome::REVALIDATION_REQUIRED;
    r.evidence = EvidenceKind::UNKNOWN;
    r.block_diagnostics.push_back("insufficient current evidence (stale/unknown generations)");
    return r;
  }
  r.evidence = sn.provenance == Provenance::MEASURED ? EvidenceKind::REAL
              : sn.provenance == Provenance::SYNTHETIC ? EvidenceKind::SYNTHETIC
              : EvidenceKind::DERIVED;

  if (sn.devices.empty()) {
    r.outcome = FitOutcome::INSUFFICIENT_EVIDENCE;
    r.block_diagnostics.push_back("no devices in snapshot");
    return r;
  }

  // ---- Select devices (topology/locality filter) ---------------------------
  std::vector<size_t> indices;
  for (size_t i = 0; i < sn.devices.size(); ++i) {
    indices.push_back(i);
  }
  if (demand.topology_group.has_value()) {
    // Devices not reporting the required topology membership cannot contribute.
    std::vector<size_t> filtered;
    for (size_t i : indices) {
      const auto& d = sn.devices[i];
      // Only devices whose topology is compatible (we require a topology token
      // equal to the demanded group).  Absent evidence of compatibility means
      // the device is excluded — UNKNOWN never becomes usable.
      if (d.resource.topology_group.has_value() &&
          *d.resource.topology_group == *demand.topology_group) {
        filtered.push_back(i);
      }
    }
    if (filtered.size() < indices.size()) {
      r.block_diagnostics.push_back("topology group restricts available devices");
    }
    indices = filtered;
  }

  // ---- Count matching devices now -----------------------------------------
  Count now = Count(0);
  Bytes aggregate_free;
  for (size_t i : indices) {
    if (device_satisfies(sn.devices[i], demand)) now = now + Count(1);
    aggregate_free = aggregate_free + sn.devices[i].layout.free_capacity();
  }
  r.matching_devices = now;

  const bool single = demand.accelerator_count.value() == 0;
  const Count needed_count = single ? Count(1) : demand.accelerator_count;
  const bool raw_short = aggregate_free < demand.aggregate_memory;

  // ---- Raw capacity shortage ----------------------------------------------
  if (raw_short) {
    r.outcome = FitOutcome::NO_FIT_RAW_CAPACITY;
    r.category = FragmentationCategory::SPATIAL;
    r.block_diagnostics.push_back("aggregate free capacity below demand");
    return r;
  }

  // ---- Contiguity / size-class shortage -----------------------------------
  if (now < needed_count) {
    if (demand.topology_group.has_value() && indices.size() < needed_count.value()) {
      r.outcome = FitOutcome::NO_FIT_TOPOLOGY;
      r.category = FragmentationCategory::TOPOLOGY;
      r.block_diagnostics.push_back("insufficient devices in required topology group");
      return r;
    }
    const Count after_reclaim = count_satisfying_after_reclaim(sn, demand, indices);
    if (after_reclaim >= needed_count &&
        aggregate_after_reclaim(sn, indices) >= demand.aggregate_memory) {
      r.outcome = FitOutcome::FIT_AFTER_RECLAIM;
      r.category = FragmentationCategory::EXTERNAL;
      r.block_diagnostics.push_back("fits after reclaiming movable/releasable allocations");
      return r;
    }
    // Protected-state check: is the shortfall purely due to immovable/protected
    // capacity occupying the needed contiguous spans?
    bool protected_only = true;
    for (size_t i : indices) {
      for (const auto& [id, alloc] : sn.devices[i].layout.allocations()) {
        if (may_move(alloc.movability) && !alloc.policy_protected && !alloc.reservation.has_value()) {
          protected_only = false;
        }
      }
    }
    if (protected_only) {
      r.outcome = FitOutcome::NO_FIT_PROTECTED_STATE;
      r.category = FragmentationCategory::OWNERSHIP;
      r.block_diagnostics.push_back("only protected/immovable capacity blocks the fit");
      return r;
    }
    r.outcome = FitOutcome::NO_FIT_FRAGMENTATION;
    r.category = FragmentationCategory::CONTIGUITY;
    r.block_diagnostics.push_back("aggregate capacity sufficient but no contiguous span large enough");
    return r;
  }

  // ---- Per-device contiguity within matching set --------------------------
  // Every counted device already satisfies the per-device contiguous span.

  // ---- Reservation / temporal gate ----------------------------------------
  if (demand.reservation_duration.has_value() && !sn.reservations.empty()) {
    Duration earliest;
    bool ok = temporal_continuous_fit(sn.reservations, Duration(0),
                                      Duration(std::numeric_limits<uint64_t>::max() / 2),
                                      *demand.reservation_duration, earliest);
    if (!ok) {
      bool any_hard = false;
      for (const auto& res : sn.reservations) {
        if (res.hard) any_hard = true;
      }
      if (any_hard) {
        r.outcome = FitOutcome::NO_FIT_RESERVATION;
        r.category = FragmentationCategory::RESERVATION;
        r.block_diagnostics.push_back("hard reservation fragments the requested window");
        return r;
      }
      r.outcome = FitOutcome::NO_FIT_TEMPORAL;
      r.category = FragmentationCategory::TEMPORAL;
      r.block_diagnostics.push_back("no continuous interval satisfies required duration");
      return r;
    }
  }

  if (single) {
    // Single-resource demand: the one matching device governs.
    r.outcome = FitOutcome::FIT_NOW;
    r.category = FragmentationCategory::UNKNOWN;
    for (size_t i : indices) {
      if (device_satisfies(sn.devices[i], demand)) {
        r.aggregate_metrics = compute_metrics(sn.devices[i].layout, &demand);
        break;
      }
    }
    return r;
  }

  r.outcome = FitOutcome::FIT_NOW;
  r.category = FragmentationCategory::UNKNOWN;
  return r;
}

bool temporal_continuous_fit(const std::vector<Reservation>& reservations,
                             const Duration& horizon_start, const Duration& horizon_end,
                             const Duration& required, Duration& earliest_start) {
  if (required.nanoseconds() == 0) { earliest_start = horizon_start; return true; }
  // Sort reservation intervals by start.
  std::vector<Reservation> rs = reservations;
  std::sort(rs.begin(), rs.end(),
            [](const Reservation& a, const Reservation& b) { return a.start < b.start; });
  Duration cursor = horizon_start;
  for (const auto& r : rs) {
    // Skip reservations entirely before cursor.
    if (r.end <= cursor) continue;
    // If there is a free window [cursor, r.start) long enough, we found it.
    if (r.start >= cursor) {
      const Duration gap = r.start - cursor;
      if (gap >= required) { earliest_start = cursor; return true; }
    }
    // Advance cursor past this reservation.
    if (r.end > cursor) cursor = r.end;
    if (cursor >= horizon_end) break;
  }
  const Duration tail = horizon_end - cursor;
  if (tail >= required) { earliest_start = cursor; return true; }
  return false;
}

}  // namespace fragmentation_governor