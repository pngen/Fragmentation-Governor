#include "fragmentation_governor/metrics/metrics.hpp"

#include <algorithm>

namespace fragmentation_governor {

namespace {
bool eligible_for_reclaim(const Allocation& a) {
  if (a.policy_protected) return false;
  if (a.reservation.has_value()) return false;
  if (!may_move(a.movability)) return false;
  return true;
}

bool is_protected(const Allocation& a) {
  if (a.policy_protected) return true;
  if (a.reservation.has_value()) return true;
  switch (a.movability) {
    case Movability::IMMOVABLE:
    case Movability::RESERVATION_PROTECTED:
    case Movability::POLICY_PROTECTED:
    case Movability::UNKNOWN:
      return true;
    case Movability::MOVABLE_LIVE:
    case Movability::MOVABLE_AFTER_QUIESCE:
    case Movability::MOVABLE_AFTER_CHECKPOINT:
    case Movability::RECOMPUTABLE:
    case Movability::EVICTABLE:
    case Movability::RELEASEABLE:
      return false;
  }
  return true;
}

bool is_movable_class(const Allocation& a) {
  switch (a.movability) {
    case Movability::MOVABLE_LIVE:
    case Movability::MOVABLE_AFTER_QUIESCE:
    case Movability::MOVABLE_AFTER_CHECKPOINT:
      return true;
    default:
      return false;
  }
}
}  // namespace

FragmentationMetrics compute_metrics(const ResourceLayout& layout,
                                     const WorkloadDemand* demand) {
  FragmentationMetrics m;
  m.total_capacity = layout.total_capacity();
  m.used_capacity = layout.used_capacity();
  m.free_capacity = layout.free_capacity();
  m.allocation_count = layout.allocation_count();
  m.largest_free_block = layout.largest_free_block();
  m.block_count = layout.free_block_count();

  if (m.free_capacity.value() > 0) {
    const double largest = static_cast<double>(m.largest_free_block.value());
    const double free = static_cast<double>(m.free_capacity.value());
    m.external_fragmentation_ratio = 1.0 - (largest / free);
    m.usable_fraction = largest / free;
  } else {
    m.external_fragmentation_ratio = 0.0;
    m.usable_fraction = 0.0;
  }

  for (const auto& [id, alloc] : layout.allocations()) {
    (void)id;
    if (is_protected(alloc)) m.protected_capacity = m.protected_capacity + alloc.length;
    if (is_movable_class(alloc)) m.movable_capacity = m.movable_capacity + alloc.length;
    if (eligible_for_reclaim(alloc)) m.reclaimable_capacity = m.reclaimable_capacity + alloc.length;
  }

  if (m.free_capacity > m.largest_free_block) {
    m.stranded_capacity = m.free_capacity - m.largest_free_block;
  } else {
    m.stranded_capacity = Bytes(0);
  }

  if (m.block_count.value() > 1) {
    m.dominant_categories.push_back(FragmentationCategory::EXTERNAL);
    m.dominant_categories.push_back(FragmentationCategory::CONTIGUITY);
  }
  if (m.protected_capacity.value() > 0 && m.stranded_capacity.value() > 0) {
    m.dominant_categories.push_back(FragmentationCategory::OWNERSHIP);
  }

  if (demand != nullptr) {
    m.has_workload = true;
    const bool has_contiguous_req = demand->contiguous_memory.value() > 0;
    const bool fit_now =
        m.free_capacity >= demand->aggregate_memory &&
        (!has_contiguous_req || m.largest_free_block >= demand->contiguous_memory);
    m.workload_fits = fit_now;

    if (fit_now) {
      m.workload_usable_capacity = demand->aggregate_memory;
    } else {
      m.workload_usable_capacity = m.largest_free_block;
    }
    if (m.free_capacity > m.workload_usable_capacity) {
      m.workload_stranded_capacity = m.free_capacity - m.workload_usable_capacity;
    } else {
      m.workload_stranded_capacity = Bytes(0);
    }

    ResourceLayout simulated = layout;
    std::string err;
    for (const auto& [id, alloc] : layout.allocations()) {
      if (eligible_for_reclaim(alloc)) {
        if (!simulated.remove_allocation(id, err)) {
          m.reclaimable_opportunity = Bytes(0);
          return m;
        }
      }
    }
    const Bytes largest_after = simulated.largest_free_block();
    if (largest_after > m.largest_free_block) {
      m.reclaimable_opportunity = largest_after - m.largest_free_block;
    } else {
      m.reclaimable_opportunity = Bytes(0);
    }
  }
  return m;
}

}  // namespace fragmentation_governor
