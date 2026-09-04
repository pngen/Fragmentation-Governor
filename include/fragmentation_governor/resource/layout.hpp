#ifndef FRAGMENTATION_GOVERNOR_RESOURCE_LAYOUT_HPP
#define FRAGMENTATION_GOVERNOR_RESOURCE_LAYOUT_HPP

#include <map>
#include <set>
#include <optional>
#include <string>

#include "fragmentation_governor/core/enums.hpp"
#include "fragmentation_governor/core/types.hpp"
#include "fragmentation_governor/core/units.hpp"
#include "fragmentation_governor/resource/allocation.hpp"

namespace fragmentation_governor {

enum class RegionKind : std::uint8_t { FREE, ALLOCATED };

/// A contiguous canonical region of the layout.  Free regions are free blocks.
struct Region {
  RegionKind kind;
  Bytes offset;
  Bytes length;
  std::optional<AllocationId> alloc_id;  // set when kind == ALLOCATED
};

/// The canonical block layout of one fragmentable resource, represented as an
/// interval map that covers [0, capacity) with no gaps.  Invariants:
///   * used + free == total;
///   * free regions are maximal (never adjacent to another free region);
///   * allocated regions never overlap;
///   * every allocated region is backed by an Allocation with a current
///     generation.
///
/// Largest-free-block and free-capacity are maintained incrementally so common
/// paths are O(log n), never O(n^2).
class ResourceLayout {
 public:
  ResourceLayout() = default;
  explicit ResourceLayout(Bytes capacity) : capacity_(capacity) {
    if (capacity.value() == 0) return;  // degenerate/empty layout
    Region whole{RegionKind::FREE, Bytes(0), capacity_, std::nullopt};
    regions_.emplace(Bytes(0), whole);
    free_lengths_.insert(capacity_);
    free_capacity_ = capacity_;
  }

  ResourceLayout(const ResourceLayout&) = default;
  ResourceLayout& operator=(const ResourceLayout&) = default;

  // ---- queries ------------------------------------------------------------
  Bytes total_capacity() const { return capacity_; }
  Bytes free_capacity() const { return free_capacity_; }
  Bytes used_capacity() const { return capacity_ - free_capacity_; }
  Bytes largest_free_block() const {
    if (free_lengths_.empty()) return Bytes(0);
    return *free_lengths_.rbegin();
  }
  Count allocation_count() const { return alloc_count_; }
  Count free_block_count() const { return Count(free_lengths_.size()); }
  const std::map<Bytes, Region>& regions() const { return regions_; }
  const std::multiset<Bytes>& free_lengths() const { return free_lengths_; }
  const std::map<AllocationId, Allocation>& allocations() const { return allocations_; }
  std::optional<Allocation> find_allocation(const AllocationId& id) const {
    auto it = allocations_.find(id);
    if (it == allocations_.end()) return std::nullopt;
    return it->second;
  }

  /// Insert a freshly governed allocation.  Rejects overlap, out-of-bounds,
  /// duplicate id, zero/saturating length, and stale generation relative to the
  /// layout's known maximum.  On success keeps used+free==total exact.
  bool insert_allocation(const Allocation& a, std::string& err) {
    if (!validate_geometry(a.offset, a.length, err)) return false;
    if (allocations_.count(a.id) != 0) { err = "duplicate allocation id"; return false; }
    auto it = region_containing(a.offset);
    if (it == regions_.end()) { err = "no region contains allocation offset"; return false; }
    if (it->second.kind != RegionKind::FREE) { err = "allocation overlaps an existing allocation"; return false; }
    const Bytes free_start = it->second.offset;
    const Bytes free_len = it->second.length;
    const Bytes free_end = free_start + free_len;
    const Bytes a_end = a.offset + a.length;
    if (a_end > free_end) { err = "allocation exceeds free region"; return false; }

    // Remove the split free block from the indexed free lengths.
    consume_free(free_len);

    // Erase the original free region before re-inserting the sub-regions
    // (otherwise the emplace at the same key silently fails and corrupts the
    // canonical layout).
    regions_.erase(it);

    // Left remainder free region.
    if (a.offset > free_start) {
      const Bytes left = a.offset - free_start;
      regions_.emplace(free_start, Region{RegionKind::FREE, free_start, left, std::nullopt});
      free_lengths_.insert(left);
    }
    // Allocated region.
    regions_.emplace(a.offset, Region{RegionKind::ALLOCATED, a.offset, a.length, a.id});
    // Right remainder free region.
    if (a_end < free_end) {
      const Bytes right = free_end - a_end;
      regions_.emplace(a_end, Region{RegionKind::FREE, a_end, right, std::nullopt});
      free_lengths_.insert(right);
    }

    allocations_.emplace(a.id, a);
    alloc_offsets_[a.id] = a.offset;
    free_capacity_ = free_capacity_ - a.length;
    alloc_count_ = alloc_count_ + Count(1);
    return true;
  }

  /// Update allocation metadata without changing its geometry.
  bool update_allocation(const Allocation& a, std::string& err) {
    auto it = allocations_.find(a.id);
    if (it == allocations_.end()) { err = "allocation not found"; return false; }
    if (it->second.offset != a.offset || it->second.length != a.length) {
      err = "geometry change requires remove+insert";
      return false;
    }
    it->second = a;  // keep id/offset/length; refresh metadata
    alloc_offsets_[a.id] = a.offset;
    return true;
  }

  /// Remove an allocation by id and merge the freed span with free neighbors.
  bool remove_allocation(const AllocationId& id, std::string& err) {
    auto it = allocations_.find(id);
    if (it == allocations_.end()) { err = "allocation not found"; return false; }
    const Bytes off = alloc_offsets_[id];
    const Bytes len = it->second.length;
    auto region = regions_.find(off);
    if (region == regions_.end() || region->second.kind != RegionKind::ALLOCATED ||
        region->second.alloc_id != id) {
      err = "layout inconsistent with allocation registry";
      return false;
    }

    // Gather the freed span, merging with adjacent FREE regions.
    Bytes new_start = off;
    Bytes new_end = off + len;

    // Left neighbor.
    if (region != regions_.begin()) {
      auto left = std::prev(region);
      if (left->second.kind == RegionKind::FREE) {
        new_start = left->second.offset;
        consume_free(left->second.length);
        regions_.erase(left);
      }
    }
    // Right neighbor.
    auto right = std::next(region);
    if (right != regions_.end() && right->second.kind == RegionKind::FREE) {
      new_end = right->second.offset + right->second.length;
      consume_free(right->second.length);
      regions_.erase(right);
    }

    regions_.erase(region);  // remove the allocated region
    const Bytes merged = new_end - new_start;
    regions_.emplace(new_start, Region{RegionKind::FREE, new_start, merged, std::nullopt});
    free_lengths_.insert(merged);

    free_capacity_ = free_capacity_ + len;
    alloc_count_ = alloc_count_ - Count(1);
    allocations_.erase(id);
    alloc_offsets_.erase(id);
    return true;
  }

 private:
  bool validate_geometry(const Bytes& off, const Bytes& len, std::string& err) const {
    if (len.value() == 0) { err = "zero-length allocation"; return false; }
    if ((off.value() + len.value()) > capacity_.value() ||
        off.value() > capacity_.value()) {
      err = "allocation out of bounds";
      return false;
    }
    // offset + length overflow is impossible because both are uint64 and
    // capacity <= uint64 max, but guard explicitly anyway.
    if (len.value() > capacity_.value() - off.value()) {
      err = "allocation end overflow";
      return false;
    }
    return true;
  }

  void consume_free(const Bytes& len) {
    auto it = free_lengths_.find(len);
    if (it != free_lengths_.end()) free_lengths_.erase(it);
  }

  std::map<Bytes, Region>::iterator region_containing(const Bytes& off) {
    auto it = regions_.upper_bound(off);
    if (it == regions_.begin()) return regions_.end();
    --it;
    if (it->second.offset <= off && off < it->second.offset + it->second.length) return it;
    return regions_.end();
  }

  Bytes capacity_;
  std::map<Bytes, Region> regions_;                    // interval map, keyed by offset
  std::multiset<Bytes> free_lengths_;                  // free block length index
  std::map<AllocationId, Bytes> alloc_offsets_;        // id -> offset
  std::map<AllocationId, Allocation> allocations_;     // authoritative metadata
  Bytes free_capacity_ = Bytes(0);
  Count alloc_count_ = Count(0);
};

}  // namespace fragmentation_governor

#endif  // FRAGMENTATION_GOVERNOR_RESOURCE_LAYOUT_HPP